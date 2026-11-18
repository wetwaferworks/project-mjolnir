# The labeling tool

```
cd python
python -m mjolnir label --runs RUN_FOLDER [RUN_FOLDER ...] --data DATA_FOLDER [--port 8765] [--grader JD] [--no-browser]
```

It serves a page on this machine only, at `http://127.0.0.1:8765/`, and steps through recorded runs one step at a time, least certain steps first. Keys and buttons:

| Key | Button | Meaning |
|---|---|---|
| Enter | Agree | take the suggested label |
| 1 or G | Good | a good step |
| 2 or B | Bad | not productive, not yet |
| 3 or P | Productive | it moved the task forward |
| Space or S | Skip | come back to it later |
| Backspace | Back | the previous step; grading it again adds a newer grade |

Buttons are large and named, so Windows Voice Access can press them ("click Good"). Each grade is one line in `DATA_FOLDER/grades.mjl`, a hash-chained MJL1 ledger. Grades are never edited; a newer grade for the same step wins. The page only answers requests from this machine that carry its own marker, so another web page cannot post grades. Full detail: [M1.md, section 1](../M1.md).
