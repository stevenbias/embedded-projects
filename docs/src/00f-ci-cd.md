---
title: "CI/CD Pipeline: Automated Code Quality"
phase: 0
project: 0
---

# CI/CD Pipeline: Automated Code Quality

> **Prerequisite:** Understand the per-language tools from [00e-project-tooling](00e-project-tooling.md). This chapter shows how they are automated.

Two GitHub Actions workflows run on every push and pull request:

| Workflow | File | Purpose |
|----------|------|---------|
| **Code CI** | `.github/workflows/ci.yml` | Build, lint, test, format-check, doc generation for all language implementations |
| **Docs Deploy** | `.github/workflows/deploy.yml` | Build and deploy the tutorial HTML to GitHub Pages (master branch only) |

The Code CI workflow runs on every push to any branch, providing fast feedback. The Docs Deploy runs only on `master` to publish the tutorial site.

---

## Code CI Workflow (ci.yml)

Each major language gets its own **job**, running in parallel.

### C Job

```yaml
  c:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        project: [01-led-blinker]
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update
      - run: sudo apt-get install -y gcc-arm-none-eabi cppcheck clang-tidy clang-format doxygen
      - run: make -C code/${{ matrix.project }}/c all
      - run: make -C code/${{ matrix.project }}/c lint 2>/dev/null || true
      - run: make -C code/${{ matrix.project }}/c format-check 2>/dev/null || true
      - run: make -C code/${{ matrix.project }}/c doc 2>/dev/null || true
```

**Checks:** compilation succeeds, cppcheck + clang-tidy find no issues, code matches clang-format style, Doxygen generates without errors.

### Rust Job

```yaml
  rust:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        project: [01-led-blinker]
    steps:
      - uses: actions/checkout@v4
      - run: rustup target add thumbv7em-none-eabihf && rustup component add clippy rustfmt
      - run: make -C code/${{ matrix.project }}/rust all
      - run: make -C code/${{ matrix.project }}/rust lint 2>/dev/null || true
      - run: make -C code/${{ matrix.project }}/rust format-check 2>/dev/null || true
      - run: make -C code/${{ matrix.project }}/rust test 2>/dev/null || true
      - run: make -C code/${{ matrix.project }}/rust doc 2>/dev/null || true
```

**Checks:** compilation, Clippy warnings-as-errors, rustfmt compliance, host-based unit tests pass, rustdoc generates without errors.

### Ada and Zig Jobs

Jobs for Ada and Zig are added when implementations exist, following the same pattern.

---

## Docs Deploy Workflow (deploy.yml)

Existing workflow (unchanged):

1. Triggered on push to `master` (or manual via `workflow_dispatch`)
2. Installs `pandoc`
3. Runs `make -C docs deploy` (builds multi-page HTML + copies `00-index.html` to `index.html`)
4. Uploads as GitHub Pages artifact
5. Deploys to `github-pages` environment

**Reference:** [GitHub Pages with Actions](https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site)

---

## Adding a New Project

When a new project is added (e.g., `02-uart-echo`):

1. Create its `c/` and `rust/` Makefiles following the existing pattern
2. Add `02-uart-echo` to the `matrix.project` list in the C and Rust jobs in `ci.yml`
3. For a new language: add a new job to `ci.yml`

---

## Badges

Add a status badge to the repository's `README.md`:

```markdown
[![Code CI](https://github.com/4spa/embedded-projects/actions/workflows/ci.yml/badge.svg)](https://github.com/4spa/embedded-projects/actions/workflows/ci.yml)
```

**Reference:** [GitHub Badges](https://docs.github.com/en/actions/monitoring-and-troubleshooting-workflows/adding-a-workflow-status-badge)

---

## Beyond CI: Release & Versioning

For production embedded projects:

- **Git tags** per project/release (`v01-led-blinker-c-1.0`)
- **Changelog** documenting changes per release
- **Binary artifacts** (`.bin`, `.hex`) attached to GitHub Releases
- **Renode smoke tests** in CI: load the ELF in Renode, verify LED toggling via a script that checks GPIO register state
