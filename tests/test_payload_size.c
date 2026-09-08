/*
 * @file: test_payload_size.c
 * @brief Variable-size payload validation and round-trip coverage.
 *
 * Exercises the payload_bits argument added in v2.2.0 (CAN-FD support):
 * argument validation, payload-size-relative overflow detection, and
 * round-trips at non-default payload sizes (128 / 256 / 512 bits).
 */

#include "ppack.h"
#include "test_fixtures.h"
#include "test_harness.h"
#include <ppack_platform.h>
#include <stddef.h>
#include <stdint.h>

/* ---------------- payload_bits validation -------------------------------- */

TEST_CASE(test_pack_payload_bits_zero_rejected)
{
        ppack_byte_t payload[PPACK_PAYLOAD_UNITS] = {0};
        test_struct_t data = {.field_uint16 = 0x1234};

        const struct ppack_field fields[] = {
            {.type = PPACK_TYPE_UINT16,
             .start_bit = 0,
             .bit_length = 16,
             .ptr_offset = offsetof(test_struct_t, field_uint16),
             .behaviour = PPACK_BEHAVIOUR_RAW},
        };

        TEST_ASSERT(ppack_pack(&data, payload, 0, fields, 1)
                    == -PPACK_ERR_INVALARG);
}

TEST_CASE(test_unpack_payload_bits_zero_rejected)
{
        ppack_byte_t payload[PPACK_PAYLOAD_UNITS] = {0};
        test_struct_t data = {0};

        const struct ppack_field fields[] = {
            {.type = PPACK_TYPE_UINT16,
             .start_bit = 0,
             .bit_length = 16,
             .ptr_offset = offsetof(test_struct_t, field_uint16),
             .behaviour = PPACK_BEHAVIOUR_RAW},
        };

        TEST_ASSERT(ppack_unpack(&data, payload, 0, fields, 1)
                    == -PPACK_ERR_INVALARG);
}

TEST_CASE(test_pack_payload_bits_not_multiple_rejected)
{
        ppack_byte_t payload[PPACK_PAYLOAD_UNITS] = {0};
        test_struct_t data = {.field_uint16 = 0x1234};

        const struct ppack_field fields[] = {
            {.type = PPACK_TYPE_UINT16,
             .start_bit = 0,
             .bit_length = 16,
             .ptr_offset = offsetof(test_struct_t, field_uint16),
             .behaviour = PPACK_BEHAVIOUR_RAW},
        };

        /* Non-multiple of 8 (and therefore of 16): rejected on every MAU. */
        TEST_ASSERT(ppack_pack(&data, payload, 7, fields, 1)
                    == -PPACK_ERR_INVALARG);
        TEST_ASSERT(ppack_pack(&data, payload, 33, fields, 1)
                    == -PPACK_ERR_INVALARG);

        /* On 16-bit MAU builds, a multiple-of-8 value that is not a
         * multiple of 16 must also be rejected (e.g. 8, 24). */
        if (PPACK_ADDR_UNIT_BITS == 16u) {
                TEST_ASSERT(ppack_pack(&data, payload, 8, fields, 1)
                            == -PPACK_ERR_INVALARG);
                TEST_ASSERT(ppack_pack(&data, payload, 24, fields, 1)
                            == -PPACK_ERR_INVALARG);
        }
}

TEST_CASE(test_pack_payload_bits_above_ceiling_rejected)
{
        ppack_byte_t payload[PPACK_PAYLOAD_UNITS] = {0};
        test_struct_t data = {.field_uint16 = 0x1234};

        const struct ppack_field fields[] = {
            {.type = PPACK_TYPE_UINT16,
             .start_bit = 0,
             .bit_length = 16,
             .ptr_offset = offsetof(test_struct_t, field_uint16),
             .behaviour = PPACK_BEHAVIOUR_RAW},
        };

        /* Oversized payloads must fail before touching this small buffer. */
        TEST_ASSERT(ppack_pack(&data, payload,
                               (size_t)PPACK_MAX_PAYLOAD_BITS + 8u, fields, 1)
                    == -PPACK_ERR_INVALARG);
        TEST_ASSERT(ppack_pack(&data, payload, SIZE_MAX, fields, 1)
                    == -PPACK_ERR_INVALARG);
}

