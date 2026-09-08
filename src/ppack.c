/**
 * SPDX-License-Identifier: MIT
 *
 * @file: ppack.c
 *
 * @brief
 *    Implementation of the ppack library - generic payload serialisation.
 *
 * @par MISRA C:2023 Compliance
 *
 * The library is analysed with misch (cppcheck-backed MISRA C:2023
 * analysis), configured by misra.toml at the project root. The audit is
 * clean; every deviation is recorded at point of use as a
 * `cppcheck-suppress` comment carrying an `@deviation` rationale, or
 * project-wide in misra-deviations.txt (Rule 15.5, single-exit house
 * style). The only required-rule deviations are three Rule 21.15 sites:
 * the type-erased member copies centralised in ppack_member_write /
 * ppack_member_read, and the deliberate float/uint32_t pun in F32
 * unpack. Run `misch run` to reproduce the audit and `misch deviations`
 * to harvest the deviation record.
 */

/* ================ INCLUDES ================================================ */

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "ppack.h"
#include "ppack_platform.h"

/* ================ DEFINES ================================================= */

/** Maximum supported field width in bits. */
#define PPACK_MAX_FIELD_BITS   32u

/* ================ STRUCTURES ============================================== */

/* ================ TYPEDEFS ================================================ */

/* ================ STATIC PROTOTYPES ======================================= */

/* ================ STATIC VARIABLES ======================================== */

/* ================ MACROS ================================================== */

/*
 * Largest uint32_t / int32_t values that are *exactly* representable as a
 * 32-bit IEEE-754 float AND fit in the destination integer. Float spacing
 * in [2^31, 2^32) is 256 and in [2^30, 2^31) is 128, so UINT32_MAX
 * (4294967295) and INT32_MAX (2147483647) both round up to 2^32 / 2^31 as
 * floats: values that are out of range for the destination cast and would
 * invoke UB. Clamp to the next-lower representable float instead.
 */
#define PPACK_FLOAT_UINT32_MAX ((float)0xFFFFFF00u) /* 4294967040 */
#define PPACK_FLOAT_INT32_MAX  ((float)0x7FFFFF80)  /* 2147483520 */
#define PPACK_FLOAT_INT32_MIN  (-2147483648.0f)     /* -2^31, exact */

/* ================ STATIC FUNCTIONS ======================================== */

/**
 * @brief Clamp a float to a closed range [@p lo, @p hi].
 *
 * Single-exit by design (MISRA 15.5 friendly).
 */
static inline float
ppack_clamp_float(float val, float lo, float hi)
{
        float result = val;

        if (result < lo) {
                result = lo;
        } else if (result > hi) {
                result = hi;
        } else {
                /* in range, leave untouched */
        }

        return result;
}

/**
 * @brief Copy @p n bytes from a typed temporary into a struct member
 *        addressed as char* via its offsetof() value.
 *
 * Centralises the type-erased member store so the deliberate Rule 21.15
 * deviation has a single, documented site.
 */
static void
ppack_member_write(char *dst, const void *src, size_t n)
{
        /* cppcheck-suppress misra-c2012-21.15 ; @deviation Type-erased copy to
         * a struct member addressed via offsetof(); the source temporary's type
         * matches the member's declared type by API contract (see ppack_type).
         */
        (void)memcpy(dst, src, n);
}

/**
 * @brief Copy @p n bytes from a struct member addressed as const char*
 *        via its offsetof() value into a typed temporary.
 *
 * Centralises the type-erased member load so the deliberate Rule 21.15
 * deviation has a single, documented site.
 */
static void
ppack_member_read(void *dst, const char *src, size_t n)
{
        /* cppcheck-suppress misra-c2012-21.15 ; @deviation Type-erased copy
         * from a struct member addressed via offsetof(); the destination
         * temporary's type matches the member's declared type by API contract
         * (see ppack_type). */
        (void)memcpy(dst, src, n);
}

/**
 * @brief Write a bitfield of arbitrary length into a payload buffer.
 *
 * @param[in,out] payload   Output buffer
 * @param[in]     start_bit Starting bit position within the payload
 * @param[in]     bit_len   Number of bits to write (1-32)
 * @param[in]     value     Raw 32-bit value (lower bit_len bits written)
 *
 * @pre  start_bit + bit_len <= payload size  (caller must validate)
 * @pre  bit_len <= 32                        (caller must validate)
 */
