test_sources = [
  'test_ppack_main.c',
  'test_harness.c',
  'test_fuzz.c',
  'test_null_args.c',
  'test_validation.c',
  'test_scale_zero.c',
  'test_scaled_nonfinite.c',
  'test_layout.c',
  'test_roundtrip_raw.c',
  'test_sign_extension.c',
  'test_scaled.c',
  'test_uint8_shim.c',
  'test_wire_lockdown.c',
  'test_edge.c',
  'test_exhaustive.c',
  'test_patterns.c',
  'test_fuzz_cases.c',
  'test_partial_coverage.c',
  'test_payload_size.c',
  'test_mau_layout.c',
  'test_large_payload.c',
]

test_exe = executable(
  'test_ppack',
  test_sources,
  dependencies: [ppack_dep],
  include_directories: public_headers,
)

# The geometry sweep covers over two million start/width pairs per model.
test('ppack 8-bit native', test_exe, timeout: 120)

test_16bit_exe = executable(
  'test_ppack_16bit',
  test_sources,
  dependencies: [ppack_sim_dep],
  include_directories: public_headers,
)

test('ppack 16-bit simulated', test_16bit_exe, timeout: 120)