TEST_CASE(test_pack_canfd_payload_passes)
{
        /* 512-bit payload with a uint32 field at the very top. */
        ppack_byte_t payload[512u / PPACK_ADDR_UNIT_BITS] = {0};
        test_struct_t data = {.field_uint32 = 0xDEADBEEFu};

        const struct ppack_field fields[] = {
            {.type = PPACK_TYPE_UINT32,
             .start_bit = 480,
             .bit_length = 32,
             .ptr_offset = offsetof(test_struct_t, field_uint32),
             .behaviour = PPACK_BEHAVIOUR_RAW},
        };

        TEST_ASSERT(ppack_pack(&data, payload, 512, fields, 1)
                    == PPACK_SUCCESS);
        TEST_ASSERT(READ_LOGICAL_BYTE(payload, 60) == 0xEFu);
        TEST_ASSERT(READ_LOGICAL_BYTE(payload, 61) == 0xBEu);
        TEST_ASSERT(READ_LOGICAL_BYTE(payload, 62) == 0xADu);
        TEST_ASSERT(READ_LOGICAL_BYTE(payload, 63) == 0xDEu);

        test_struct_t out = {0};
        TEST_ASSERT(ppack_unpack(&out, payload, 512, fields, 1)
                    == PPACK_SUCCESS);
        TEST_ASSERT(out.field_uint32 == 0xDEADBEEFu);
}

/* ---------------- payload_bits-aware overflow ---------------------------- */

TEST_CASE(test_pack_overflow_against_payload_bits)
{
        /* 128-bit payload, field at start=120, bit_length=16: 120+16=136
         * exceeds 128 and must be rejected — proves validate_field uses
         * the runtime-supplied size, not the historical 64. */
        ppack_byte_t payload[128u / PPACK_ADDR_UNIT_BITS] = {0};
        test_struct_t data = {.field_uint16 = 0x1234};

        const struct ppack_field fields[] = {
            {.type = PPACK_TYPE_UINT16,
             .start_bit = 120,
             .bit_length = 16,
             .ptr_offset = offsetof(test_struct_t, field_uint16),
             .behaviour = PPACK_BEHAVIOUR_RAW},
        };

        TEST_ASSERT(ppack_pack(&data, payload, 128, fields, 1)
                    == -PPACK_ERR_OVERFLOW);
}

TEST_CASE(test_pack_field_above_64_bits_with_64_payload_rejected)
{
        /* The same field that would succeed at payload_bits=128 must
         * fail at payload_bits=64 — guards against any regression that
         * silently widens validation. */
        ppack_byte_t payload[128u / PPACK_ADDR_UNIT_BITS] = {0};
        test_struct_t data = {.field_uint16 = 0x1234};

        const struct ppack_field fields[] = {
            {.type = PPACK_TYPE_UINT16,
             .start_bit = 64,
             .bit_length = 16,
             .ptr_offset = offsetof(test_struct_t, field_uint16),
             .behaviour = PPACK_BEHAVIOUR_RAW},
        };

        TEST_ASSERT(ppack_pack(&data, payload, 64, fields, 1)
                    == -PPACK_ERR_OVERFLOW);
        TEST_ASSERT(ppack_pack(&data, payload, 128, fields, 1)
                    == PPACK_SUCCESS);
}

/* ---------------- round-trips at non-default sizes ----------------------- */