static void
write_bits(void *payload, uint16_t start_bit, uint16_t bit_len, uint32_t value)
{
        /* cppcheck-suppress misra-c2012-11.5 ; @deviation Opaque void* payload
         * requires unit-typed access for indexed read-modify-write. */
        ppack_byte_t *words = (ppack_byte_t *)payload;
        uint16_t bits_written = 0;

        while (bits_written < bit_len) {
                uint16_t current_bit = start_bit + bits_written;
                uint16_t unit_idx = ppack_bit_to_unit(current_bit);
                uint16_t bit_offset = ppack_bit_to_shift(current_bit);
                uint16_t bits_left = bit_len - bits_written;
                uint16_t bits_this_unit = PPACK_ADDR_UNIT_BITS - bit_offset;

                if (bits_this_unit > bits_left) {
                        bits_this_unit = bits_left;
                }

                uint32_t val_chunk = (value >> bits_written)
                                     & (((uint32_t)1u << bits_this_unit) - 1u);
                uint32_t mask = (((uint32_t)1u << bits_this_unit) - 1u)
                                << bit_offset;

                uint32_t unit_val = words[unit_idx];
                unit_val = (unit_val & ~mask) | (val_chunk << bit_offset);
                words[unit_idx] = (ppack_byte_t)unit_val;

                bits_written += bits_this_unit;
        }
}

/**
 * @brief Read a bitfield of arbitrary length from a payload buffer.
 *
 * @param[in] payload   Input buffer
 * @param[in] start_bit Starting bit position within the payload
 * @param[in] bit_len   Number of bits to read (1-32)
 *
 * @return 32-bit value with extracted bits in LSB positions
 *
 * @pre  start_bit + bit_len <= payload size  (caller must validate)
 * @pre  bit_len <= 32                        (caller must validate)
 */
static uint32_t
read_bits(const void *payload, uint16_t start_bit, uint16_t bit_len)
{
        /* cppcheck-suppress misra-c2012-11.5 ; @deviation Opaque const void*
         * payload requires unit-typed access for indexed reads. */
        const ppack_byte_t *words = (const ppack_byte_t *)payload;
        uint32_t result = 0;
        uint16_t bits_read = 0;

        while (bits_read < bit_len) {
                uint16_t current_bit = start_bit + bits_read;
                uint16_t unit_idx = ppack_bit_to_unit(current_bit);
                uint16_t bit_offset = ppack_bit_to_shift(current_bit);
                uint16_t bits_left = bit_len - bits_read;
                uint16_t bits_this_unit = PPACK_ADDR_UNIT_BITS - bit_offset;

                if (bits_this_unit > bits_left) {
                        bits_this_unit = bits_left;
                }

                uint32_t mask = (((uint32_t)1u << bits_this_unit) - 1u);
                uint32_t unit_val = words[unit_idx];
                uint32_t val_chunk = (unit_val >> bit_offset) & mask;
                result |= (val_chunk << bits_read);

                bits_read += bits_this_unit;
        }

        return result;
}

/**
 * @brief Sign-extend a value from @p width bits to 32 bits.
 *
 * @param[in] value Raw unsigned bit pattern (lower @p width bits are valid)
 * @param[in] width Number of bits in the original signed field (1-32)
 *
 * @return Sign-extended 32-bit signed integer
 *
 * @note  The unsigned-to-signed cast on the final return is well-defined
 *        on every two's-complement target; the library's supported
 *        platforms all use two's-complement (see README "Supported
 *        architectures").
 */
static int32_t
sign_extend(uint32_t value, uint16_t width)
{
        uint16_t shift = width - 1u;
        uint32_t sign_bit = (uint32_t)1u << shift;

        /* If sign bit is set, fill upper bits with 1s. */
        if ((value & sign_bit) != 0u) {
                /* Compute the sign-extended bit pattern in an intermediate
                 * variable, then cast: keeps the cast operand a simple
                 * variable rather than a composite expression. */
                uint32_t bits = value | ~(sign_bit - 1u);
                return (int32_t)bits;
        }
        return (int32_t)value;
}

