---
status: accepted
---

# Choices stream to the supervisor as they are drawn

A crashed worker cannot report anything after the fact, yet shrinking a crash needs the exact choice sequence that produced it. So the worker writes each typed choice to the supervisor the moment it is drawn, before the property sees the value. When the worker dies, the supervisor already holds the full sequence up to the crash and shrinks from it.

## Considered options

- **Worker returns the recorded sequence when the case finishes.** Rejected: a crash, hang, or `exit` loses the sequence, which is precisely the case this library exists to handle.

## Consequences

One pipe write per draw. That cost is accepted; it is small next to process isolation, and it makes crashing cases no different from failing ones as far as the engine is concerned.
