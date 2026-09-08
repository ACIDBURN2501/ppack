# Large-payload verification

Version 2.5.0 raises the payload limit from 512 to 65,528 bits.
The change addresses [ppack #6](https://github.com/aajll/ppack/issues/6).
The consumer request is
[pebb-firmware #178](https://github.com/Next-Gen-MP/pebb-firmware/issues/178).

## Scope

The function signatures and `struct ppack_field` layout do not change.
The default buffer size remains 64 bits. Fields remain 1 to 32 bits wide.
The bit-copy loops, integer conversions, floating-point conversions, and
field ordering do not change.

The implementation uses one public maximum for runtime and compile-time
validation. Field validation checks the start before subtracting it from
the payload size. This prevents an overflowing start-plus-length sum on
targets with 16-bit `size_t`.

Valid sizes above 512 bits now reach buffer access. This intentionally changes
the old oversized-input rejection behavior. Callers must allocate sufficient
storage and enforce their own transport limits.

## Range argument

These bounds apply after successful payload and field validation:

1. The payload size is a multiple of eight in the range 8 to 65,528.
2. The field width is in the range 1 to 32.
3. The start is no greater than the payload size.
4. The field width is no greater than the payload size minus the start.

The subtraction cannot underflow because condition 3 is checked first.
It remains valid when `size_t` has only 16 bits.

Together, the conditions imply that every accessed bit is between 0 and
65,527. The `uint16_t` bit-index additions cannot wrap for accepted fields.
The largest logical-octet index is 8,190.

Each loop iteration processes between one and eight bits. A 32-bit field
requires at most five iterations. Value shifts remain below 32, regardless
of the absolute field position. The larger payload does not widen a field
or change its packed representation.

Packing clears at most 8,191 payload storage units. On a real 16-bit-MAU
target, each logical octet occupies one 16-bit C addressable unit.
The host simulation uses two 8-bit C addressable units per logical octet.
Even that clearing count fits within a 16-bit `size_t`.

This is a code-level range argument, not a machine-checked formal proof.
It assumes valid caller-owned buffers, accessible descriptors, and correct
structure offsets and member types.

## Persistent tests

`tests/test_large_payload.c` contains an independent bit-at-a-time model.
It does not use ppack's bit-index or bit-copy helpers. Pack results are
compared with reference bytes. Unpack consumes separately constructed
reference bytes, including nonzero bits outside the selected field.

Each native or simulated-storage run checks:

- All 2,096,400 legal start-position and field-width pairs, with deterministic
  pseudorandom values and the smallest payload that contains each field.
- All 8,191 legal payload sizes, with a field ending at the payload boundary.
- Every source-bit position, zero, ones, and alternating patterns at every
  alignment near the old limit, high offsets, and the new limit.
- Every supported raw and scaled type in large, unaligned multi-field layouts.
- Buffer guards, complete clearing from nonzero contents, unpack input
  preservation, and untouched destination neighbors.
- Invalid sizes and widths, exact-end overflow, starts beyond the payload,
  and malformed descriptors near `UINT16_MAX`.
- Preservation of a 496-bit prefix when fields are appended beyond bit 512.

The existing golden-byte fixtures remain unchanged. Meson also checks valid
and invalid `PPACK_PAYLOAD_BITS` definitions during test configuration.
CI runs the default buffer size and overrides of 512 and 65,528 bits.

### Legacy comparison

`tests/compatibility` builds the current and unmodified 2.4.0 implementations
into one test process under separate function names. It compares exact pack
bytes and unpacked member representations from independent input payloads.

The test covers every legacy payload size, legal start and width, seven raw
types, and four scaled types. Each storage model checks 5,512,760 cases.
Values use a fixed pseudorandom seed. This is not exhaustive over all values
or multi-field descriptor combinations.

CI pins the old source to `fecf8778423c353a54b7387aa0954887a75fbe49`.
It runs both storage models on Linux and macOS with sanitizers.
To reproduce from the repository root:

```sh
legacy_dir=$(mktemp -d)
git archive fecf8778423c353a54b7387aa0954887a75fbe49 include src |
  tar -x -C "$legacy_dir"
meson setup build_compat tests/compatibility \
  --buildtype=debug "-Dlegacy_root=$legacy_dir" \
  -Db_sanitize=address,undefined \
  -Dc_args=-fsanitize=float-cast-overflow \
  -Dc_link_args=-fsanitize=float-cast-overflow
meson test -C build_compat --verbose
```

Keep the extracted legacy source until the build directory is no longer
needed. A shallow checkout must fetch the pinned commit first.

## Local verification record

The following checks passed during development on 2026-09-08:

| Check | Result |
| --- | --- |
| GCC 14.2, ASan, UBSan, float-cast-overflow | Native and simulated-storage suites pass |
| Maximum default-buffer override | Both storage models pass at 65,528 bits |
| Exact comparison with 2.4.0 | 5,512,760 cases pass per storage model |
| GCC coverage | 229/229 lines and 108/108 branches |
| `misch run`, version 0.4.0 | Zero findings, no new deviations |
| `misch deviations` | All deviations justified and valid |
| TI C2000 22.6.2.LTS and 25.11.1.LTS | Release library compilation passes with default and maximum buffer definitions |
| GCC release library | Builds successfully |

The TI builds used C11, C28x large/unified memory, EABI, and FPU32.
They did not enable relaxed floating-point mode. The cross-file settings were:

```ini
[binaries]
c = '/path/to/ti-cgt-c2000/bin/cl2000'
ar = '/path/to/ti-cgt-c2000/bin/ar2000'

[host_machine]
system = 'none'
cpu_family = 'c2000'
cpu = 'c28x'
endian = 'little'

[properties]
needs_exe_wrapper = true

[built-in options]
c_args = ['--c11', '-v28', '-ml', '-mt', '--float_support=fpu32',
          '--abi=eabi', '--diag_warning=225', '--display_error_number',
          '--diag_wrap=off', '-I/path/to/ti-cgt-c2000/include']
```

Use absolute toolchain paths. Add `-DPPACK_PAYLOAD_BITS=65528` to `c_args`
for the maximum-buffer check. Build each configuration with Meson:

```sh
meson setup build_target --cross-file /path/to/cross.ini \
  --buildtype=release -Dbuild_tests=false
meson compile -C build_target
```

No target hardware or instruction-set simulator was available for execution.
Target compilation does not establish target runtime correctness. Host MAU
simulation does not reproduce target integer promotions or `size_t` width.
The range argument covers the intended 16-bit arithmetic constraints, but
consumer testing on the actual target remains necessary.

## Consumer obligations

The codec cannot validate the allocation size behind a pointer. The caller
must supply at least `payload_bits / 8` elements of `ppack_byte_t` and valid
structure offsets. Increasing the runtime size does not resize a buffer.

Pack and unpack can leave partial output after an error. A record manager
must decode into temporary storage and publish only after success. It must
also retain whole-record CRC, redundant-copy selection, and atomic commits.
New stored fields still need the consumer's format-version and recovery
policy. Extending ppack alone does not verify those firmware behaviors.
