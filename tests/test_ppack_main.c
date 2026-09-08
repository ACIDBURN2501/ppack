/*
 * @file: test_ppack_main.c
 * @brief Unit-test driver for the ppack library.
 *
 * Each category lives in its own translation unit
 * (test_<category>.c) and exposes a single
 * void run_<category>_tests(void) entry point. This file is the
 * driver: it dispatches to each category in a fixed order and
 * prints the start/end banners.
 */

#include <ppack_platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Compile-time verification that ppack_byte_t resolves correctly.
 * On byte-addressable platforms ppack_byte_t == uint8_t (1 byte);
 * on word-addressable platforms ppack_byte_t == uint16_t (2 bytes).
 *
 * PPACK_PAYLOAD_UNITS is bounded by the library's own static asserts
 * in ppack_platform.h (positive multiple of 8, at most 65528 bits);
 * we deliberately do not duplicate that bound here so tests build
 * cleanly under any legal PPACK_PAYLOAD_BITS override.
 */
_Static_assert(sizeof(ppack_byte_t) == 1 || sizeof(ppack_byte_t) == 2,
               "ppack_byte_t must be 8 or 16 bits");

extern void run_null_arg_tests(void);
extern void run_validation_tests(void);
extern void run_scale_zero_tests(void);
extern void run_scaled_nonfinite_tests(void);
extern void run_roundtrip_raw_tests(void);
extern void run_sign_extension_tests(void);
extern void run_layout_tests(void);
extern void run_scaled_tests(void);
extern void run_edge_tests(void);
extern void run_exhaustive_tests(void);
extern void run_pattern_tests(void);
extern void run_uint8_shim_tests(void);
extern void run_fuzz_tests(void);
extern void run_wire_lockdown_tests(void);
extern void run_partial_coverage_tests(void);
extern void run_payload_size_tests(void);
extern void run_mau_layout_tests(void);
extern void run_large_payload_tests(void);

int
main(void)
{

        fprintf(stdout, "\n=== Running ppack unit tests ===\n\n");

        run_null_arg_tests();
        run_validation_tests();
        run_scale_zero_tests();
        run_scaled_nonfinite_tests();
        run_roundtrip_raw_tests();
        run_sign_extension_tests();
        run_layout_tests();
        run_scaled_tests();
        run_edge_tests();
        run_exhaustive_tests();
        run_pattern_tests();
        run_uint8_shim_tests();
        run_fuzz_tests();
        run_wire_lockdown_tests();
        run_partial_coverage_tests();
        run_payload_size_tests();
        run_mau_layout_tests();
        run_large_payload_tests();

        fprintf(stdout, "\n=== All tests passed ===\n\n");
        return EXIT_SUCCESS;
}
