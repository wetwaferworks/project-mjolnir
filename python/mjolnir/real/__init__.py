"""The real-weight edit path: Mjolnir edits applied to a working copy of a model.

The original download is never written. It is checksummed before and after
every session and kept as the rollback point. Edits are stored as small
low-rank deltas in the weight version store, applied to the working copy by
patching only the tensors they touch, and evaluated before and after.

MIT License, (c) 2026 Jaydee Wetwork / Wet Wafer Works.
"""
