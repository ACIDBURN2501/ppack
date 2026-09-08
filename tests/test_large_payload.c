/*
 * @file: test_large_payload.c
 * @brief Large-payload checks against an independent bit-at-a-time model.
 *
 * The reference code uses logical octets directly. It must not call the
 * codec's index, mask, read or write helpers. Pack and unpack are checked
 * separately so matching codec errors cannot hide behind a round trip.
 */

#include "ppack.h"
#include "test_fuzz.h"
#include "test_harness.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MAX_UNITS (PPACK_MAX_PAYLOAD_BITS / 8u)
#define GUARD     ((ppack_byte_t)0xA5u)

static void
reference_write(ppack_byte_t *wire, uint32_t start, uint16_t width,
                uint32_t value)
{
        for (uint32_t bit = 0; bit < width; ++bit) {
                uint32_t position = start + bit;
                ppack_byte_t mask = (ppack_byte_t)(1u << (position % 8u));
                if (((value >> bit) & 1u) != 0u) {
                        wire[position / 8u] |= mask;
                } else {
                        wire[position / 8u] &= (ppack_byte_t)~mask;
                }
        }
}

static uint32_t
reference_read(const ppack_byte_t *wire, uint32_t start, uint16_t width)
{
        uint32_t value = 0;
        for (uint32_t bit = 0; bit < width; ++bit) {
                uint32_t position = start + bit;
                if ((wire[position / 8u] & (1u << (position % 8u))) != 0u) {
                        value |= UINT32_C(1) << bit;
                }
        }
        return value;
}

static void
check_raw(size_t bits, uint16_t start, uint16_t width, uint32_t value)
{
        /* Static host-test storage avoids large per-call stack frames. */
        static ppack_byte_t guarded[MAX_UNITS + 2u];
        static ppack_byte_t expected[MAX_UNITS];
        size_t units = bits / 8u;
        size_t bytes = units * sizeof(ppack_byte_t);
        ppack_byte_t *payload = &guarded[1];
        struct ppack_field field = {
            .type = PPACK_TYPE_BITS,
            .start_bit = start,
            .bit_length = width,
            .ptr_offset = 0,
            .behaviour = PPACK_BEHAVIOUR_RAW,
        };

        memset(payload, 0xFF, bytes);
        guarded[0] = GUARD;
        guarded[units + 1u] = GUARD;
        memset(expected, 0, bytes);
        reference_write(expected, start, width, value);
        TEST_ASSERT(ppack_pack(&value, payload, bits, &field, 1)
                    == PPACK_SUCCESS);
        TEST_ASSERT(memcmp(payload, expected, bytes) == 0);
        TEST_ASSERT(guarded[0] == GUARD);
        TEST_ASSERT(guarded[units + 1u] == GUARD);

        /* Decode reference bytes with nonzero gaps, not the packed result.
         * The high storage bits also start nonzero in the MAU simulation. */
        memset(expected, 0xFF, bytes);
        reference_write(expected, start, width, value);
        memcpy(payload, expected, bytes);
        uint32_t out[3] = {UINT32_C(0x12345678), 0, UINT32_C(0x87654321)};
        TEST_ASSERT(ppack_unpack(&out[1], payload, bits, &field, 1)
                    == PPACK_SUCCESS);
        TEST_ASSERT(out[1] == reference_read(expected, start, width));
        TEST_ASSERT(out[0] == UINT32_C(0x12345678));
        TEST_ASSERT(out[2] == UINT32_C(0x87654321));
        TEST_ASSERT(memcmp(payload, expected, bytes) == 0);
        TEST_ASSERT(guarded[0] == GUARD);
        TEST_ASSERT(guarded[units + 1u] == GUARD);
}

TEST_CASE(test_large_every_field_geometry)
{
        /* Every legal start/width pair, including all eight alignments.
         * Each uses the smallest legal payload that contains the field.
         * Wider loop counters cannot wrap at the uint16_t limit. */
        fuzz_seed(0xBEEF1001u);
        for (uint32_t start = 0; start < PPACK_MAX_PAYLOAD_BITS; ++start) {
                for (uint16_t width = 1; width <= 32u; ++width) {
                        uint32_t end = start + width;
                        if (end <= PPACK_MAX_PAYLOAD_BITS) {
                                size_t bits = (size_t)((end + 7u) & ~7u);
                                check_raw(bits, (uint16_t)start, width,
                                          fuzz_next());
                        }
                }
        }
}

