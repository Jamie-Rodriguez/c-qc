# Bootstrap the repo and three-OS CI

Type: task
Status: resolved
Blocked by: none

## Question

Turn this directory into a git repository on GitHub with a GitHub Actions matrix that builds and runs a trivial C99 program on Linux (GCC and Clang), macOS (Clang), and Windows (MSVC and clang-cl via the Windows-only CMake file, GNU Make elsewhere). Include a `.gitignore`, the MIT `LICENSE`, and a placeholder `Makefile` and `CMakeLists.txt` so the split is exercised from day one.

Human steps (creating the GitHub repository, authenticating `gh`) go in a precise checklist for the human. The agent does the rest.

Resolved when CI is green on all three platforms. The answer records the repo URL, the workflow file path, and any toolchain quirks discovered (for example how GNU Make and MSVC were reconciled on the Windows runner).

## Comments

### 2026-09-10 — agent half done, waiting on the human half

Done locally, committed on `main` as the initial commit:

- `tests/smoke.c` — a C99 program (designated initializer, `<stdbool.h>`, `<stdint.h>`) that exits 0 only if the toolchain compiled it correctly. The exit code is the test, so Make and ctest check the same thing.
- `Makefile` — Unix build. `make check CC=<cc>` compiles with `-std=c99 -pedantic -Wall -Wextra -Werror` into `build/` and runs the program. Compatible with GNU Make 3.81 (macOS's bundled make).
- `CMakeLists.txt` — Windows-only build. Warns (does not fail) on non-Windows so it can be syntax-checked locally. `/W4 /WX` under MSVC and clang-cl; `add_test` on the smoke binary.
- `.github/workflows/ci.yml` — five jobs: ubuntu-24.04 gcc, ubuntu-24.04 clang, macos-15 clang (all via `make check`), windows-2022 MSVC and windows-2022 clang-cl (via CMake, clang-cl selected with `-T ClangCL`).
- `.gitignore`, MIT `LICENSE`.

Verified on this Mac: `make check` passes with Apple clang and Homebrew clang; CMake configure, build, and ctest pass. Failure mode verified: with the smoke program deliberately broken, `make check` exits 2 and ctest exits 8, then restored and re-run green.

**Human checklist** (the agent cannot do these: `gh` is not installed and no GitHub credentials exist here):

1. Install and authenticate the GitHub CLI:
   ```
   brew install gh
   gh auth login
   ```
2. Create the repository from this directory and push. Pick the name you want; `qc` is suggested (the glossary avoids `qc-c` for the library, but the repo name is your call):
   ```
   cd ~/code/qc-c
   gh repo create qc --public --source=. --remote=origin --push
   ```
3. Watch the first CI run and confirm all five jobs are green:
   ```
   gh run watch
   ```
4. If the clang-cl job fails at configure with "toolset ClangCL not found", the runner image dropped the Clang component. Fix: add a step before configure that installs it, or switch that job to `-DCMAKE_C_COMPILER=clang-cl` with the Ninja generator. Report which happened.
5. Rerun `/wayfinder` on this ticket with the repo URL and the run result. The next session records the answer, marks it resolved, and adds the pointer to the map.

Ticket stays `claimed` until then so other sessions skip it.

**Quirks noted so far** (for the answer):

- GNU Make is never used on Windows. The ticket's "GNU Make elsewhere" split means Windows is CMake-only, so nothing had to be reconciled with MSVC on the Windows runner; MSVC versus clang-cl is a CMake toolset switch, not a separate build file.
- MSVC has no `-std=c99` equivalent. `CMAKE_C_STANDARD 99` is a no-op there, so strictness on Windows rests on `/W4 /WX`; the C99 header rule is enforced by the GCC and Clang jobs with `-pedantic -Werror`.
- The MSVC 2015 floor is not exercised by CI. The Windows runner has Visual Studio 2022 only. Checking VS2015 compatibility stays a manual or fog item.

### 2026-09-11 — human half done, CI green

The human created the repository and pushed. The first run on `main` (commit `1e3dcac`) completed with all five jobs green, no clang-cl toolset workaround needed.

## Answer

- **Repo URL:** https://github.com/Jamie-Rodriguez/c-qc (public, default branch `main`). The repo is named `c-qc`; the library and its prefix stay `qc` per the glossary.
- **Workflow file:** `.github/workflows/ci.yml`. Five jobs: `ubuntu-24.04 / gcc`, `ubuntu-24.04 / clang`, `macos-15 / clang` (all `make check`), `windows-2022 / msvc` and `windows-2022 / clang-cl` (CMake, clang-cl selected with `-T ClangCL`).
- **First green run:** https://github.com/Jamie-Rodriguez/c-qc/actions/runs/34567535801. The `ClangCL` toolset was present on the `windows-2022` image, so no install step was needed.
- **Build entry points:** `make check CC=<cc>` on Unix; `cmake -S . -B build && cmake --build build && ctest --test-dir build` on Windows. `tests/smoke.c` is the C99 canary; its exit code is the test.

**Toolchain quirks recorded:**

- GNU Make is never used on Windows. Windows is CMake-only, so MSVC versus clang-cl is a CMake toolset switch, not a second build file.
- MSVC has no `-std=c99`; `CMAKE_C_STANDARD 99` is a no-op there. Windows strictness rests on `/W4 /WX`; the strict-C99 rule for the public header is enforced by the GCC and Clang jobs with `-std=c99 -pedantic -Werror`.
- The MSVC 2015 floor is not exercised by CI (the runner ships Visual Studio 2022 only). Checking VS2015 compatibility remains a fog item.
- `gh` is not installed on the author's machine; CI status was read through the unauthenticated public REST API. Future sessions can do the same without credentials.
