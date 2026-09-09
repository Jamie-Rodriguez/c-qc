# Decide the source layout, Make plus CMake split, amalgamation, and the internal test harness

Type: grilling
Status: open
Blocked by: none

## Question

Decide the source tree for a multi-file engine that amalgamates to one header plus one `.c` on release: module boundaries (engine, choices, shrinker, worker protocol, platform layer, generators, reporting), how the platform layer is split per OS, how the GNU Makefile and the Windows-only CMake file stay in sync, what the amalgamation script is and where it runs, and the shape of the minimal assertion harness the engine's own tests use before `qc` can self-host.