/**
 * @brief Validate a field descriptor against the runtime payload size.
 *
 * @param[in] f            Pointer to the field descriptor to check
 * @param[in] payload_bits Payload size in bits (caller-validated)
 *
 * @return PPACK_SUCCESS on success
 * @return -PPACK_ERR_INVALARG  if bit_length is 0 or > 32
 * @return -PPACK_ERR_OVERFLOW  if start_bit + bit_length exceeds
 *                              payload_bits
 */
static int
validate_field(const struct ppack_field *f, size_t payload_bits)
{
        if ((f->bit_length == 0u) || (f->bit_length > PPACK_MAX_FIELD_BITS)) {
                return -PPACK_ERR_INVALARG;
        }
        /* Check the start before subtracting. Adding start and length can
         * wrap when size_t is 16 bits, even for a small payload. */
        if (((size_t)f->start_bit > payload_bits)
            || ((size_t)f->bit_length
                > (payload_bits - (size_t)f->start_bit))) {
                return -PPACK_ERR_OVERFLOW;
        }
        return PPACK_SUCCESS;
}

/**
 * @brief Validate the @c payload_bits argument supplied to pack/unpack.
 *
 * @param[in] payload_bits Caller-supplied payload size in bits
 *
 * @return PPACK_SUCCESS on success
 * @return -PPACK_ERR_INVALARG  if @c payload_bits is 0, not a multiple
 *                              of @c PPACK_ADDR_UNIT_BITS, or exceeds
 *                              @c PPACK_MAX_PAYLOAD_BITS
 */
static int
validate_payload_bits(size_t payload_bits)
{
        if ((payload_bits == 0u) || (payload_bits > PPACK_MAX_PAYLOAD_BITS)) {
                return -PPACK_ERR_INVALARG;
        }
        if ((payload_bits % (size_t)PPACK_ADDR_UNIT_BITS) != 0u) {
                return -PPACK_ERR_INVALARG;
        }
        return PPACK_SUCCESS;
}

/* ================ GLOBAL PROTOTYPES ======================================= */

int
/* cppcheck-suppress misra-c2012-8.7 ; @deviation Public API function consumed
   by external translation units. */