TEST_CASE(test_roundtrip_128bit_payload)
{
        ppack_byte_t payload[128u / PPACK_ADDR_UNIT_BITS] = {0};
        test_struct_t src = {
            .field_uint16 = 0xCAFEu,
            .field_int16 = -12345,
            .field_uint32 = 0x89ABCDEFu,
            .field_int32 = -987654321,
        };

        const struct ppack_field fields[] = {
            {.type = PPACK_TYPE_UINT16,
             .start_bit = 0,
             .bit_length = 16,
             .ptr_offset = offsetof(test_struct_t, field_uint16),
             .behaviour = PPACK_BEHAVIOUR_RAW},
            {.type = PPACK_TYPE_INT16,
             .start_bit = 16,
             .bit_length = 16,
             .ptr_offset = offsetof(test_struct_t, field_int16),
             .behaviour = PPACK_BEHAVIOUR_RAW},
            {.type = PPACK_TYPE_UINT32,
             .start_bit = 64,
             .bit_length = 32,
             .ptr_offset = offsetof(test_struct_t, field_uint32),
             .behaviour = PPACK_BEHAVIOUR_RAW},
            {.type = PPACK_TYPE_INT32,
             .start_bit = 96,
             .bit_length = 32,
             .ptr_offset = offsetof(test_struct_t, field_int32),
             .behaviour = PPACK_BEHAVIOUR_RAW},
        };

        TEST_ASSERT(ppack_pack(&src, payload, 128, fields, 4) == PPACK_SUCCESS);

        test_struct_t dst = {0};
        TEST_ASSERT(ppack_unpack(&dst, payload, 128, fields, 4)
                    == PPACK_SUCCESS);

        TEST_ASSERT(dst.field_uint16 == src.field_uint16);
        TEST_ASSERT(dst.field_int16 == src.field_int16);
        TEST_ASSERT(dst.field_uint32 == src.field_uint32);
        TEST_ASSERT(dst.field_int32 == src.field_int32);
}

TEST_CASE(test_roundtrip_256bit_payload)
{
        ppack_byte_t payload[256u / PPACK_ADDR_UNIT_BITS] = {0};
        test_struct_t src = {.field_uint32 = 0x12345678u};

        /* Field placed in the upper half of a 256-bit payload to exercise
         * bit positions impossible with the legacy 64-bit API. */
        const struct ppack_field fields[] = {
            {.type = PPACK_TYPE_UINT32,
             .start_bit = 224,
             .bit_length = 32,
             .ptr_offset = offsetof(test_struct_t, field_uint32),
             .behaviour = PPACK_BEHAVIOUR_RAW},
        };

        TEST_ASSERT(ppack_pack(&src, payload, 256, fields, 1) == PPACK_SUCCESS);

        /* Lower 28 bytes must be untouched zero (memset wrote them). */
        for (uint16_t i = 0; i < 28; ++i) {
                TEST_ASSERT(READ_LOGICAL_BYTE(payload, i) == 0u);
        }

        test_struct_t dst = {0};
        TEST_ASSERT(ppack_unpack(&dst, payload, 256, fields, 1)
                    == PPACK_SUCCESS);
        TEST_ASSERT(dst.field_uint32 == src.field_uint32);
}

TEST_CASE(test_roundtrip_each_legacy_size)
{
        /* Sweep every legacy payload size and round-trip a small
         * field at the bottom. Catches regressions in the memset width
         * and the unit-count math. */
        const struct ppack_field fields[] = {
            {.type = PPACK_TYPE_UINT16,
             .start_bit = 0,
             .bit_length = 16,
             .ptr_offset = offsetof(test_struct_t, field_uint16),
             .behaviour = PPACK_BEHAVIOUR_RAW},
        };

        for (size_t bits = PPACK_ADDR_UNIT_BITS; bits <= 512u;
             bits += PPACK_ADDR_UNIT_BITS) {
                if (bits < 16u) {
                        continue; /* uint16 field needs at least 16 bits */
                }

                ppack_byte_t payload[512u / PPACK_ADDR_UNIT_BITS] = {0};
                test_struct_t src = {.field_uint16 =
                                         (uint16_t)(bits ^ 0xA5A5u)};
                test_struct_t dst = {0};

                TEST_ASSERT(ppack_pack(&src, payload, bits, fields, 1)
                            == PPACK_SUCCESS);
                TEST_ASSERT(ppack_unpack(&dst, payload, bits, fields, 1)
                            == PPACK_SUCCESS);
                TEST_ASSERT(dst.field_uint16 == src.field_uint16);
        }
}

/* ---------------- non-default-size wire-format lockdown ------------------ */

