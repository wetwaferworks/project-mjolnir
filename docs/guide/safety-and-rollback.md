# Safety, gates and rollback

## Rollback points

| What | Rollback point | How to go back |
|---|---|---|
| M0 toy weights | The snapshot taken before every dream, stored by SHA-256 | Rollback in `mj_store` re-checks the old version before making it live. |
| Real model | The original download, checksummed and read-only | `real-apply --version base` makes the working copy a byte-for-byte copy of the original; `real-verify` proves it. |
| Any edit | Its version in the store and its line in the ledger | `real-apply --version ID` checks out any stored version. |

Versions are never overwritten, not even after they are retired. Retention keeps the newest 14 nightly versions, 14 snapshots and 14 candidates, every pinned milestone, and whatever is live (decision 11: about two weeks).

## Rules every stage keeps

- The original download is never written, only read and checksummed.
- An edited model is not wired into PCHIE's official runners, chat, or any official record.
- Lessons from failed runs are not injected into prompts without JD's go.
- Run logs are read-only. Replays never write beside their source.
- Every dream, score, grade, apply and rollback is a ledger line. A broken chain stops all further writes until it is whole again.
- Run data, stores, ledgers, grades and weights stay out of this repository (see `.gitignore`).

## What a failure looks like

A dream that fails a gate is recorded as `rejected`, leaves the live weights untouched, and writes a failure note that the next dream reads (decision 10). After a drift failure the next dream halves its step size and replays more old memories. A dream that hits a memory, step or time ceiling leaves nothing behind but its ledger line.

## Checking by hand

```
mj_ledger_verify dreams.mjl
mj_store_verify dream-store
```

Both are read-only. The full list of standing gates is in [JD's decisions](decisions.md).
