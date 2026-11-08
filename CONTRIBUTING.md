<div align="center">

# Contributing to Total Markdown

**Thank you for taking the time to improve Total Markdown.**

</div>

---

This guide covers everything you need to send a change that can be merged:
the licensing rules you **must** follow, the architecture the code respects, the
development workflow, and the review process. Please read it before your first
commit.

<details>
<summary><strong>Table of contents</strong></summary>

- [Code of Conduct](#code-of-conduct)
- [Ways to contribute](#ways-to-contribute)
- [Licensing rules (mandatory)](#licensing-rules-mandatory)
- [Architecture constraints](#architecture-constraints)
- [Getting started](#getting-started)
- [Development workflow](#development-workflow)
- [Coding conventions](#coding-conventions)
- [Commit conventions](#commit-conventions)
- [Pull request process](#pull-request-process)
- [Reporting bugs and requesting features](#reporting-bugs-and-requesting-features)
- [Security](#security)
- [Getting help](#getting-help)

</details>

## Code of Conduct

This project follows the [Contributor Covenant, version 2.1][covenant]. By
participating you agree to uphold it. Report unacceptable behaviour to the
maintainer through the contact in [`README.md`](README.md); reports are handled
confidentially.

[covenant]: https://www.contributor-covenant.org/version/2/1/code_of_conduct/

## Ways to contribute

- **Report a bug** — a clear reproduction is worth more than a fix.
- **Suggest a feature** — describe the problem first, then the idea.
- **Improve the documentation** — [`README.md`](README.md),
  [`STYLEGUIDE.md`](STYLEGUIDE.md) and [`src/doc/`](src/doc/index.md).
- **Send a patch** — code, tests, or both; see the workflow below.

If you plan a large change or a new dependency, open an issue first so the
design can be agreed before you invest time in it.

## Licensing rules (mandatory)

Total Markdown is licensed under **GPL-3.0-or-later** (see [`LICENSE`](LICENSE)). The
repository also ships third-party material that keeps its own license:

| Component | Location | License |
|---|---|---|
| md4c parser | `third_party/md4c/` | MIT (Martin Mitaš) |
| Inter typeface | `third_party/fonts/inter/` | SIL OFL 1.1 |
| JetBrains Mono | `third_party/fonts/jetbrains-mono/` | SIL OFL 1.1 |
| `.clang-format` | repository root | MIT (KDE ECM — Christoph Cullmann, Gernot Gebhard) |
| KF6 SyntaxHighlighting | external dependency | MIT (KDE) |

> [!IMPORTANT]
> **These rules are not negotiable. A pull request that breaks them cannot be
> merged.**

1. **Never delete or weaken a license notice.** When editing a file that carries
   a license header (SPDX or plain text), keep the header intact — in particular
   the KDE, MIT and OFL notices anywhere in the tree. When updating a vendored
   file from upstream, carry the new upstream notice over verbatim.
2. **Every change is made in a real person's name.** Git authorship must
   identify the human contributor who reviewed and approved the change.
3. **AI-assisted commits are allowed — the contributor is fully responsible**
   for every line in them, exactly as if they had written it by hand. Review an
   AI-generated diff with the same scrutiny as a stranger's patch.
4. **No AI co-authorship.** Do not add `Co-Authored-By:` trailers crediting AI
   tools, and never credit an AI tool as an author or copyright holder.
5. **New files start with SPDX headers:**

   ```cpp
   // SPDX-FileCopyrightText: 2026 <your name> <your@email>
   // SPDX-License-Identifier: GPL-3.0-or-later
   ```

   Use `SPDX-License-Identifier: MIT` only inside `third_party/`, when porting
   MIT-licensed upstream code, and keep the upstream copyright line.

## Architecture constraints

- **QWidgets only.** The UI is plain Qt Widgets (`QPainter`, `QTextDocument`,
  model/view). QML / Qt Quick dependencies will be rejected.
- **Monolithic.** One executable, one internal static library (`mmd_core`), no
  plugins.
- **One-way dependencies.** `app → editor/viewer → core`. `core` must not
  include anything from the layers above it.
- **Signals are the only glue** between layers; keep the wiring in `MainWindow`.

The reasoning behind these is in [`STYLEGUIDE.md`](STYLEGUIDE.md).

## Getting started

```sh
git clone git@github.com:TankucT-AC/total-markdown.git
cd total-markdown

cmake --preset gcc-debug
cmake --build --preset gcc-debug
ctest --preset gcc-debug
```

Requirements are listed in [`README.md#requirements`](README.md#requirements)
(CMake ≥ 3.24, Ninja, a C++20 compiler, Qt 6.5+, KF6 SyntaxHighlighting).

## Development workflow

The project is developed **test-first**. A change is only complete when it has
a test that would have failed before it.

1. **Write the failing test** (red). One QTest binary per module under
   `tests/`, registered with CTest.
2. **Make it pass** (green) with the smallest reasonable implementation.
3. **Build and run everything:**

   ```sh
   cmake --build --preset gcc-debug && ctest --preset gcc-debug
   ```

4. **Format the files you touched** — the repository `.clang-format` is
   authoritative:

   ```sh
   clang-format -i <files>
   ```

5. **Smoke-test GUI changes** without a monitor:

   ```sh
   QT_QPA_PLATFORM=offscreen ./out/gcc-debug/src/total-markdown feature-showcase.md
   # exercise the feature, then quit; no crashes and no warnings in the log
   ```

6. **Verify with the second compiler** before you request a review:

   ```sh
   cmake --preset clang-release && cmake --build --preset clang-release && ctest --preset clang-release
   ```

`STYLEGUIDE.md` §*Review checklist* lists the mistakes that have actually caused
bugs here (unchecked I/O, a `QSyntaxHighlighter` pass after programmatic text,
`qrc` resources in a static library, and so on). Run through it before pushing.

## Coding conventions

- **Language:** C++20; no compiler extensions (`CMAKE_CXX_EXTENSIONS OFF`).
- **Formatting:** `clang-format` with the repository config (KDE WebKit-based,
  four-space indent, 160-column limit). Do not hand-format.
- **Naming:** Qt style — `MixedCaps` types, `camelCase` members, `m_` prefix for
  private members, no `get` prefix on getters.
- **Headers:** each file carries the SPDX header, has `#ifndef`-guards, and
  includes what it uses.
- **Qt idioms:** `std::clamp` over `qBound`, `QStringList` over `QList<QString>`,
  no `Q_ASSERT` as a release-mode null guard, no unbounded `wait()`.
- **Colors and fonts come from the theme**, never from literals in widgets or
  the renderer.
- **Warnings are strict.** The `mmd_warnings` interface library enables an
  aggressive warning set (`-Wall -Wextra -Wconversion -Wshadow -Wold-style-cast`
  and more). The tree builds with **zero warnings** under both GCC and Clang —
  keep it that way. The flags live in the top-level `CMakeLists.txt`; add new
  ones there rather than per-target.

## Commit conventions

Commits follow [Conventional Commits](https://www.conventionalcommits.org/):

```text
feat(viewer): play animated GIFs via a QTextObjectInterface handler
fix(markdown): render a fenced code block as one continuous region
docs: reference documentation for the public classes
```

- Allowed types: `feat`, `fix`, `refactor`, `docs`, `test`, `style`, `chore`,
  `perf`, `build`, `ci`.
- Scope is optional — usually the module (`viewer`, `editor`, `core`, `app`).
- **One logical step per commit.** The tree must build and pass all tests after
  every commit.
- Keep the subject under ~72 characters; use the body to explain *why*.

## Pull request process

1. Branch from `main` with a descriptive name (`fix/quote-box`, `feat/themes`).
2. Make sure the [workflow](#development-workflow) is green: both compilers,
   all tests, `clang-format`, offscreen smoke.
3. Open the pull request and fill in the template. Link the issue it closes.
4. A maintainer reviews. Expect questions on ownership, threading, error
   handling and naming — they are the categories that have produced real bugs
   here.
5. Address review by adding commits; the branch is squashed or rebased on merge
   at the maintainer's discretion.

By opening a pull request you confirm that you have the right to submit the
change and that it is licensed under **GPL-3.0-or-later**.

## Reporting bugs and requesting features

Please open an issue and include the following (a minimal, self-contained
reproduction is the single most useful thing you can provide):

- **Bug:** what you did, what you expected, what happened; your OS, Qt version
  and the exact `Total Markdown` revision; and, if relevant, the markdown snippet that
  reproduces it.
- **Feature:** the problem you are trying to solve, the idea, and any
  alternatives you considered.

## Security

Do **not** open a public issue for a security problem. Report it privately to
the maintainer (contact in [`README.md`](README.md)) and allow time for a fix
before any public disclosure.

## Getting help

- Questions and ideas: open a Discussion or an issue.
- Before asking, skim [`README.md`](README.md), [`STYLEGUIDE.md`](STYLEGUIDE.md)
  and the reference pages in [`src/doc/`](src/doc/index.md).

---

Released under the **GNU General Public License v3.0 or later** — see
[`LICENSE`](LICENSE).