TEST_CASE(test_wire_lockdown_128bit)
{
        /* Layout (all little-endian):
         *   bits   0..31   PPACK_TYPE_UINT32 = 0x11223344
         *   bits  64..95   PPACK_TYPE_UINT32 = 0x55667788
         *   bits  96..127  PPACK_TYPE_UINT32 = 0x99AABBCC
         */
        test_struct_t data = {0};
        const struct ppack_field fields[] = {
            {.type = PPACK_TYPE_UINT32,
             .start_bit = 0,
             .bit_length = 32,
             .ptr_offset = offsetof(test_struct_t, field_uint32),
             .behaviour = PPACK_BEHAVIOUR_RAW},
            {.type = PPACK_TYPE_UINT32,
             .start_bit = 64,
             .bit_length = 32,
             .ptr_offset = offsetof(test_struct_t, field_bits),
             .behaviour = PPACK_BEHAVIOUR_RAW},
            {.type = PPACK_TYPE_UINT32,
             .start_bit = 96,
             .bit_length = 32,
             .ptr_offset = offsetof(test_struct_t, field_int32),
             .behaviour = PPACK_BEHAVIOUR_RAW},
        };

        data.field_uint32 = 0x11223344u;
        data.field_bits = 0x55667788u;
        data.field_int32 = (int32_t)0x99AABBCCu;

        ppack_byte_t payload[128u / PPACK_ADDR_UNIT_BITS] = {0};
        TEST_ASSERT(ppack_pack(&data, payload, 128, fields, 3)
                    == PPACK_SUCCESS);

        static const uint8_t expected[16] = {
            0x44u, 0x33u, 0x22u, 0x11u, 0x00u, 0x00u, 0x00u, 0x00u,
            0x88u, 0x77u, 0x66u, 0x55u, 0xCCu, 0xBBu, 0xAAu, 0x99u,
        };
        for (uint16_t i = 0; i < 16; ++i) {
                TEST_ASSERT(READ_LOGICAL_BYTE(payload, i) == expected[i]);
        }
}

TEST_CASE(test_wire_lockdown_256bit)
{
        /* Layout (all little-endian):
         *   bits   0..31   PPACK_TYPE_UINT32 = 0xAABBCCDD
         *   bits  64..95   PPACK_TYPE_INT32  = -1 (= 0xFFFFFFFF)
         *   bits 224..255  PPACK_TYPE_UINT32 = 0x12345678
         * All other bytes are zero (memset by ppack_pack).
         */
        test_struct_t data = {0};
        data.field_uint32 = 0xAABBCCDDu;
        data.field_int32 = -1;
        data.field_bits = 0x12345678u;

        const struct ppack_field fields[] = {
            {.type = PPACK_TYPE_UINT32,
             .start_bit = 0,
             .bit_length = 32,
             .ptr_offset = offsetof(test_struct_t, field_uint32),
             .behaviour = PPACK_BEHAVIOUR_RAW},
            {.type = PPACK_TYPE_INT32,
             .start_bit = 64,
             .bit_length = 32,
             .ptr_offset = offsetof(test_struct_t, field_int32),
             .behaviour = PPACK_BEHAVIOUR_RAW},
            {.type = PPACK_TYPE_UINT32,
             .start_bit = 224,
             .bit_length = 32,
             .ptr_offset = offsetof(test_struct_t, field_bits),
             .behaviour = PPACK_BEHAVIOUR_RAW},
        };

        ppack_byte_t payload[256u / PPACK_ADDR_UNIT_BITS] = {0};
        TEST_ASSERT(ppack_pack(&data, payload, 256, fields, 3)
                    == PPACK_SUCCESS);

        static const uint8_t expected[32] = {
            /* bytes  0..  3: UINT32 = 0xAABBCCDD */
            0xDDu,
            0xCCu,
            0xBBu,
            0xAAu,
            /* bytes  4..  7: gap */
            0x00u,
            0x00u,
            0x00u,
            0x00u,
            /* bytes  8.. 11: INT32 = -1 */
            0xFFu,
            0xFFu,
            0xFFu,
            0xFFu,
            /* bytes 12.. 27: gap */
            0x00u,
            0x00u,
            0x00u,
            0x00u,
            0x00u,
            0x00u,
            0x00u,
            0x00u,
            0x00u,
            0x00u,
            0x00u,
            0x00u,
            0x00u,
            0x00u,
            0x00u,
            0x00u,
            /* bytes 28.. 31: UINT32 = 0x12345678 */
            0x78u,
            0x56u,
            0x34u,
            0x12u,
        };
        for (uint16_t i = 0; i < 32; ++i) {
                TEST_ASSERT(READ_LOGICAL_BYTE(payload, i) == expected[i]);
        }
}

