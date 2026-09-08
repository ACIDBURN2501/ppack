# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [2.5.0] - 2026-09-08

### Changed

- Raised the payload limit from 512 to 65,528 bits, or 8,191 logical octets.
  The public `PPACK_MAX_PAYLOAD_BITS` constant defines the runtime and
  compile-time maximum. The default buffer size remains 64 bits.
- Retained function signatures, field-descriptor layout, 32-bit field widths,
  and the packed representation of existing fields.

Callers must provide enough payload storage and enforce transport-specific
limits. Valid sizes above 512 bits no longer fail before buffer access.
The codec still permits partial output on errors. CRC, record publication,
and format-version policy remain the consumer's responsibility.

### Fixed

- Changed field-range validation to ordered subtraction. This prevents
  malformed high offsets from wrapping the check on 16-bit `size_t` targets.

### Added

- Independent bit-at-a-time reference tests for every legal start/width pair,
  every payload size, high-offset mixed types, and boundary value patterns.
- Guard, clearing, input-preservation, and malformed-geometry tests for both
  native and simulated 16-bit-MAU storage.
- Compile-time configuration checks and a maximum-size CI override build.
- A [verification record](docs/large-payload-verification.md) with the range
  argument, compatibility checks, and target-compilation scope.

## [2.4.0] - 2026-07-03

### Fixed

- Return `-PPACK_ERR_INVALARG` when a scaled field produces a NaN result during packing. A NaN source value or a NaN from the scale/offset calculation previously reached a float-to-integer cast. That is undefined behaviour. Positive and negative infinity saturate to the clamp bounds as before.

Added non-finite tests for every scaled type.

### Changed

- Fixed MISRA C:2023 Rule 10.4, 10.7, and 10.8 violations with unsigned suffixes and temporary variables in composite expressions.
- Moved the type-erased struct-member copies into internal `ppack_member_write` and `ppack_member_read` helper functions.

Rule 21.15 deviations are now at exactly three documented sites: the two helpers and the deliberate F32 `float`/`uint32_t` pun. The library no longer suppresses this rule per call site.
- Made operator precedence explicit with parentheses to resolve advisory MISRA C:2023 Rule 12.1 findings.
- Moved `PPACK_PAYLOAD_UNITS` from `ppack_platform.h` to `ppack.h`. This macro is part of the public API.

Include `ppack.h` in your code. Do not include `ppack_platform.h` directly.
- Updated `README.md` and `CONTRIBUTING.md` to describe the tool-driven MISRA workflow.

### Removed

- Removed unused `PPACK_WORD_MASK` macro from `ppack_platform.h`.

### Added

- Added `misch` (cppcheck-backed MISRA C:2023 analysis) via `misra.toml`.

The audit reports zero findings. All deviations (advisory Rules 2.5, 8.7, 11.5, 15.5, 18.4 and required Rule 21.15) are justified at point of use with `cppcheck-suppress` and `@deviation` comments or project-wide in `misra-deviations.txt`.
- Added a compile-time check that `float` is 32 bits. This is required for the `PPACK_TYPE_F32` wire format.
- Added `-fsanitize=float-cast-overflow` to the CI sanitizer jobs.

GCC does not check float-to-integer conversions with `-fsanitize=undefined`. This flag covers that undefined behaviour class on Linux.
- Raised the CI coverage gate from 80% line and 70% branch to 100% for both metrics. The test suite meets this level already.
- Added documentation for the `ppack_pack` and `ppack_unpack` error paths.

The destination buffer contains unspecified data after a non-zero return.

Clarified the 16-bit MAU `PPACK_TYPE_UINT8` storage contract.
The unpack function zeroes the upper 8 bits of the member's storage unit.

## [2.3.0] - 2026-06-19

### Changed

- Changed the memory layout on 16-bit MAU platforms to one octet per addressable unit.

The ppack wire format matches the ucrc CRC library.
- Separated logical payload units from physical MAU size.

`PPACK_ADDR_UNIT_BITS` is always 8.
- Replaced vendor-specific platform names with "16-bit MAU" in the documentation.
- Changed the default `build_tests` Meson option to `false`.

CI enables tests explicitly with `-Dbuild_tests=true`.
- Set the CI coverage gate to 80% line and 70% branch coverage. This matches the shared baseline for all primitives in this repository.

## [2.2.0] - 2026-04-28

### Added

- Added variable-size payload support up to 512 bits, including the `PPACK_PAYLOAD_BITS` override and payload-size validation.
- Added payload-size-focused tests covering legal sizes, 128/256/512-bit round trips, and wire-format lockdown cases.

### Changed

- `ppack_pack` and `ppack_unpack` now take an explicit `payload_bits` argument; passing `64` preserves the previous fixed-payload behaviour.
- Overflow detection now uses the caller-provided payload size instead of the old fixed 64-bit limit.

## [2.0.0] - 2026-04-26

### Changed

- Renamed the `enum ppack_type` constants from `S*` and `U*` style to `<stdint.h>` style (`INT*` and `UINT*`).

This breaks source compatibility. The wire format does not change.

## [1.4.1] - 2026-04-25

### Added

- First public release.

Initial features: `ppack_pack`, `ppack_unpack`, fixed-size 64-bit payloads, bit-aligned field definitions, raw and scaled modes, 8-bit and 16-bit addressable-unit support, MISRA deviation documentation, and CI coverage gates.
