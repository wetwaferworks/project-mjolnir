# Design principles

Project Mjolnir is built to a short list of design principles. Code that contradicts one of them is a bug. Other pages refer to them by number.

1. **Structure.** The four-layer picture in [the architecture](architecture.md) is how the system is built. Editing weights during dreams is the core, and it is the part that has to be right.
2. **What a dream edits.** The full weights, directly. A full copy is saved first, so any dream can be rolled back.
3. **The update rule.** Strengthen whatever led to productive outcomes, and weaken whatever did not.
4. **Step size.** Small nudges at first. Catastrophic forgetting must not happen.
5. **The replay mix.** Older memories that already became better reasoning are mixed in with the new ones.
6. **Dream material.** Replays of recorded runs, circuit experiments and tests, read mainly from the memory file.
7. **Labeling.** Mazes and circuits are labeled automatically, because their outcomes are plainly true or false. The language stage works like a preschool curriculum with a teacher grading in the loop, which needs a labeling tool that helps the teacher step through the runs.
8. **Partial credit counts.** A maze run that clears one pit and falls into the next is a partial success. A pixel screen with three pixels wired right and a mistake on the fourth counts the good wiring as good and marks only the faulty part as needing refinement.
9. **When it dreams.** Every night. New verified experience can also start a dream early, through a "take a nap" function.
10. **Failed dreams.** When verification fails, the dreamer learns why. A teacher grading an outcome as not productive is one source of that reason.
11. **Rollback history.** Keep about two weeks of weight versions, as storage allows.

How these play out in the safety rules is in [Intended use and safety](intended-use-and-safety.md).
