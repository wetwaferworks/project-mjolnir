"""The labeling tool: JD steps through recorded runs and grades each step.

A small web page served on this machine only (127.0.0.1). One step at a time:
where the agent stood, what was open, what it proposed, what it chose and what
happened, next to the automatic label. The least certain steps come first.

Made for voice and for hands that should not type much:
  Enter        agree with the suggested label
  1 or G       good
  2 or B       bad (not yet)
  3 or P       productive
  Space or S   skip for now
  Backspace    go back one step (grading it again simply adds a newer grade)
Every button is large and named, so Windows Voice Access can press it by
saying "click Good", "click Bad" and so on.

Grades go into their own hash-chained ledger. Run logs are only read.
The server runs on one thread, so requests are handled strictly one after
another and nothing needs a lock.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import http.server
import json
import urllib.parse

from . import grades, replay

SUGGEST = {"good": "good", "productive": "productive", "not-yet": "bad"}


class Session:
    def __init__(self, runs, grades_path, grader="JD"):
        self.runs = {r["id"]: r for r in runs}
        self.grades_path = grades_path
        self.grader = grader
        self.history = []
        self.skipped = set()
        self.done = grades.latest(grades_path)
        order = []
        for r in runs:
            for s in r["steps"]:
                # uncertain first: smallest gap between the top two proposals,
                # then loops and rewards before plain forward steps
                order.append((round(s["margin"], 6), 0 if s["auto"] != "good" else 1, r["id"], s["index"]))
        order.sort()
        self.order = [(rid, i) for _, _, rid, i in order]

    def next_step(self):
        for key in self.order:
            if key not in self.done and key not in self.skipped:
                return key
        return None

    def view(self, key):
        if key is None:
            return {"finished": True, "stats": self.stats()}
        rid, idx = key
        run = self.runs[rid]
        s = run["steps"][idx]
        trail = [(t["x"], t["y"]) for t in run["steps"][: idx + 1]]
        nxt = run["steps"][idx + 1] if idx + 1 < len(run["steps"]) else None
        tok = {t["token"]: t["action"] for t in run["steps"]}  # the run's own token-to-action map
        return {
            "finished": False, "replay": rid, "index": idx, "count": len(run["steps"]), "source": run["source"],
            "x": s["x"], "y": s["y"], "key": bool(s["key"]), "open": [b for b in range(4) if (int(s["open"] or 0) >> b) & 1],
            "proposals": [[replay.ACTION_NAMES.get(tok.get(p[0]), "token %d" % p[0]), round(p[1], 3)] for p in s["proposals"]],
            "chosen": replay.ACTION_NAMES.get(s["action"], "action %s" % s["action"]),
            "reward": s["reward"], "revisit": s["revisit"], "auto": s["auto"], "suggest": SUGGEST[s["auto"]],
            "map": _map(trail, (nxt["x"], nxt["y"]) if nxt else None),
            "graded": self.done.get(key, [None])[0], "stats": self.stats(),
        }

    def grade(self, rid, idx, label):
        s = self.runs[rid]["steps"][idx]
        grades.add(self.grades_path, rid, idx, label, grader=self.grader, auto=s["auto"])
        self.done[(rid, idx)] = (label, self.grader)
        self.history.append((rid, idx))

    def skip(self, rid, idx):
        self.skipped.add((rid, idx))
        self.history.append((rid, idx))

    def back(self):
        if not self.history:
            return None
        key = self.history.pop()
        self.skipped.discard(key)
        return key

    def stats(self):
        total = len(self.order)
        mine = sum(1 for v in self.done.values() if v[1] == self.grader)
        return {"total": total, "graded": len(self.done), "by_you": mine, "skipped": len(self.skipped)}


def _map(trail, nxt):
    pts = list(trail) + ([nxt] if nxt else [])
    pts = [p for p in pts if isinstance(p[0], int) and isinstance(p[1], int)]
    if not pts:
        return ""
    x0, x1 = min(p[0] for p in pts), max(p[0] for p in pts)
    y0, y1 = min(p[1] for p in pts), max(p[1] for p in pts)
    if (x1 - x0) > 40 or (y1 - y0) > 24:
        return "(map too large to draw)"
    grid = [["." for _ in range(x1 - x0 + 1)] for _ in range(y1 - y0 + 1)]
    for (x, y) in trail[:-1]:
        grid[y - y0][x - x0] = "o"
    if nxt:
        grid[nxt[1] - y0][nxt[0] - x0] = "*"
    hx, hy = trail[-1]
    grid[hy - y0][hx - x0] = "@"
    return "\n".join(" ".join(r) for r in grid)


PAGE = r"""<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Mjolnir labeling</title>
<style>
body{font-family:Segoe UI,system-ui,sans-serif;background:#14101c;color:#eee;margin:0;padding:24px;font-size:22px}
h1{font-size:26px;margin:0 0 12px;color:#c9a6ff}
.row{display:flex;gap:28px;flex-wrap:wrap}
.card{background:#211a2e;border-radius:14px;padding:18px 22px;min-width:320px}
pre{font-size:26px;line-height:1.1;margin:8px 0;color:#fff}
.big{display:flex;gap:18px;margin-top:22px;flex-wrap:wrap}
button{font-size:30px;padding:26px 34px;border-radius:16px;border:3px solid #a970ff;background:#2b2140;color:#fff;cursor:pointer;min-width:220px}
button:focus{outline:4px solid #fff}
#good{border-color:#3fbf6f}#bad{border-color:#e05a5a}#productive{border-color:#e0b43a}
.suggest{box-shadow:0 0 0 6px #a970ff}
.k{color:#aaa;font-size:18px;display:block}
.dim{color:#aaa}.tag{font-weight:700}
</style></head><body>
<h1>Mjolnir: grade the dream material</h1>
<div id="status" class="dim"></div>
<div class="row" id="main"></div>
<div class="big">
<button id="accept" title="Enter">Agree<span class="k">Enter</span></button>
<button id="good">Good<span class="k">1 or G</span></button>
<button id="bad">Bad<span class="k">2 or B</span></button>
<button id="productive">Productive<span class="k">3 or P</span></button>
<button id="skip">Skip<span class="k">Space or S</span></button>
<button id="back">Back<span class="k">Backspace</span></button>
</div>
<script>
let cur=null;
async function api(path,body){const o=body?{method:'POST',headers:{'Content-Type':'application/json','X-Mjolnir':'1'},body:JSON.stringify(body)}:{headers:{'X-Mjolnir':'1'}};const r=await fetch(path,o);return r.json();}
function esc(s){return String(s).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));}
function show(v){cur=v;const st=v.stats;document.getElementById('status').textContent=`${st.graded} of ${st.total} steps graded (${st.by_you} by you), ${st.skipped} skipped this session`;
for(const b of ['good','bad','productive'])document.getElementById(b).classList.remove('suggest');
if(v.error){document.getElementById('main').innerHTML='<div class=card>'+esc(v.error)+'</div>';return;}
if(v.finished){document.getElementById('main').innerHTML='<div class=card>Every step has a grade or was skipped. Thank you.</div>';return;}
document.getElementById(v.suggest).classList.add('suggest');
const props=v.proposals.map(p=>esc(p[0])+' ('+p[1]+')').join(', ')||'none recorded';
document.getElementById('main').innerHTML=`<div class=card><div class=dim>${esc(v.replay)}, step ${v.index+1} of ${v.count}</div>
<div>Standing at (${v.x}, ${v.y}), ${v.key?'holding the key':'no key'}</div><div>Open: ${v.open.map(b=>'move '+b).join(', ')||'nothing'}</div>
<div>Proposed: ${props}</div><div>Chose: <span class=tag>${esc(v.chosen)}</span></div>
<div>Result: ${v.reward>0?'reward '+v.reward:(v.revisit?'went back to visited ground':'new ground')}</div>
<div>Suggested label: <span class=tag>${esc(v.suggest)}</span>${v.graded?' (you graded this '+esc(v.graded)+')':''}</div></div>
<div class=card><div class=dim>Path so far: @ here, o visited, * where it went</div><pre>${esc(v.map)}</pre></div>`;}
async function next(){show(await api('/api/next'));}
async function grade(l){if(!cur||cur.finished)return;show(await api('/api/grade',{replay:cur.replay,index:cur.index,label:l}));}
async function skip(){if(!cur||cur.finished)return;show(await api('/api/skip',{replay:cur.replay,index:cur.index}));}
async function back(){show(await api('/api/back',{}));}
document.getElementById('accept').onclick=()=>cur&&grade(cur.suggest);
document.getElementById('good').onclick=()=>grade('good');document.getElementById('bad').onclick=()=>grade('bad');
document.getElementById('productive').onclick=()=>grade('productive');document.getElementById('skip').onclick=skip;document.getElementById('back').onclick=back;
document.addEventListener('keydown',e=>{if(e.repeat)return;const k=e.key.toLowerCase();
if(k==='enter'){e.preventDefault();cur&&grade(cur.suggest);}else if(k==='1'||k==='g')grade('good');else if(k==='2'||k==='b')grade('bad');
else if(k==='3'||k==='p')grade('productive');else if(k===' '||k==='s'){e.preventDefault();skip();}else if(k==='backspace'||k==='arrowleft'){e.preventDefault();back();}});
next();
</script></body></html>"""


def make_handler(session, port):
    allowed_hosts = {"127.0.0.1:%d" % port, "localhost:%d" % port}

    class Handler(http.server.BaseHTTPRequestHandler):
        server_version = "Mjolnir"

        def log_message(self, fmt, *args):
            pass  # nothing about the grades goes to the console

        def _send(self, code, body, ctype="application/json"):
            data = body.encode("utf-8") if isinstance(body, str) else body
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            self.wfile.write(data)

        def _ok_request(self, needs_marker):
            if self.headers.get("Host") not in allowed_hosts:
                self._send(403, json.dumps({"error": "this page only answers on this machine"}))
                return False
            if needs_marker and self.headers.get("X-Mjolnir") != "1":
                self._send(403, json.dumps({"error": "missing request marker"}))
                return False
            return True

        def do_GET(self):
            path = urllib.parse.urlparse(self.path).path
            if path == "/":
                if self._ok_request(False):
                    self._send(200, PAGE, "text/html; charset=utf-8")
            elif path == "/api/next":
                if self._ok_request(True):
                    self._send(200, json.dumps(session.view(session.next_step())))
            else:
                self._send(404, json.dumps({"error": "not found"}))

        def do_POST(self):
            path = urllib.parse.urlparse(self.path).path
            if not self._ok_request(True):
                return
            try:
                n = int(self.headers.get("Content-Length", "0"))
                if n < 0 or n > 4096:
                    raise ValueError("request too large")
                body = json.loads(self.rfile.read(n) or b"{}")
                if path == "/api/grade":
                    rid, idx, label = body["replay"], int(body["index"]), body["label"]
                    if rid not in session.runs or not 0 <= idx < len(session.runs[rid]["steps"]):
                        raise ValueError("unknown step")
                    session.grade(rid, idx, label)
                    out = session.view(session.next_step())
                elif path == "/api/skip":
                    rid, idx = body["replay"], int(body["index"])
                    if rid not in session.runs:
                        raise ValueError("unknown step")
                    session.skip(rid, idx)
                    out = session.view(session.next_step())
                elif path == "/api/back":
                    key = session.back()
                    out = session.view(key if key else session.next_step())
                else:
                    self._send(404, json.dumps({"error": "not found"}))
                    return
                self._send(200, json.dumps(out))
            except Exception as e:  # report, never crash the page
                self._send(400, json.dumps({"error": str(e), "stats": session.stats()}))

    return Handler


def serve(session, port=8765, open_browser=True):
    httpd = http.server.HTTPServer(("127.0.0.1", port), make_handler(session, port))
    url = "http://127.0.0.1:%d/" % port
    print("Mjolnir labeling is open at %s  (close this window to stop)" % url, flush=True)
    if open_browser:
        import webbrowser
        webbrowser.open(url)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()
