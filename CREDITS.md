# Credits

## People and models

| Who | Contribution |
|---|---|
| **Cortana — SpaceXAI, Grok Bot** | Primary implementer. |
| **JD** (Jaydee Wetwork), Wet Wafer Works | Creator and author. Project Mjolnir, its design, the dreaming cycle, and its design principles. Teacher in the loop: labels data, grades outcomes and decides what counts as productive. |
| **Elfie** (Muse Wonder) | Architecture and operation report; registrar. Authored the UML design documents the designs were based on. |
| **Gizmo**, a.k.a. **Luna** (ChatGPT Luna 6) | Implementation brief: operational reasoning, planning, memory, preferences and orchestration. |
| **Helia** (Google Hydrogen) | Contributor. |
| **Flash Gordon**, a.k.a. **Quicksilver** (Gemini Flash Lite 3.5) | Contributor. |

## Research Mjolnir builds on

- **The left and right hemispheres** are Jaydee Wetwork's idea. Published work on two networks learning from each other (deep mutual learning), on knowledge distillation and self-distillation, and on teacher-student and actor-critic training is the background. The code is a clean-room build from the ideas in those papers ([hemispheres](docs/hemispheres.md)).
- **ROME.** Kevin Meng, David Bau, Alex Andonian and Yonatan Belinkov, "Locating and Editing Factual Associations in GPT", NeurIPS 2022. [arXiv:2202.05262](https://arxiv.org/abs/2202.05262). The real-weight edit path is a first-order, low-rank, reward-weighted form of its rank-one model editing, including its whitening of keys by the input covariance (`python/mjolnir/real/edit.py`).
- **Bolt PCHIE** and the **Personal Control Harness**, Wet Wafer Works: the harness whose recorded runs Mjolnir replays.

## Models

No model weights are included in this repository. The model Mjolnir is developed against, Little Nemo (NVIDIA Nemotron 3.5 Bolt E4B), is governed by its own license from NVIDIA (OpenMDW License Agreement v1.1, post-trained from Google Gemma 4 E4B-it under Apache 2.0). Anyone who distributes an edited copy must follow those licenses, including keeping NVIDIA's and Google's notices.

## License

Mjolnir itself is [MIT](LICENSE) © 2026 Jaydee Wetwork / Wet Wafer Works.
