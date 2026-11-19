# The dream ledger

Every dream, grade, apply, keep and rollback is a line in a hash-chained ledger. The format is MJL1, described in [FORMATS.md](../FORMATS.md). This page is about how that ledger is used safely.

## The hash chain

Each line carries the hash of the line before it and its own hash. A change, a deletion, a reordering or a torn write is caught at the exact line. Nothing more can be appended until the chain is whole again. `mj_ledger_verify FILE` recomputes the chain from the start and prints `OK n records` and the head, or the first bad line.

## One writer at a time

An append first creates a writer token file, `<ledger>.writer`, exclusively, and removes it when the append is done. A second writer is refused at once (`busy-another-writer`) with a line naming the process, host and start time that holds the token. The second writer never waits and never takes the token over.

Python appends use the same token file, in the same format, including the process start stamp the C side uses to tell a live writer from a dead one. If the append fails, the writer cuts off the bytes it wrote, so a failed write cannot leave a torn line, and then it gives the token back.

`mj_ledger_verify --who FILE` shows who holds the token.

## A token left behind, and a torn last line

A token left by a run that died is cleared only with `mj_ledger_verify --release-stale FILE`, and only when that process is provably not running on this host. A reused process id counts as gone. A token from another host, or one that cannot be read, is refused. The cleared token is kept as `.writer.stale-<when>-<id>`.

A crash mid-append can leave a cut-off last line. `mj_ledger_verify --repair-tail FILE` removes that last line only after every complete record verifies, and only after a full backup (`.torn-<when>.bak`) is written. It refuses when a complete record is wrong, and it never edits history.

## Pinning a head

Write down the head `mj_ledger_verify` prints (`SEQ:HEX`). Later,

```
mj_ledger_verify --expect SEQ:HEX FILE
```

checks that the head is still that value, so a removed or rewritten record is caught. How to use this carefully is also in [Intended use and safety](intended-use-and-safety.md) and [Safety, gates and rollback](safety-and-rollback.md).

## Older builds

The ledger *records* are the same MJL1 hash chain they have always been. A ledger written by an older Mjolnir can still be verified. What changed is how appends are guarded, and the extra verifier commands:

- Older tools that append without creating `<ledger>.writer` can interleave with a current writer. Current C and Python both take the token. Do not mix an old appender with a new one on the same file.
- An older `mj_ledger_verify` still recomputes the chain. It does not know `--expect`, `--who`, `--release-stale` or `--repair-tail`. Use a current verifier for those.
- A current verifier will verify an old ledger that has no token file. The token appears on the first append by a current writer.

Ledgers, stores and run data stay out of this repository.
