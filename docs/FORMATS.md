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
