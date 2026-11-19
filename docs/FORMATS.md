# M0 file formats

All text files are ASCII with `\n` line endings. Every parser is strict: one spelling per value, no extra bytes, and anything else is refused.

## Weight version store

```
<store>/
  HEAD                    the live version
  objects/<sha256>.blob   each payload, stored once, named by its SHA-256
  versions/<id>.ver       one immutable manifest per live version
  retired/<id>.ver        manifests moved out by retention (the record is kept)
  pins/<id>.pin           pinned versions, never retired
```

A manifest, in this exact order:

```
MJVER1
id=<1-64 of A-Z a-z 0-9 . _ ->
seq=<decimal, no leading zeros>
kind=snapshot|nightly|milestone|candidate
sha256=<64 lowercase hex>
size=<decimal bytes>
crc32=<8 lowercase hex>
parent=<id or ->
label=<0-256 printable characters, no '|'>
mcrc=<CRC-32 of every byte above this line>
```

`HEAD` is `MJHEAD1`, `id=<id>`, `crc=<CRC-32 of the two lines above>`. It is the one file that is ever replaced, and it is replaced atomically.

## Dream ledger

One record per line, eighteen fields separated by `|`:

```
MJL1|seq|prev|dream|when|trigger|grader|inputs|before|candidate|edit|score_before|score_after|shift_bp|gates|outcome|notes|hash
```

- `seq` counts from 1; `prev` is the previous line's `hash` (64 zeros on line 1).
- `when` is UTC, `YYYY-MM-DDTHH:MM:SSZ`.
- `inputs` is up to 16 replay ids separated by commas, `set-<hash>` for a larger set, or `-`.
- Scores and the shift are basis points, 0 to 10000.
- `outcome` is one of `integrated`, `dry-run-pass`, `rejected`, `aborted`, `rolled-back`, `grade`.
- `hash` is the SHA-256 of every byte before `|hash`.

## Toy weights

144 bytes: `MJTOY1\0\0`, the feature count (8) and action count (4) as single bytes, two zero bytes, 32 little-endian signed 32-bit weights in Q16 fixed point (65536 = 1.0, each within ±64.0), then a CRC-32 of everything before it.

## Hemisphere sample sets (MJHEMI1)

One file per pass or lesson (`pass1.mjhemi`, `pass2.mjhemi`, `lesson.mjhemi`). All integers are little-endian.

Header, 96 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | `MJHEMI1\0` |
| 8 | 2 | version, 1 |
| 10 | 2 | header size, 96 |
| 12 | 4 | round |
| 16 | 1 | pass, 1 or 2 |
| 17 | 1 | the acting side: 0 left, 1 right |
| 18 | 1 | world: 1 toy maze, 2 PCHIE grid maze, 3 circuit |
| 19 | 1 | feature count, 1 to 64 |
| 20 | 1 | move count, 2 to 16 |
| 21 | 3 | zero |
| 24 | 8 | the worlds' seed |
| 32 | 16 | the actor's fingerprint (first 16 bytes of its SHA-256) |
| 48 | 16 | the watcher's fingerprint |
| 64 | 4 | record count, at most 1,048,576 |
| 68 | 4 | record size: 14 + features + 4 × moves |
| 72 | 24 | zero |

Each record: episode (u32), step (u16), the actor's move (u8), the answer key's grade (u8: 1 cleared, 2 not yet, 3 never reached), the watcher's preferred move (u8), the verdict (u8: 1 agree, 2 prefer other, 3 unsure, 4 illegal), the verdict's score in basis points (i16, -10000 to 10000), its confidence in basis points (u16), then the features (i8 each), the actor's probabilities and the watcher's probabilities (u16 each, p × 65535). Records are strictly increasing by (episode, step).

A 32-byte SHA-256 of everything before it ends the file. The reader refuses a wrong magic, version or size, a bad seal, non-zero reserved bytes, a move or grade out of range, a probability on an illegal move, or records out of order.
