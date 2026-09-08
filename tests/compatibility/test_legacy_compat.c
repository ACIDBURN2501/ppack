/*
 * @file: test_legacy_compat.c
 * @brief Compare valid legacy geometry against an unmodified old codec.
 *
 * This checks exact pack bytes and independently generated input to unpack.
 * Every legal legacy size, start and width is exercised for all seven raw
 * types and four scaled types. Values use a fixed xorshift32 seed.
 */

#include "ppack.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

int legacy_pack(const void *, void *, size_t, const struct ppack_field *,
                size_t);
int legacy_unpack(void *, const void *, size_t, const struct ppack_field *,
                  size_t);

union value {
        ppack_u8_t u8;
        uint16_t u16;
        int16_t s16;
        uint32_t u32;
        int32_t s32;
        float f32;
};

static uint32_t state = 0xBEEF2001u;
static uint32_t
next(void)
{
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
}

int
main(void)
{
        size_t cases = 0;
        for (size_t bits = 8; bits <= 512; bits += 8) {
                for (uint16_t width = 1; width <= 32 && width <= bits;
                     ++width) {
                        for (uint16_t start = 0; (size_t)start + width <= bits;
                             ++start) {
                                for (uint32_t mode = 0; mode < 11; ++mode) {
                                        struct ppack_field field = {
                                            .start_bit = start,
                                            .bit_length = width,
                                            .ptr_offset = 0,
                                            .scale = 0.5f,
                                            .offset = 1.0f,
                                            .behaviour = PPACK_BEHAVIOUR_RAW,
                                        };
                                        uint32_t r = next();
                                        union value src = {0};
                                        size_t member_size;
                                        switch (mode) {
                                        case 0:
                                                field.type = PPACK_TYPE_UINT8;
                                                src.u8 = (ppack_u8_t)r;
                                                member_size = sizeof(src.u8);
                                                break;
                                        case 1:
                                                field.type = PPACK_TYPE_UINT16;
                                                src.u16 = (uint16_t)r;
                                                member_size = sizeof(src.u16);
                                                break;
                                        case 2:
                                                field.type = PPACK_TYPE_INT16;
                                                src.s16 = (int16_t)r;
                                                member_size = sizeof(src.s16);
                                                break;
                                        case 3:
                                                field.type = PPACK_TYPE_UINT32;
                                                src.u32 = r;
                                                member_size = sizeof(src.u32);
                                                break;
                                        case 4:
                                                field.type = PPACK_TYPE_INT32;
                                                src.s32 = (int32_t)r;
                                                member_size = sizeof(src.s32);
                                                break;
                                        case 5:
                                                field.type = PPACK_TYPE_F32;
                                                memcpy(&src.f32, &r, sizeof(r));
                                                member_size = sizeof(src.f32);
                                                break;
                                        case 6:
                                                field.type = PPACK_TYPE_BITS;
                                                src.u32 = r;
                                                member_size = sizeof(src.u32);
                                                break;
                                        default:
                                                field.type =
                                                    (mode == 7)
                                                        ? PPACK_TYPE_UINT16
                                                    : (mode == 8)
                                                        ? PPACK_TYPE_INT16
                                                    : (mode == 9)
                                                        ? PPACK_TYPE_UINT32
                                                        : PPACK_TYPE_INT32;
                                                field.behaviour =
                                                    PPACK_BEHAVIOUR_SCALED;
                                                src.f32 =
                                                    (float)(int32_t)r / 32.0f;
                                                member_size = sizeof(src.f32);
                                                break;
                                        }
                                        ppack_byte_t old[64], current[64],
                                            input[64];
                                        TEST_ASSERT(legacy_pack(&src, old, bits,
                                                                &field, 1)
                                                    == 0);
                                        TEST_ASSERT(ppack_pack(&src, current,
                                                               bits, &field, 1)
                                                    == 0);
                                        TEST_ASSERT(
                                            memcmp(old, current,
                                                   bits / 8 * sizeof(old[0]))
                                            == 0);
                                        for (size_t i = 0; i < bits / 8; ++i) {
                                                input[i] = (ppack_byte_t)next();
                                        }
                                        union value a, b;
                                        memset(&a, 0xA5, sizeof(a));
                                        memset(&b, 0xA5, sizeof(b));
                                        TEST_ASSERT(legacy_unpack(&a, input,
                                                                  bits, &field,
                                                                  1)
                                                    == 0);
                                        TEST_ASSERT(ppack_unpack(&b, input,
                                                                 bits, &field,
                                                                 1)
                                                    == 0);
                                        TEST_ASSERT(memcmp(&a, &b, member_size)
                                                    == 0);
                                        ++cases;
                                }
                        }
                }
        }
        printf("Exact legacy comparison: %zu pack/unpack cases passed\n",
               cases);
}
