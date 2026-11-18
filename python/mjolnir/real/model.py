"""Run the text part of the model with 4 GB of graphics memory.

The weights stay in the file, mapped read-only; the operating system pages in
what each step needs. The two big embedding tables stay on the processor and
are only read row by row. Each decoder layer is copied to the GPU just before
it runs and dropped right after. The output head and the small parts stay on
the GPU. Layers that an edit needs gradients through can be kept resident.

Loading reads config.json and the safetensors header; no code from the model
download is ever run.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import os

from . import safet

PREFIX = "model.language_model."


class TextModel:
    def __init__(self, model_dir, device="cuda", resident_layers=()):
        import torch
        from transformers import AutoConfig
        from transformers.models.gemma4 import modeling_gemma4 as G

        self.device = device
        self.model_dir = model_dir
        self.file = os.path.join(model_dir, "model-00001-of-00001.safetensors")
        cfg = AutoConfig.from_pretrained(model_dir)
        tc = cfg.text_config
        tc._attn_implementation = "sdpa"
        self.config = tc
        with torch.device("meta"):
            m = G.Gemma4ForCausalLM(tc)
        header, _ = safet.read_header(self.file)
        self.header = header
        sd = {}
        for k in m.state_dict().keys():
            ck = "model.language_model.embed_tokens.weight" if k == "lm_head.weight" else PREFIX + k[len("model."):]
            if ck not in header:
                raise KeyError("weight missing from the file: " + ck)
            sd[k] = safet.map_tensor(self.file, header[ck])
        m.load_state_dict(sd, assign=True, strict=True)
        tm = m.model
        # non-persistent buffers are not in the file: rebuild them exactly as the model does
        tm.rotary_emb = G.Gemma4TextRotaryEmbedding(config=tc, device="cpu")
        tm.embed_tokens.register_buffer("embed_scale", torch.tensor(tc.hidden_size ** 0.5), persistent=False)
        tm.embed_tokens_per_layer.register_buffer(
            "embed_scale", torch.tensor(tc.hidden_size_per_layer_input ** 0.5), persistent=False)
        for p in m.parameters():
            p.requires_grad_(False)
        # small parts and the output head live on the GPU
        for name, mod in tm.named_children():
            if name not in ("embed_tokens", "embed_tokens_per_layer", "layers"):
                mod.to(device)
        self.lm_head_weight = sd["lm_head.weight"].to(device)
        self.softcap = tc.final_logit_softcapping
        # embeddings: computed on the processor from the mapped table, result sent to the GPU
        for emb in (tm.embed_tokens, tm.embed_tokens_per_layer):
            emb.register_forward_hook(lambda mod, args, out: out.to(device))
        self.resident = set(resident_layers)
        self._cpu = {}
        for i, layer in enumerate(tm.layers):
            for mod in layer.modules():  # buffers (such as the layer scalar) are tiny: keep them on the GPU
                for k, b in list(mod._buffers.items()):
                    if b is not None:
                        mod._buffers[k] = b.to(device)
            if i in self.resident:
                layer.to(device)
            else:
                self._stream(i, layer)
        self.m, self.tm = m, tm
        self.overlays = {}  # (layer, name) -> (U, V, scale) applied on the fly, never written

    def _stream(self, i, layer):
        cpu = {n: p.data for n, p in layer.named_parameters()}
        self._cpu[i] = cpu
        dev = self.device

        def pre(mod, args):
            for n, p in mod.named_parameters():
                p.data = cpu[n].to(dev, non_blocking=True)

        def post(mod, args, out):
            for n, p in mod.named_parameters():
                p.data = cpu[n]

        layer.register_forward_pre_hook(pre)
        layer.register_forward_hook(post)

    def mlp_down(self, layer):
        return self.tm.layers[layer].mlp.down_proj

    def add_overlay(self, layer, U, V, scale):
        """Try an edit in memory only: down_proj(x) + scale * (x @ V^T) @ U^T."""
        import torch
        mod = self.mlp_down(layer)
        Ud, Vd = U.to(self.device, torch.float32), V.to(self.device, torch.float32)

        def hook(m, args, out):
            x = args[0].float()
            return out + ((x @ Vd.t()) @ Ud.t() * scale).to(out.dtype)

        h = mod.register_forward_hook(hook)
        self.overlays[layer] = h

    def clear_overlays(self):
        for h in self.overlays.values():
            h.remove()
        self.overlays = {}

    def hidden(self, input_ids, attention_mask):
        out = self.tm(input_ids=input_ids, attention_mask=attention_mask.to(self.device), use_cache=False)
        return out.last_hidden_state

    def logits_at(self, h):
        import torch
        logits = (h.to(self.lm_head_weight.dtype) @ self.lm_head_weight.t()).float()
        if self.softcap:
            logits = torch.tanh(logits / self.softcap) * self.softcap
        return logits
