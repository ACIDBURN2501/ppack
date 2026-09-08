# ppack

[![CI](https://github.com/aajll/ppack/actions/workflows/ci.yml/badge.svg)](https://github.com/aajll/ppack/actions/workflows/ci.yml)

A generic payload serialisation library for bit-aligned data fields in C.

## Features

- **Bit-aligned fields** - Fields can start at any bit position and span arbitrary bit ranges
- **Variable payload size** - Caller supplies a multiple of 8 bits, up to 65,528 bits. CAN classic and CAN-FD sizes remain supported.
- **No dynamic memory** - Fixed-size operations, no `malloc` / `free`
- **Deterministic WCET** - All operations have bounded execution time
- **Scaled fields** - Linear scale/offset transformations for physical-unit encoding
- **Multiple types** - Supports `u8`, `u16`, `i16`, `u32`, `i32`, `f32`, and raw bitfields
- **Cross-platform** - Works on any C11 target with an 8-bit or 16-bit minimum addressable unit; auto-detected from `<limits.h>` with no chip-specific code paths
- **Error codes** - All APIs return explicit error codes; no `errno`, no exceptions

## Requirements

- A C11-compatible toolchain (uses `_Static_assert`)
- Conformant `<stdint.h>` and `<limits.h>`
- IEEE-754 binary32 `float` (every modern target the library targets uses this)

## Installation

### Copy-in (recommended for embedded targets)

Copy three files into your project tree:

```
include/ppack.h
include/ppack_platform.h
src/ppack.c
```

Then include the public header:

```c
#include "ppack.h"
```

`ppack_platform.h` is auto-included by `ppack.h`. Make sure both headers are reachable from the same include path.

### Meson subproject

Add this repo as a wrap dependency or subproject:

```meson
ppack_dep = dependency('ppack', fallback : ['ppack', 'ppack_dep'])
```

## Quick Start

```c
#include <stddef.h>
#include <stdint.h>
#include "ppack.h"

typedef struct {
        uint16_t rpm;
        int16_t  temperature;
        float    voltage;
} engine_data_t;

static const struct ppack_field fields[] = {
        {
                .type       = PPACK_TYPE_UINT16,
                .start_bit  = 0,
                .bit_length = 16,
                .ptr_offset = offsetof(engine_data_t, rpm),
                .behaviour  = PPACK_BEHAVIOUR_RAW,
        },
        {
                .type       = PPACK_TYPE_INT16,
                .start_bit  = 16,
                .bit_length = 16,
                .ptr_offset = offsetof(engine_data_t, temperature),
                .behaviour  = PPACK_BEHAVIOUR_RAW,
        },
        /*
         * Voltage: encode as a 16-bit unsigned integer with 0.01 V/LSB
         * resolution.  The struct member must be float when using
         * PPACK_BEHAVIOUR_SCALED.  PPACK_TYPE_F32 performs a raw 32-bit
         * IEEE 754 copy and does not support scale/offset.
         */
        {
                .type       = PPACK_TYPE_UINT16,
                .start_bit  = 32,
                .bit_length = 16,
                .ptr_offset = offsetof(engine_data_t, voltage),
                .scale      = 0.01f,
                .offset     = 0.0f,
                .behaviour  = PPACK_BEHAVIOUR_SCALED,
        },
};

int main(void)
{
        ppack_byte_t payload[PPACK_PAYLOAD_UNITS] = {0};
        engine_data_t tx = {.rpm = 3000, .temperature = -10, .voltage = 12.5f};
        engine_data_t rx = {0};

        /* Pack structure into a 64-bit payload (CAN classic). The third
         * argument is the payload size in bits — pass 128, 256, 512,
         * etc. for CAN-FD or other larger frames. */
        int ret = ppack_pack(&tx, payload, 64, fields, 3);
        if (ret != PPACK_SUCCESS) {
                return ret;
        }

        /* Unpack payload back into structure */
        ret = ppack_unpack(&rx, payload, 64, fields, 3);

        return ret;
}
```

`ppack_byte_t[PPACK_PAYLOAD_UNITS]` defaults to a 64-bit payload (`uint8_t[8]` on byte-addressable targets, `uint16_t[8]` on 16-bit MAU targets) and is intended as a convenience for the common case. For non-default sizes, override `PPACK_PAYLOAD_BITS` at the toolchain level (e.g. `-DPPACK_PAYLOAD_BITS=512`) or size the buffer manually as `ppack_byte_t payload[N / PPACK_ADDR_UNIT_BITS]`. The wire format is identical across MAU sizes.

### CAN-FD (512-bit) example

For a CAN-FD frame with a full 64-byte data field, declare the
buffer manually and pass `512` as `payload_bits`:

```c
typedef struct {
        uint32_t timestamp_ms;
        uint16_t sequence;
        int32_t  current_ma;
        /* ...up to 512 bits' worth of fields... */
} canfd_telemetry_t;

static const struct ppack_field canfd_fields[] = {
        {
                .type = PPACK_TYPE_UINT32,
                .start_bit =   0,
                .bit_length = 32,
                .ptr_offset = offsetof(canfd_telemetry_t, timestamp_ms),
                .behaviour = PPACK_BEHAVIOUR_RAW
        },
        {
                .type = PPACK_TYPE_UINT16,
                .start_bit =  32,
                .bit_length = 16,
                .ptr_offset = offsetof(canfd_telemetry_t, sequence),
                .behaviour = PPACK_BEHAVIOUR_RAW
        },
        {
                .type = PPACK_TYPE_INT32,
                .start_bit = 480,
                .bit_length = 32,
                .ptr_offset = offsetof(canfd_telemetry_t, current_ma),
                .behaviour = PPACK_BEHAVIOUR_RAW
        },
};

ppack_byte_t payload[512u / PPACK_ADDR_UNIT_BITS] = {0};

uint32_t time = get_time_ms();
uint16_t seq = get_seq_multiplex("current");
int32_t curr = get_current_scaled();

canfd_telemetry_t tx = { .timestamp_ms = time, .sequence = seq, .current_ma = curr };

int ret = ppack_pack(&tx, payload, 512, canfd_fields, 3);
```

Or, if every payload in your project is 512 bits, set
`-DPPACK_PAYLOAD_BITS=512` at the toolchain level and continue using
`ppack_byte_t payload[PPACK_PAYLOAD_UNITS]`. The runtime API still
takes the size explicitly.

### Larger stored records

Stored records can use up to `PPACK_MAX_PAYLOAD_BITS`, which is 65,528 bits
or 8,191 logical octets. Individual fields remain limited to 32 bits.
For example, a 1,024-bit record needs 128 payload storage units:

```c
ppack_byte_t record[1024u / PPACK_ADDR_UNIT_BITS];
int ret = ppack_pack(&parameters, record, 1024u,
                    parameter_fields, parameter_field_count);
```

Increasing `payload_bits` does not resize a buffer. Allocate at least
`payload_bits / 8` elements of `ppack_byte_t` before calling the codec.
Each logical octet occupies one 16-bit storage unit on a 16-bit MAU target.
The default `PPACK_PAYLOAD_BITS` remains 64. Do not allocate the maximum
on a small stack unless that space is available.

The codec no longer rejects valid sizes above 512 bits. Callers must enforce
CAN-FD and other transport limits before calling it. Audit callers that relied
on the old rejection to protect a smaller buffer.

The packed representation of existing fields does not change. The codec does
not provide CRC, schema versioning, or atomic record updates. Decode into a
temporary structure and publish it only after a successful return. On failure,
pack and unpack can leave partial output. Record-format changes still require
the consumer's version and recovery policy.

See [large-payload verification](docs/large-payload-verification.md) for the
range argument, test coverage, and remaining caller obligations.

## Building

```sh
# Library only (release)
meson setup build --buildtype=release -Dbuild_tests=false
meson compile -C build

# With unit tests (host plus simulated 16-bit MAU)
meson setup build --buildtype=debug -Dbuild_tests=true
meson compile -C build
meson test -C build --verbose
```

The test target runs twice: once natively (8-bit MAU) and once with `-DPPACK_SIMULATE_16BIT_MAU` to exercise the word-addressable code path that runs on 16-bit MAU platforms.

### Code coverage

```sh
# Configure with gcov instrumentation
meson setup build_cov --buildtype=debug -Dbuild_tests=true -Db_coverage=true
meson compile -C build_cov
meson test -C build_cov

# Generate report (requires gcovr; pip install gcovr)
gcovr --root . --filter 'src/' --filter 'include/' --print-summary
gcovr --root . --filter 'src/' --filter 'include/' --html-details build_cov/coverage.html
```

CI gates on 100% line and 100% branch coverage. The library's safety case depends on every reachable code path being exercised by an explicit test; any drop from this baseline is a deliberate decision that must be approved alongside a justification for the new gap.

## API Reference

### Pack / Unpack

```c
int ppack_pack(const void *base_ptr, void *payload, size_t payload_bits,
               const struct ppack_field *fields, size_t field_count);

int ppack_unpack(void *base_ptr, const void *payload, size_t payload_bits,
                 const struct ppack_field *fields, size_t field_count);
```

`payload_bits` is the payload size in bits. It must be a positive multiple
of `PPACK_ADDR_UNIT_BITS`, which is always 8. The maximum is
`PPACK_MAX_PAYLOAD_BITS`, or 65,528. Common values are `64` for CAN classic
and `512` for full CAN-FD frames. The caller must enforce transport limits.

### Field Descriptor

```c
struct ppack_field {
        enum ppack_type      type;        /* Data type */
        uint16_t             start_bit;   /* Bit position (0..payload_bits-1) */
        uint16_t             bit_length;  /* Number of bits */
        size_t               ptr_offset;  /* offsetof() into base structure */
        float                scale;       /* Scaling factor (default: 1.0) */
        float                offset;      /* Offset value (default: 0.0) */
        enum ppack_behaviour behaviour;   /* PPACK_BEHAVIOUR_RAW or _SCALED */
};
```

### Supported Types

| Type                | Description                          | Struct member type (RAW) |
| ------------------- | ------------------------------------ | ------------------------ |
| `PPACK_TYPE_UINT8`  | 8-bit unsigned                       | `uint8_t`                |
| `PPACK_TYPE_UINT16` | 16-bit unsigned                      | `uint16_t`               |
| `PPACK_TYPE_INT16`  | 16-bit signed                        | `int16_t`                |
| `PPACK_TYPE_UINT32` | 32-bit unsigned                      | `uint32_t`               |
| `PPACK_TYPE_INT32`  | 32-bit signed                        | `int32_t`                |
| `PPACK_TYPE_F32`    | 32-bit float (raw IEEE-754 bit copy) | `float`                  |
| `PPACK_TYPE_BITS`   | Raw bitfield, up to 32 bits          | `uint32_t`               |

For `PPACK_BEHAVIOUR_SCALED` fields the struct member must always be `float`, regardless of `type`.

### Error Codes

| Code                 | Value | Meaning                                                                                                                                                                                         |
| -------------------- | ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `PPACK_SUCCESS`      | 0     | Operation succeeded                                                                                                                                                                             |
| `PPACK_ERR_INVALARG` | 1     | Invalid argument (NULL fields, zero field count, `bit_length` out of range, `payload_bits` zero or not a multiple of `PPACK_ADDR_UNIT_BITS` or > `PPACK_MAX_PAYLOAD_BITS`, scaling requested for `PPACK_TYPE_UINT8`) |
| `PPACK_ERR_NOTFOUND` | 3     | Unknown field type                                                                                                                                                                              |
| `PPACK_ERR_NULLPTR`  | 4     | NULL pointer passed for `base_ptr` or `payload`                                                                                                                                                 |
| `PPACK_ERR_OVERFLOW` | 5     | `start_bit + bit_length > payload_bits`, or `scale == 0` on a `SCALED` field                                                                                                                    |

Functions return the negated error code on failure (e.g. `-PPACK_ERR_NULLPTR`). Code `2` is reserved.

## Wire Format

The payload contains `payload_bits` bits, a multiple of 8 between 8 and 65,528.
Its `payload_bits / 8` logical bytes are numbered from 0 to
`payload_bits / 8 - 1`.

- **Bit numbering**: payload bit `N` lives in logical byte `N / 8`, at position `N mod 8` within that byte.
- **Within a byte**: bit 0 is the least significant bit.
- **Multi-byte fields**: little-endian (Intel ordering, equivalent to DBC `byte_order=1`). A field at `start_bit=0`, `bit_length=16` with value `0x1234` produces `0x34` in byte 0 and `0x12` in byte 1.
- **Cross-platform**: the format is identical on byte-addressable and 16-bit-MAU hosts. Two nodes using ppack interoperate regardless of their addressable-unit size, provided they agree on `payload_bits`.

`PPACK_TYPE_F32` is a raw 32-bit IEEE-754 bit copy. The wire bytes are the host's `uint32_t` byte order. This is interoperable between any two little-endian hosts (e.g. x86_64, ARM Cortex-M, AArch64 in default mode, 16-bit MAU platforms) but NOT between a little-endian and a big-endian host without explicit byte swapping in user code.

## Scaled Fields

When `behaviour` is set to `PPACK_BEHAVIOUR_SCALED`, the library applies a linear transformation:

```
raw      = (physical - offset) / scale    /* pack:   float -> integer */
physical = (float)raw * scale + offset    /* unpack: integer -> float */
```

This encodes floating-point physical values into compact integer fields, e.g. voltage in 0.01 V/LSB or temperature with -40 °C offset and 0.25 °C resolution.

### Saturation

Pack silently clamps the integer value to the destination type's representable range. For example, `PPACK_TYPE_UINT16` clamps to `0..65535` and `PPACK_TYPE_INT16` clamps to `-32768..32767`. Out-of-range physical inputs do NOT produce an error.

For `PPACK_TYPE_UINT32` and `PPACK_TYPE_INT32` scaled fields, the float clamp uses `4294967040` (largest float exactly representable below `UINT32_MAX`) and `2147483520` (largest float below `INT32_MAX`). The true type maxima `UINT32_MAX` and `INT32_MAX` round up to `2^32` / `2^31` as 32-bit floats and would overflow the destination cast.

For safety-critical applications where an out-of-range physical value MUST be detected, validate the input at the call site before calling `ppack_pack`.

### Non-finite values

Pack rejects a scaled field whose scaled result is NaN (a NaN source value, or a NaN produced by the scale/offset arithmetic) with `-PPACK_ERR_INVALARG`; NaN has no defined integer representation. Positive and negative infinity are treated like any other out-of-range value and saturate to the clamp bounds above. `PPACK_TYPE_F32` fields are a raw bit-copy and carry NaN/infinity payloads unchanged.

### Quantization

The float-to-integer cast in pack truncates toward zero (C standard cast semantics). The maximum round-trip error is one LSB, equal to `scale`. For round-to-nearest behaviour, pre-add `0.5f * scale * sign(physical)` at the call site before pack.

## Platform Support

### Supported architectures

ppack works on any toolchain meeting the requirements below. There are no chip-specific code paths; the library auto-detects the addressing model from the standard library headers.

| Requirement                         | Notes                                                                                                        |
| ----------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| C11 toolchain                       | Uses `_Static_assert`. C99 with a non-standard equivalent will also build but is not exercised by CI.        |
| `CHAR_BIT == 8` or `CHAR_BIT == 16` | Detected from `<limits.h>`. Other values are rejected at compile time by `_Static_assert`.                   |
| Two's-complement signed integers    | Required for the documented sign-extension and unsigned-to-signed cast behaviour. Universal on real targets. |
| IEEE-754 binary32 `float`           | Required for `PPACK_TYPE_F32` and scaled fields. Universal on real targets.                                  |

Targets meeting these requirements are expected to work, including (but not limited to) x86_64, AArch64, ARMv7-M, ARMv8-M, RISC-V, AVR, and 16-bit MAU platforms.

### Validated configurations

These configurations are run through the test suite or have been verified on hardware:

| Toolchain                                    | Target               | Status                                                            |
| -------------------------------------------- | -------------------- | ----------------------------------------------------------------- |
| GCC, Clang                                   | x86_64 Linux         | Run in CI (8-bit MAU code path)                                   |
| GCC, Clang with `-DPPACK_SIMULATE_16BIT_MAU` | x86_64 Linux         | Run in CI (16-bit MAU code path)                                  |
| Native Toolchains                            | 16-bit MAU platforms | Code path covered by host simulation; no live cross-compile in CI |

Other architectures from the supported list above are expected to work but are not yet routinely validated. If you bring up ppack on a new target, please report back so the table can be extended.

### 16-bit-MAU specifics

On targets where `CHAR_BIT == 16` (16-bit MAU platforms) the narrowest integer type is 16 bits, so any `uint8_t` the toolchain provides is backed by 16-bit storage. ppack detects this from `CHAR_BIT`/`UCHAR_MAX` (not from `uint8_t` itself) and handles it transparently via `ppack_platform.h`. Two contract points apply:

| Point             | Rule                                                                                                                                                                                                                                                                                                                  |
| ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **UINT8 storage** | A `PPACK_TYPE_UINT8` field reads and writes only the LOW 8 bits of the struct member's storage. The underlying 16-bit storage can technically hold 0..65535 on a 16-bit-MAU target, but only the low 8 bits round-trip through the wire. Do not store values >= 256 in UINT8 fields if you expect them to round-trip. |
| **`ptr_offset`**  | Always use `offsetof()`. The library treats `ptr_offset` as a `char`-unit offset, which is 16 bits on a 16-bit-MAU target. Manually computing offsets in 8-bit units will fail.                                                                                                                                       |

### Forcing the simulation flag

For host CI builds that need to exercise the 16-bit MAU code path without a cross-compile, define `PPACK_SIMULATE_16BIT_MAU` at compile time. Production builds for real targets should leave it undefined and rely on auto-detection.

## Input Validation

All public APIs validate arguments at the function boundary.

- Passing `NULL` for any pointer returns `-PPACK_ERR_NULLPTR`.
- Passing `NULL` or an empty `fields` array returns `-PPACK_ERR_INVALARG`.
- A `payload_bits` argument that is zero, not a multiple of 8, or greater than `PPACK_MAX_PAYLOAD_BITS` returns `-PPACK_ERR_INVALARG`.
- A field with `bit_length == 0` or `bit_length > 32` returns `-PPACK_ERR_INVALARG`.
- A field where `start_bit + bit_length > payload_bits` returns `-PPACK_ERR_OVERFLOW`.
- A `PPACK_BEHAVIOUR_SCALED` field with `scale == 0.0` returns `-PPACK_ERR_OVERFLOW`.
- A `PPACK_BEHAVIOUR_SCALED` request on a `PPACK_TYPE_UINT8` field returns `-PPACK_ERR_INVALARG`.
- An unrecognised field type returns `-PPACK_ERR_NOTFOUND`.

`bit_length` is range-checked against the absolute field limit (1..32) and against the runtime payload limit (`bit_length <= payload_bits - start_bit`). It is NOT cross-checked against the natural width of the field's `type`. Declaring a `PPACK_TYPE_UINT16` with `bit_length=24` is accepted by the library; only the low 16 bits are meaningful, with the upper bits zero or sign-extended depending on type. Match `bit_length` to the type's natural width unless you have a specific reason not to.

## Use Cases

1. **Protocol Encoding** - Compact data for CAN, I2C, or custom binary protocols
2. **Register Access** - Safe manipulation of peripheral register layouts
3. **Telemetry Frames** - Serialise sensor data into fixed-size telemetry packets
4. **Cross-platform IPC** - Deterministic binary serialisation between heterogeneous targets

## Limitations

ppack is a serialisation primitive only. The following are explicitly out of scope:

- **No integrity checks**: no CRC, checksum, or framing. Caller (or the transport, e.g. the CAN frame CRC) is responsible for detecting bit errors.
- **No schema versioning**: the field-descriptor layout is the schema. Maintain it in shared headers across nodes.
- **Payload size capped at 65,528 bits**: the bound retains 16-bit bit indices and supports targets with 16-bit `size_t`.
- **No multi-buffer / streaming API**: a single payload is processed in one call.
- **No runtime endianness adaptation**: the wire format is little-endian. Big-endian hosts would need explicit byte swapping at the boundary (no such host is currently a target).

## Notes

| Topic                   | Note                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| ----------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Memory**              | All operations use stack memory; no dynamic allocation                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| **MISRA C:2023**        | Audited with `misch` (cppcheck-backed MISRA C:2023 analysis; configuration in `misra.toml`); the audit reports zero findings. Deviations are justified at point of use with `cppcheck-suppress ... @deviation` comments, or project-wide in `misra-deviations.txt`. The only required-rule deviations are three Rule 21.15 sites: the type-erased struct-member copies centralised in two internal helpers, and the deliberate F32 `float`/`uint32_t` pun. Design follows MISRA principles throughout: no dynamic allocation, no UB shifts, explicit error codes, `memcpy`-based type punning. |
| **Payload size**        | Caller-supplied via `payload_bits`, a multiple of 8 between 8 and 65,528. The default buffer size remains 64 bits.                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| **Bit ordering**        | LSB-first within each byte; multi-byte fields little-endian; fields may span byte boundaries                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| **Field size**          | 1-32 bits per field                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| **Thread safety**       | Not thread-safe; caller must provide mutual exclusion when `base_ptr` or `payload` is shared across threads or ISRs                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| **WCET**                | Execution time is bounded and deterministic **when field descriptors are `static const`** (loop bounds are compile-time constants). WCET is not guaranteed if descriptors are constructed at runtime with arbitrary `bit_length` values.                                                                                                                                                                                                                                                                                                                                                       |
| **F32 and scaling**     | `PPACK_TYPE_F32` always performs a raw 32-bit IEEE 754 bit-copy; `scale`, `offset`, and `behaviour` are ignored for this type. Use a scaled integer type (`UINT16`, `INT16`, `UINT32`, `INT32`) to encode floating-point physical values with a resolution factor.                                                                                                                                                                                                                                                                                                                             |
| **UINT8 struct member** | `PPACK_TYPE_UINT8` reads and writes a `uint8_t`. On 16-bit MAU platforms, where a `uint8_t` member is backed by 16-bit storage, only the low 8 bits round-trip.                                                                                                                                                                                                                                                                                                                                                                                                                                |
| **Version header**      | `ppack_version.h` is auto-generated by the Meson build and placed in the output build folder                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
