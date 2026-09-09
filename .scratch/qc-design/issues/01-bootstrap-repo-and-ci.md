# Bootstrap the repo and three-OS CI

Type: task
Status: claimed
Blocked by: none

## Question

Turn this directory into a git repository on GitHub with a GitHub Actions matrix that builds and runs a trivial C99 program on Linux (GCC and Clang), macOS (Clang), and Windows (MSVC and clang-cl via the Windows-only CMake file, GNU Make elsewhere). Include a `.gitignore`, the MIT `LICENSE`, and a placeholder `Makefile` and `CMakeLists.txt` so the split is exercised from day one.

Human steps (creating the GitHub repository, authenticating `gh`) go in a precise checklist for the human. The agent does the rest.

Resolved when CI is green on all three platforms. The answer records the repo URL, the workflow file path, and any toolchain quirks discovered (for example how GNU Make and MSVC were reconciled on the Windows runner).
