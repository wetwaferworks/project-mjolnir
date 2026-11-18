# Security

## Reporting

Please report security issues privately through GitHub's "Report a vulnerability" on this repository rather than in a public issue.

## What is protected, and how

- **Weights.** The original model is never written; edits go to a separate, checksummed working copy and can always be rolled back to the original byte for byte.
- **History.** Dream, grade and edit ledgers are hash-chained and append-only. Tampering, deletion, reordering and torn writes are caught at the exact line. This is tamper-evident, not tamper-proof: someone who controls the machine can rewrite a whole ledger, which is why ledger heads should be recorded elsewhere.
- **Inputs.** Every parser is strict and size-limited, and the parsers are fuzzed in every test run.
- **The labeling page** listens on `127.0.0.1` only and answers only requests that carry its own marker, so another web page cannot post grades.
- **Model files** are read through memory-mapped safetensors headers; no code from a model download is ever run.
- **Keys.** Hosted-model API keys are stored in Windows Credential Manager, never in the repository, commits, logs, ledgers or command lines ([model access](docs/MODEL-ACCESS.md)).

See [safety, gates and rollback](docs/guide/safety-and-rollback.md) for the full rules.