ppack_unpack(void *base_ptr, const void *payload, size_t payload_bits,
             const struct ppack_field *fields, size_t field_count)
{
        if ((base_ptr == NULL) || (payload == NULL)) {
                return -PPACK_ERR_NULLPTR;
        }

        if ((field_count == 0u) || (fields == NULL)) {
                return -PPACK_ERR_INVALARG;
        }

        int pret = validate_payload_bits(payload_bits);
        if (pret != PPACK_SUCCESS) {
                return pret;
        }

        for (size_t i = 0; i < field_count; ++i) {
                const struct ppack_field *f = &fields[i];

                int vret = validate_field(f, payload_bits);
                if (vret != PPACK_SUCCESS) {
                        return vret;
                }

                /* cppcheck-suppress misra-c2012-11.5 ; @deviation void* opaque
                 * struct requires char* for offsetof()-based member addressing.
                 */
                char *field_ptr = (char *)base_ptr;
                /* cppcheck-suppress misra-c2012-18.4 ; @deviation ptr_offset is
                 * the user-supplied offsetof() value and is added to the base
                 * pointer. */
                field_ptr += f->ptr_offset;
                uint32_t raw = read_bits(payload, f->start_bit, f->bit_length);

                switch (f->type) {
                case PPACK_TYPE_UINT8: {
                        if (f->behaviour == PPACK_BEHAVIOUR_SCALED) {
                                return -PPACK_ERR_INVALARG;
                        }
                        ppack_u8_t tmp = (ppack_u8_t)(raw & 0xFFu);
                        ppack_member_write(field_ptr, &tmp, sizeof(tmp));
                        break;
                }

                case PPACK_TYPE_UINT16: {
                        if (f->behaviour == PPACK_BEHAVIOUR_SCALED) {
                                float tmp = (((float)(uint16_t)raw) * f->scale)
                                            + f->offset;
                                ppack_member_write(field_ptr, &tmp,
                                                   sizeof(tmp));
                        } else {
                                uint16_t tmp = (uint16_t)raw;
                                ppack_member_write(field_ptr, &tmp,
                                                   sizeof(tmp));
                        }
                        break;
                }

                case PPACK_TYPE_INT16: {
                        if (f->behaviour == PPACK_BEHAVIOUR_SCALED) {
                                int32_t sval = sign_extend(raw, f->bit_length);
                                float tmp =
                                    (((float)sval) * f->scale) + f->offset;
                                ppack_member_write(field_ptr, &tmp,
                                                   sizeof(tmp));
                        } else {
                                int16_t tmp =
                                    (int16_t)sign_extend(raw, f->bit_length);
                                ppack_member_write(field_ptr, &tmp,
                                                   sizeof(tmp));
                        }
                        break;
                }

                case PPACK_TYPE_UINT32: {
                        if (f->behaviour == PPACK_BEHAVIOUR_SCALED) {
                                float tmp =
                                    (((float)raw) * f->scale) + f->offset;
                                ppack_member_write(field_ptr, &tmp,
                                                   sizeof(tmp));
                        } else {
                                ppack_member_write(field_ptr, &raw,
                                                   sizeof(raw));
                        }
                        break;
                }

                case PPACK_TYPE_INT32: {
                        int32_t sval = sign_extend(raw, f->bit_length);
                        if (f->behaviour == PPACK_BEHAVIOUR_SCALED) {
                                float tmp =
                                    (((float)sval) * f->scale) + f->offset;
                                ppack_member_write(field_ptr, &tmp,
                                                   sizeof(tmp));
                        } else {
                                ppack_member_write(field_ptr, &sval,
                                                   sizeof(sval));
                        }
                        break;
                }

                case PPACK_TYPE_F32: {
                        float tmp;
                        /* cppcheck-suppress misra-c2012-21.15 ; @deviation
                         * Deliberate float/uint32_t bit reinterpretation;
                         * memcpy is the well-defined idiom for type punning. */
                        (void)memcpy(&tmp, &raw, sizeof(tmp));
                        ppack_member_write(field_ptr, &tmp, sizeof(tmp));
                        break;
                }

                case PPACK_TYPE_BITS: {
                        ppack_member_write(field_ptr, &raw, sizeof(raw));
                        break;
                }

                default: return -PPACK_ERR_NOTFOUND;
                }
        }

        return PPACK_SUCCESS;
}

int
/* cppcheck-suppress misra-c2012-8.7 ; @deviation Public API function consumed
   by external translation units. */