TEST_CASE(test_large_basis_patterns)
{
        /* Values are independent of geometry in the codec. Exercise every
         * source bit at each alignment near old and new size boundaries. */
        static const uint16_t bases[] = {0, 504, 1016, 32760, 65488};
        for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
                for (uint16_t alignment = 0; alignment < 8u; ++alignment) {
                        uint16_t start = bases[i] + alignment;
                        for (uint16_t width = 1; width <= 32u; ++width) {
                                check_raw(PPACK_MAX_PAYLOAD_BITS, start, width,
                                          0);
                                check_raw(PPACK_MAX_PAYLOAD_BITS, start, width,
                                          UINT32_MAX);
                                check_raw(PPACK_MAX_PAYLOAD_BITS, start, width,
                                          UINT32_C(0xAAAAAAAA));
                                check_raw(PPACK_MAX_PAYLOAD_BITS, start, width,
                                          UINT32_C(0x55555555));
                                for (uint16_t bit = 0; bit < width; ++bit) {
                                        uint32_t value = UINT32_C(1) << bit;
                                        check_raw(PPACK_MAX_PAYLOAD_BITS, start,
                                                  width, value);
                                        check_raw(PPACK_MAX_PAYLOAD_BITS, start,
                                                  width, ~value);
                                }
                        }
                }
        }
}

TEST_CASE(test_large_each_payload_size)
{
        /* Test every legal size with an exact-end field. Adjacent guard
         * units detect too much clearing. Nonzero fill detects too little. */
        for (uint32_t bits = 8; bits <= PPACK_MAX_PAYLOAD_BITS; bits += 8u) {
                uint16_t width = (bits < 32u) ? (uint16_t)bits : 32u;
                uint16_t start = (uint16_t)(bits - width);
                check_raw((size_t)bits, start, width, UINT32_C(0xDEADBEEF));
        }
}

TEST_CASE(test_large_invalid_sizes_and_geometry)
{
        static ppack_byte_t payload[MAX_UNITS + 1u];
        uint32_t value = UINT32_C(0xDEADBEEF);
        struct ppack_field field = {
            .type = PPACK_TYPE_BITS,
            .start_bit = 0,
            .bit_length = 1,
            .ptr_offset = 0,
            .behaviour = PPACK_BEHAVIOUR_RAW,
        };
        const size_t invalid_sizes[] = {
            0,
            1,
            7,
            513,
            PPACK_MAX_PAYLOAD_BITS - 1u,
            (size_t)PPACK_MAX_PAYLOAD_BITS + 1u,
            (size_t)PPACK_MAX_PAYLOAD_BITS + 8u,
            SIZE_MAX,
        };
        for (size_t i = 0; i < sizeof(invalid_sizes) / sizeof(invalid_sizes[0]);
             ++i) {
                /* Tiny actual storage proves size rejection comes first. */
                ppack_byte_t tiny = GUARD;
                uint32_t out = value;
                TEST_ASSERT(
                    ppack_pack(&value, &tiny, invalid_sizes[i], &field, 1)
                    == -PPACK_ERR_INVALARG);
                TEST_ASSERT(tiny == GUARD);
                TEST_ASSERT(
                    ppack_unpack(&out, &tiny, invalid_sizes[i], &field, 1)
                    == -PPACK_ERR_INVALARG);
                TEST_ASSERT(out == value);
                TEST_ASSERT(tiny == GUARD);
        }

        const uint16_t invalid_widths[] = {0, 33, UINT16_MAX};
        for (size_t i = 0;
             i < sizeof(invalid_widths) / sizeof(invalid_widths[0]); ++i) {
                field.bit_length = invalid_widths[i];
                uint32_t out = value;
                TEST_ASSERT(ppack_pack(&value, payload, PPACK_MAX_PAYLOAD_BITS,
                                       &field, 1)
                            == -PPACK_ERR_INVALARG);
                TEST_ASSERT(ppack_unpack(&out, payload, PPACK_MAX_PAYLOAD_BITS,
                                         &field, 1)
                            == -PPACK_ERR_INVALARG);
                TEST_ASSERT(out == value);
        }

        const size_t sizes[] = {8, 64, 512, 520, 1024, PPACK_MAX_PAYLOAD_BITS};
        for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
                const uint16_t starts[] = {
                    (uint16_t)(sizes[i] - 1u),
                    (uint16_t)sizes[i],
                    (uint16_t)(sizes[i] + 1u),
                    UINT16_MAX - 15u,
                    UINT16_MAX,
                };
                for (size_t j = 0; j < sizeof(starts) / sizeof(starts[0]);
                     ++j) {
                        field.start_bit = starts[j];
                        field.bit_length = 32;
                        uint32_t out = value;
                        payload[sizes[i] / 8u] = GUARD;
                        TEST_ASSERT(
                            ppack_pack(&value, payload, sizes[i], &field, 1)
                            == -PPACK_ERR_OVERFLOW);
                        TEST_ASSERT(
                            ppack_unpack(&out, payload, sizes[i], &field, 1)
                            == -PPACK_ERR_OVERFLOW);
                        TEST_ASSERT(out == value);
                        TEST_ASSERT(payload[sizes[i] / 8u] == GUARD);
                }
        }
}

