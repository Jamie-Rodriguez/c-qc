# Decide sanitizer integration mechanics

Type: grilling
Status: open
Blocked by: 05, 09

## Question

Given the per-OS sanitizer facts and the worker protocol, decide how `qc` detects a sanitized build at compile time, how the worker performs a recoverable leak check after each case where LeakSanitizer exists, how a sanitizer report is distinguished from a plain crash in the supervisor's classification, and how the report surfaces the sanitizer's own output. Decide what happens on platforms without leak detection so behaviour degrades silently.