ppack_pack(const void *base_ptr, void *payload, size_t payload_bits,
           const struct ppack_field *fields, size_t field_count)
{
        if ((base_ptr == NULL) || (payload == NULL)) {
                return -PPACK_ERR_NULLPTR;
        }

        if ((field_count == 0u) || (fields == NULL)) {
                return -PPACK_ERR_INVALARG;
        }

        int pret = validate_payload_bits(payload_bits);
        if (pret != PPACK_SUCCESS) {
                return pret;
        }

        size_t payload_units = payload_bits / (size_t)PPACK_ADDR_UNIT_BITS;
        (void)memset(payload, 0, sizeof(ppack_byte_t) * payload_units);

        for (size_t i = 0; i < field_count; ++i) {
                const struct ppack_field *f = &fields[i];

                int vret = validate_field(f, payload_bits);
                if (vret != PPACK_SUCCESS) {
                        return vret;
                }

                /* cppcheck-suppress misra-c2012-11.5 ; @deviation const void*
                 * opaque struct requires const char* for offsetof()-based
                 * member addressing. */
                const char *field_ptr = (const char *)base_ptr;
                /* cppcheck-suppress misra-c2012-18.4 ; @deviation ptr_offset is
                 * the user-supplied offsetof() value. */
                field_ptr += f->ptr_offset;
                uint32_t raw = 0u;

                switch (f->type) {
                case PPACK_TYPE_UINT8: {
                        if (f->behaviour == PPACK_BEHAVIOUR_SCALED) {
                                return -PPACK_ERR_INVALARG;
                        }
                        ppack_u8_t tmp;
                        ppack_member_read(&tmp, field_ptr, sizeof(tmp));
                        uint32_t mask_val = (uint32_t)tmp;
                        mask_val &= 0xFFu;
                        raw = mask_val;
                        break;
                }

                case PPACK_TYPE_UINT16: {
                        if (f->behaviour == PPACK_BEHAVIOUR_SCALED) {
                                if (f->scale == 0.0f) {
                                        return -PPACK_ERR_OVERFLOW;
                                }
                                float val;
                                ppack_member_read(&val, field_ptr, sizeof(val));
                                float scaled = (val - f->offset) / f->scale;
                                if (isnan(scaled) != 0) {
                                        return -PPACK_ERR_INVALARG;
                                }
                                scaled =
                                    ppack_clamp_float(scaled, 0.0f, 65535.0f);
                                raw = (uint32_t)(uint16_t)scaled;
                        } else {
                                uint16_t tmp;
                                ppack_member_read(&tmp, field_ptr, sizeof(tmp));
                                raw = (uint32_t)tmp;
                        }
                        break;
                }

                case PPACK_TYPE_INT16: {
                        if (f->behaviour == PPACK_BEHAVIOUR_SCALED) {
                                if (f->scale == 0.0f) {
                                        return -PPACK_ERR_OVERFLOW;
                                }
                                float val;
                                ppack_member_read(&val, field_ptr, sizeof(val));
                                float scaled = (val - f->offset) / f->scale;
                                if (isnan(scaled) != 0) {
                                        return -PPACK_ERR_INVALARG;
                                }
                                scaled = ppack_clamp_float(scaled, -32768.0f,
                                                           32767.0f);
                                raw = (uint32_t)(int16_t)scaled;
                        } else {
                                int16_t tmp;
                                ppack_member_read(&tmp, field_ptr, sizeof(tmp));
                                raw = (uint32_t)tmp;
                        }
                        break;
                }

                case PPACK_TYPE_UINT32: {
                        if (f->behaviour == PPACK_BEHAVIOUR_SCALED) {
                                if (f->scale == 0.0f) {
                                        return -PPACK_ERR_OVERFLOW;
                                }
                                float val;
                                ppack_member_read(&val, field_ptr, sizeof(val));
                                float scaled = (val - f->offset) / f->scale;
                                if (isnan(scaled) != 0) {
                                        return -PPACK_ERR_INVALARG;
                                }
                                scaled = ppack_clamp_float(
                                    scaled, 0.0f, PPACK_FLOAT_UINT32_MAX);
                                raw = (uint32_t)scaled;
                        } else {
                                ppack_member_read(&raw, field_ptr, sizeof(raw));
                        }
                        break;
                }

                case PPACK_TYPE_INT32: {
                        if (f->behaviour == PPACK_BEHAVIOUR_SCALED) {
                                if (f->scale == 0.0f) {
                                        return -PPACK_ERR_OVERFLOW;
                                }
                                float val;
                                ppack_member_read(&val, field_ptr, sizeof(val));
                                float scaled = (val - f->offset) / f->scale;
                                if (isnan(scaled) != 0) {
                                        return -PPACK_ERR_INVALARG;
                                }
                                scaled = ppack_clamp_float(
                                    scaled, PPACK_FLOAT_INT32_MIN,
                                    PPACK_FLOAT_INT32_MAX);
                                raw = (uint32_t)(int32_t)scaled;
                        } else {
                                int32_t tmp;
                                ppack_member_read(&tmp, field_ptr, sizeof(tmp));
                                raw = (uint32_t)tmp;
                        }
                        break;
                }

                case PPACK_TYPE_F32: {
                        uint32_t tmp;
                        ppack_member_read(&tmp, field_ptr, sizeof(tmp));
                        raw = tmp;
                        break;
                }

                case PPACK_TYPE_BITS: {
                        ppack_member_read(&raw, field_ptr, sizeof(raw));
                        break;
                }

                default: return -PPACK_ERR_NOTFOUND;
                }

                write_bits(payload, f->start_bit, f->bit_length, raw);
        }

        return PPACK_SUCCESS;
}
