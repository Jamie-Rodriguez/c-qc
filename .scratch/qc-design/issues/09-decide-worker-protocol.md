# Decide the worker protocol

Type: grilling
Status: open
Blocked by: 05, 08

## Question

Decide the wire protocol between supervisor and worker: how the supervisor sends a property identifier and a choice-sequence prefix, how the worker streams each choice back as it is drawn (ADR-0002), how a case's outcome is reported (pass, assert failure with message and location, assume rejection), how the supervisor classifies a dead worker (signal or exception, abort, exit, timeout, sanitizer report), how stderr is captured, and the respawn policy after a death. Decide framing and byte order so the same protocol runs on all three platforms.
