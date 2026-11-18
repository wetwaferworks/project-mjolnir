"""Labeling tool: grades land in the chain, latest wins, and the page only
answers requests from this machine that carry its marker.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
import http.client
import http.server
import json
import os
import threading
import unittest

from mjolnir import grades, ledger, replay
from mjolnir.labeler import Session, make_handler
from tests.test_replay import MOVES
from tests.util import tmpdir, write_run


class LabelerTest(unittest.TestCase):
    def setUp(self):
        self.d = tmpdir("label-" + self._testMethodName)
        write_run(os.path.join(self.d, "r1.jsonl"), MOVES)
        self.runs = replay.load_all([self.d])
        self.g = os.path.join(self.d, "grades.mjl")

    def test_session_grades_and_back(self):
        s = Session(self.runs, self.g)
        first = s.next_step()
        v = s.view(first)
        self.assertIn(v["suggest"], ("good", "bad", "productive"))
        self.assertIn("@", v["map"])
        s.grade(first[0], first[1], "bad")
        self.assertNotEqual(s.next_step(), first)
        self.assertEqual(s.back(), first)
        s.grade(first[0], first[1], "good")
        latest = grades.latest(self.g)
        self.assertEqual(latest[first], ("good", "JD"))
        chain = ledger.read(self.g)
        self.assertEqual(len(chain), 2)
        self.assertEqual(chain[-1][1]["outcome"], "grade")
        self.assertEqual(Session(self.runs, self.g).stats()["graded"], 1)

    def test_uncertain_first(self):
        s = Session(self.runs, self.g)
        autos = [self.runs[0]["steps"][i]["auto"] for _, i in s.order[:3]]
        self.assertNotIn("good", autos[:1])

    def test_unknown_label_refused(self):
        s = Session(self.runs, self.g)
        k = s.next_step()
        with self.assertRaises(Exception):
            s.grade(k[0], k[1], "great")
        self.assertEqual(ledger.read(self.g), [])

    def test_http(self):
        s = Session(self.runs, self.g)
        httpd = http.server.HTTPServer(("127.0.0.1", 0), None)
        port = httpd.server_address[1]
        httpd.RequestHandlerClass = make_handler(s, port)
        t = threading.Thread(target=httpd.serve_forever, daemon=True)
        t.start()
        try:
            def req(method, path, body=None, headers=None):
                c = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
                c.request(method, path, body=json.dumps(body) if body is not None else None, headers=headers or {})
                r = c.getresponse()
                data = r.read()
                c.close()
                return r.status, data
            st, page = req("GET", "/")
            self.assertEqual(st, 200)
            self.assertIn(b"Productive", page)
            st, _ = req("GET", "/api/next")
            self.assertEqual(st, 403)  # no marker
            st, _ = req("GET", "/", headers={"Host": "evil.example:%d" % port})
            self.assertEqual(st, 403)  # wrong host
            st, data = req("GET", "/api/next", headers={"X-Mjolnir": "1"})
            v = json.loads(data)
            st, data = req("POST", "/api/grade", {"replay": v["replay"], "index": v["index"], "label": "productive"},
                           {"X-Mjolnir": "1", "Content-Type": "application/json"})
            self.assertEqual(st, 200)
            st, _ = req("POST", "/api/grade", {"replay": v["replay"], "index": v["index"], "label": "good"},
                        {"Content-Type": "application/json"})
            self.assertEqual(st, 403)
            st, _ = req("POST", "/api/grade", {"replay": "nope", "index": 0, "label": "good"}, {"X-Mjolnir": "1"})
            self.assertEqual(st, 400)
        finally:
            httpd.shutdown()
            httpd.server_close()
        self.assertEqual(grades.latest(self.g)[(v["replay"], v["index"])][0], "productive")
        self.assertEqual(len(ledger.read(self.g)), 1)


if __name__ == "__main__":
    unittest.main()
