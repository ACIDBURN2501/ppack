# Contributing to ppack

ppack is a small C library used as a serialisation primitive for
fixed-size payloads. It is designed to be safe to drop into
embedded firmware and audited environments.

## Getting started

The same commands CI runs, locally:

```sh
# Configure with tests + sanitisers (CI default)
meson setup build --buildtype=debug -Dbuild_tests=true \
                  -Db_sanitize=address,undefined
meson compile -C build
meson test -C build --verbose

# Coverage (CI gate is 100% line + 100% branch)
meson setup build_cov --buildtype=debug -Dbuild_tests=true \
                      -Db_coverage=true
meson compile -C build_cov && meson test -C build_cov
gcovr --root . --filter 'src/' --filter 'include/' --print-summary
```

## Source style

- `.clang-format` is mandatory. Run `clang-format -i` on every modified
  `.c` / `.h` file before submitting.
- 8-space indent, Linux brace style, 80-column limit. Match the existing
  conventions; do not reformat unrelated code.
- The Meson build system is the single source of truth. Update
  `meson.build` / `tests/meson.build` when adding or removing source files.
- No CMake, no Make, no other build systems.

## C language rules

- C11 only (uses `_Static_assert`).
- Use fixed-width types from `<stdint.h>` and `<stdbool.h>`. Never plain
  `int` for fixed-width fields.
- No heap allocation (`malloc`, `free`, VLAs).
- Validate pointer arguments at every public-API boundary.
- Public functions return `int` (negated `PPACK_ERR_*` on failure). No
  `errno`, no exceptions.

## MISRA C:2023

The library is analysed with `misch` (cppcheck-backed MISRA C:2023 analysis), configured by `misra.toml`. The audit is clean: `misch run` must report zero findings. If your change introduces a new finding:

1. Prefer restructuring the code so the rule is satisfied. Deviate only when compliance would make the code genuinely worse, and never restructure purely to hide a violation from the checker; an honest deviation beats evasion.
2. Suppress at point of use with `/* cppcheck-suppress misra-c2012-<rule> ; @deviation <rationale> */`, or add a justified project-wide entry to `misra-deviations.txt` for house-style rules.
3. Verify with `misch run` and `misch deviations` (every suppression must carry a rationale), and justify the deviation in the PR description.

`required` rule deviations face a higher bar than `advisory`. We currently carry exactly three, all Rule 21.15: the type-erased struct-member copies centralised in `ppack_member_write` / `ppack_member_read`, and the deliberate F32 `float`/`uint32_t` pun. Do not add required-rule deviations without strong justification; route new member copies through the existing helpers.

## Tests and coverage

- Add a test for every bug fix.
- Add a test for every new feature.
- All tests must pass on both build configurations: 8-bit native and
  16-bit MAU simulated (`-DPPACK_SIMULATE_16BIT_MAU`).
- CI enforces a 100% line / 100% branch coverage gate. New code without
  tests will fail the coverage gate.
- Tests live in `tests/test_*.c`.
- Fuzz seeds are fixed and recorded in each test. Failures must be
  reproducible from the test name alone. Do not use non-deterministic tests.

For payload-size changes, also run the independent geometry sweep and the
legacy comparison. See [large-payload verification](docs/large-payload-verification.md)
for commands and the limits of these checks.

## Wire format and API stability

ppack defines a public binary wire format. Once a major version is
released, the format is part of the contract:

- The wire format is locked from v1.0.0 onwards. See
  `test_wire_v1_lockdown_*`. Any change to those expected byte arrays is
  a breaking change to the protocol.
- The public API in `ppack.h` is stable across the v1.x line.
- Breaking changes to either go in a new major release (v2.0.0) and
  require a deprecation period.

## Commits

Use Conventional Commits:

- `feat: ...` new feature
- `fix: ...` bug fix
- `doc: ...` documentation only
- `test: ...` test-only changes
- `chore: ...` build, CI, release work
- `refactor: ...` code change that neither fixes a bug nor adds a feature

Keep the subject under ~70 characters. Use the body to explain _why_ the
change is needed, not _what_ the diff already shows.

## Pull requests

- Open an issue first for non-trivial changes so the design can be agreed
  before implementation.
- Keep PRs focused. One feature or one fix per PR.
- The PR description should explain _why_ the change is needed.
- All CI checks must pass: tests on Linux+macOS, ASan+UBSan, release
  build, coverage gate.
- Any new MISRA deviation must be flagged in the PR description.

## When in doubt

Open an issue and discuss before writing code. The library is small
enough that even modest design changes have outsized implications.