TEST_CASE(test_wire_lockdown_512bit)
{
        /* Layout (all little-endian) — full CAN-FD frame:
         *   bits   0.. 15  PPACK_TYPE_UINT16 = 0xBEEF (bottom of frame)
         *   bits 256..271  PPACK_TYPE_UINT16 = 0xCAFE (middle of frame)
         *   bits 496..511  PPACK_TYPE_UINT16 = 0xDEAD (top of frame)
         * All other bytes are zero.
         */
        test_struct_t data = {0};
        data.field_uint16 = 0xBEEFu;
        data.field_int16 = (int16_t)0xCAFEu;
        data.field_bits = 0xDEADu;

        const struct ppack_field fields[] = {
            {.type = PPACK_TYPE_UINT16,
             .start_bit = 0,
             .bit_length = 16,
             .ptr_offset = offsetof(test_struct_t, field_uint16),
             .behaviour = PPACK_BEHAVIOUR_RAW},
            {.type = PPACK_TYPE_UINT16,
             .start_bit = 256,
             .bit_length = 16,
             .ptr_offset = offsetof(test_struct_t, field_int16),
             .behaviour = PPACK_BEHAVIOUR_RAW},
            {.type = PPACK_TYPE_UINT16,
             .start_bit = 496,
             .bit_length = 16,
             .ptr_offset = offsetof(test_struct_t, field_bits),
             .behaviour = PPACK_BEHAVIOUR_RAW},
        };

        ppack_byte_t payload[512u / PPACK_ADDR_UNIT_BITS] = {0};
        TEST_ASSERT(ppack_pack(&data, payload, 512, fields, 3)
                    == PPACK_SUCCESS);

        for (uint16_t i = 0; i < 64; ++i) {
                uint8_t expected;
                switch (i) {
                case 0: expected = 0xEFu; break;
                case 1: expected = 0xBEu; break;
                case 32: expected = 0xFEu; break;
                case 33: expected = 0xCAu; break;
                case 62: expected = 0xADu; break;
                case 63: expected = 0xDEu; break;
                default: expected = 0x00u; break;
                }
                TEST_ASSERT(READ_LOGICAL_BYTE(payload, i) == expected);
        }
}

void
run_payload_size_tests(void)
{
        run_test(test_pack_payload_bits_zero_rejected,
                 "test_pack_payload_bits_zero_rejected");
        run_test(test_unpack_payload_bits_zero_rejected,
                 "test_unpack_payload_bits_zero_rejected");
        run_test(test_pack_payload_bits_not_multiple_rejected,
                 "test_pack_payload_bits_not_multiple_rejected");
        run_test(test_pack_payload_bits_above_ceiling_rejected,
                 "test_pack_payload_bits_above_ceiling_rejected");
        run_test(test_pack_canfd_payload_passes,
                 "test_pack_canfd_payload_passes");
        run_test(test_pack_overflow_against_payload_bits,
                 "test_pack_overflow_against_payload_bits");
        run_test(test_pack_field_above_64_bits_with_64_payload_rejected,
                 "test_pack_field_above_64_bits_with_64_payload_rejected");
        run_test(test_roundtrip_128bit_payload,
                 "test_roundtrip_128bit_payload");
        run_test(test_roundtrip_256bit_payload,
                 "test_roundtrip_256bit_payload");
        run_test(test_roundtrip_each_legacy_size,
                 "test_roundtrip_each_legacy_size");
        run_test(test_wire_lockdown_128bit, "test_wire_lockdown_128bit");
        run_test(test_wire_lockdown_256bit, "test_wire_lockdown_256bit");
        run_test(test_wire_lockdown_512bit, "test_wire_lockdown_512bit");
}