TEST_CASE(test_large_mixed_types)
{
        struct sample {
                ppack_u8_t u8;
                uint16_t u16;
                int16_t s16;
                uint32_t u32;
                int32_t s32;
                float f32;
                uint32_t bits;
                float scaled_u16;
                float scaled_s16;
                float scaled_u32;
                float scaled_s32;
        };
        const struct sample source = {
            .u8 = 0x5A,
            .u16 = 0x1234,
            .s16 = -1234,
            .u32 = UINT32_C(0x12345678),
            .s32 = -1234567,
            .f32 = 1.0f,
            .bits = UINT32_C(0xDEADBEEF),
            .scaled_u16 = 42.5f,
            .scaled_s16 = -10.5f,
            .scaled_u32 = 42.5f,
            .scaled_s32 = -10.5f,
        };
        struct ppack_field fields[] = {
            {PPACK_TYPE_UINT8, 0, 7, offsetof(struct sample, u8), 0, 0,
             PPACK_BEHAVIOUR_RAW},
            {PPACK_TYPE_UINT16, 0, 13, offsetof(struct sample, u16), 0, 0,
             PPACK_BEHAVIOUR_RAW},
            {PPACK_TYPE_INT16, 0, 15, offsetof(struct sample, s16), 0, 0,
             PPACK_BEHAVIOUR_RAW},
            {PPACK_TYPE_UINT32, 0, 29, offsetof(struct sample, u32), 0, 0,
             PPACK_BEHAVIOUR_RAW},
            {PPACK_TYPE_INT32, 0, 31, offsetof(struct sample, s32), 0, 0,
             PPACK_BEHAVIOUR_RAW},
            {PPACK_TYPE_F32, 0, 32, offsetof(struct sample, f32), 0, 0,
             PPACK_BEHAVIOUR_RAW},
            {PPACK_TYPE_BITS, 0, 32, offsetof(struct sample, bits), 0, 0,
             PPACK_BEHAVIOUR_RAW},
            {PPACK_TYPE_UINT16, 0, 16, offsetof(struct sample, scaled_u16),
             0.5f, 1.0f, PPACK_BEHAVIOUR_SCALED},
            {PPACK_TYPE_INT16, 0, 16, offsetof(struct sample, scaled_s16), 0.5f,
             1.0f, PPACK_BEHAVIOUR_SCALED},
            {PPACK_TYPE_UINT32, 0, 32, offsetof(struct sample, scaled_u32),
             0.5f, 1.0f, PPACK_BEHAVIOUR_SCALED},
            {PPACK_TYPE_INT32, 0, 32, offsetof(struct sample, scaled_s32), 0.5f,
             1.0f, PPACK_BEHAVIOUR_SCALED},
        };
        const uint32_t raw[] = {
            0x5A,
            0x1234,
            (uint32_t)-1234,
            UINT32_C(0x12345678),
            (uint32_t)-1234567,
            UINT32_C(0x3F800000),
            UINT32_C(0xDEADBEEF),
            83,
            (uint32_t)-23,
            83,
            (uint32_t)-23,
        };
        const uint16_t bases[] = {493, 1021, 65001};
        static ppack_byte_t payload[MAX_UNITS];
        static ppack_byte_t expected[MAX_UNITS];
        size_t count = sizeof(fields) / sizeof(fields[0]);
        for (size_t layout = 0; layout < sizeof(bases) / sizeof(bases[0]);
             ++layout) {
                uint16_t start = bases[layout];
                memset(expected, 0, sizeof(expected));
                for (size_t i = 0; i < count; ++i) {
                        fields[i].start_bit = start;
                        reference_write(expected, start, fields[i].bit_length,
                                        raw[i]);
                        start += fields[i].bit_length + 3u;
                }
                memset(payload, 0xFF, sizeof(payload));
                TEST_ASSERT(ppack_pack(&source, payload, PPACK_MAX_PAYLOAD_BITS,
                                       fields, count)
                            == PPACK_SUCCESS);
                TEST_ASSERT(memcmp(payload, expected, sizeof(payload)) == 0);

                struct sample out = {0};
                TEST_ASSERT(ppack_unpack(&out, expected, PPACK_MAX_PAYLOAD_BITS,
                                         fields, count)
                            == PPACK_SUCCESS);
                TEST_ASSERT(out.u8 == source.u8);
                TEST_ASSERT(out.u16 == source.u16);
                TEST_ASSERT(out.s16 == source.s16);
                TEST_ASSERT(out.u32 == source.u32);
                TEST_ASSERT(out.s32 == source.s32);
                TEST_ASSERT(out.f32 == source.f32);
                TEST_ASSERT(out.bits == source.bits);
                TEST_ASSERT(out.scaled_u16 == source.scaled_u16);
                TEST_ASSERT(out.scaled_s16 == source.scaled_s16);
                TEST_ASSERT(out.scaled_u32 == source.scaled_u32);
                TEST_ASSERT(out.scaled_s32 == source.scaled_s32);

                /* Disjoint fields are independent of descriptor order.
                 * sizeof the fixed array is a compile-time bound, not a VLA.
                 */
                struct ppack_field reversed[sizeof(fields) / sizeof(fields[0])];
                for (size_t i = 0; i < count; ++i) {
                        reversed[i] = fields[count - 1u - i];
                }
                TEST_ASSERT(ppack_pack(&source, payload, PPACK_MAX_PAYLOAD_BITS,
                                       reversed, count)
                            == PPACK_SUCCESS);
                TEST_ASSERT(memcmp(payload, expected, sizeof(payload)) == 0);
        }
}

TEST_CASE(test_large_preserves_legacy_prefix)
{
        const uint32_t source[] = {
            UINT32_C(0x12345678), UINT32_C(0x456789AB), UINT32_C(0xDEADBEEF),
            UINT32_C(0x89ABCDEF), UINT32_C(0x0ABCDEF0),
        };
        const struct ppack_field fields[] = {
            {PPACK_TYPE_BITS, 0, 32, 0, 0, 0, PPACK_BEHAVIOUR_RAW},
            {PPACK_TYPE_BITS, 57, 31, sizeof(uint32_t), 0, 0,
             PPACK_BEHAVIOUR_RAW},
            {PPACK_TYPE_BITS, 464, 32, 2u * sizeof(uint32_t), 0, 0,
             PPACK_BEHAVIOUR_RAW},
            {PPACK_TYPE_BITS, 496, 32, 3u * sizeof(uint32_t), 0, 0,
             PPACK_BEHAVIOUR_RAW},
            {PPACK_TYPE_BITS, 65500, 28, 4u * sizeof(uint32_t), 0, 0,
             PPACK_BEHAVIOUR_RAW},
        };
        ppack_byte_t legacy[496u / 8u];
        static ppack_byte_t extended[MAX_UNITS];
        uint32_t out[5] = {0};
        TEST_ASSERT(ppack_pack(source, legacy, 496, fields, 3)
                    == PPACK_SUCCESS);
        TEST_ASSERT(
            ppack_pack(source, extended, PPACK_MAX_PAYLOAD_BITS, fields, 5)
            == PPACK_SUCCESS);
        TEST_ASSERT(memcmp(legacy, extended, sizeof(legacy)) == 0);
        TEST_ASSERT(
            ppack_unpack(out, extended, PPACK_MAX_PAYLOAD_BITS, fields, 5)
            == PPACK_SUCCESS);
        TEST_ASSERT(memcmp(source, out, sizeof(source)) == 0);
}

void
run_large_payload_tests(void)
{
        run_test(test_large_every_field_geometry,
                 "test_large_every_field_geometry");
        run_test(test_large_basis_patterns, "test_large_basis_patterns");
        run_test(test_large_each_payload_size, "test_large_each_payload_size");
        run_test(test_large_invalid_sizes_and_geometry,
                 "test_large_invalid_sizes_and_geometry");
        run_test(test_large_mixed_types, "test_large_mixed_types");
        run_test(test_large_preserves_legacy_prefix,
                 "test_large_preserves_legacy_prefix");
}
