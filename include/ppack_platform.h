/**
 * SPDX-License-Identifier: MIT
 *
 * @file: ppack_platform.h
 *
 * @brief
 *    Platform abstraction for ppack.
 *
 *    The library detects the target minimum addressable unit (MAU)
 *    size (8 or 16 bits). It defines the type aliases and
 *    compile-time constants the implementation needs.
 *
 *    On byte-addressable targets (x86_64, ARM, AArch64, RISC-V, AVR)
 *    @c ppack_byte_t is @c uint8_t.
 *
 *    On targets where @c CHAR_BIT is 16, @c ppack_byte_t is @c uint16_t.
 *    ppack uses 8-bit logical units for the payload on all targets.
 *    This matches other primitives in this project, such as @c ucrc.
 *
 *    @c PPACK_PAYLOAD_BITS sets the default payload size for the
 *    @c PPACK_PAYLOAD_UNITS buffer-sizing macro.
 *    The @c ppack.h header defines @c PPACK_PAYLOAD_UNITS.
 *    Override @c PPACK_PAYLOAD_BITS before you include this header
 *    (for example, @c -DPPACK_PAYLOAD_BITS=128 or
 *    @c -DPPACK_PAYLOAD_BITS=512).
 *    The runtime API takes the payload size as an explicit argument.
 *
 *    The library's wire format is identical across both addressing
 *    models: a sequence of N bits (where N is the @c payload_bits
 *    argument, a multiple of 8 between 8 and 65528), with bit 0
 *    corresponding to the least significant bit of the first logical
 *    8-bit byte. See the "Wire format" section in the README for the
 *    full contract.
 *
 *    Define @c PPACK_SIMULATE_16BIT_MAU at compile time on a byte-
 *    addressable host to exercise the 16-bit MAU code path.
 *    Use this flag for library development and host unit tests only.
 *    Production builds must leave it undefined.
 */

#ifndef PPACK_PLATFORM_H_
#define PPACK_PLATFORM_H_

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Auto-detection: a target is treated as 16-bit MAU when CHAR_BIT > 8
 * or when UCHAR_MAX exceeds 255. PPACK_SIMULATE_16BIT_MAU forces this
 * branch on byte-addressable hosts for unit-test simulation.
 */
#if defined(PPACK_SIMULATE_16BIT_MAU) || CHAR_BIT > 8 || UCHAR_MAX > 255u
#define PPACK_IS_16BIT_MAU   1
/** @brief Logical bits per payload unit. Always 8 for interoperability. */
#define PPACK_ADDR_UNIT_BITS 8u
/** @brief Storage unit used for the payload buffer. */
typedef uint16_t ppack_byte_t;
#else
/* cppcheck-suppress misra-c2012-2.5 ; @deviation Public macro for external
 * platform detection. */
#define PPACK_IS_16BIT_MAU   0
/** @brief Logical bits per payload unit. Always 8 for interoperability. */
#define PPACK_ADDR_UNIT_BITS 8u
typedef uint8_t ppack_byte_t;
#endif

/**
 * @brief Storage type for a `uint8_t` struct member on this target.
 *
 * On byte-addressable hosts this is @c uint8_t.
 * On a 16-bit MAU target the narrowest integer type is 16 bits.
 * Any @c uint8_t the toolchain provides uses 16-bit storage.
 * This typedef mirrors that wider storage.
 * The C standard marks @c uint8_t as optional when it maps to 16-bit
 * storage, but these toolchains provide it anyway.
 *
 * @note  The library implementation and unit tests use this alias.
 *        Application code must declare struct members as @c uint8_t
 *        for @c PPACK_TYPE_UINT8 fields on real targets.
 *        Host builds that define @c PPACK_SIMULATE_16BIT_MAU use
 *        @c ppack_u8_t to reproduce the 16-bit MAU storage layout.
 */
typedef ppack_byte_t ppack_u8_t;

/**
 * @brief Maximum payload size in bits: 8191 logical octets.
 *
 * This is the largest multiple of 8 representable in uint16_t.
 * It keeps payload sizes representable with 16-bit size_t and valid
 * bit positions within the uint16_t indices used by the codec.
 * This limit does not set the default buffer size or a transport limit.
 */
#define PPACK_MAX_PAYLOAD_BITS 65528u

/**
 * @brief Default payload size in bits for @c PPACK_PAYLOAD_UNITS
 *        (defined in @c ppack.h).
 *
 * Override at the toolchain level (for example, @c -DPPACK_PAYLOAD_BITS=128)
 * to declare stack buffers of a different size.
 * The value must be a positive multiple of @c PPACK_ADDR_UNIT_BITS
 * and no greater than @c PPACK_MAX_PAYLOAD_BITS.
 *
 * The runtime API takes the payload size as an explicit argument.
 */
#ifndef PPACK_PAYLOAD_BITS
#define PPACK_PAYLOAD_BITS 64u
#endif

/**
 * @brief Convert a payload bit index to its addressable-unit index.
 *
 * @param[in] bit Bit position within the payload.
 * @return        Index into a @c ppack_byte_t array.
 */
static inline uint16_t
ppack_bit_to_unit(uint16_t bit)
{
        return (uint16_t)((uint32_t)bit / PPACK_ADDR_UNIT_BITS);
}

/**
 * @brief Convert a payload bit index to its intra-unit bit offset.
 *
 * @param[in] bit Bit position within the payload.
 * @return        Bit offset (0..PPACK_ADDR_UNIT_BITS-1) within the
 *                addressable unit returned by @c ppack_bit_to_unit.
 */
static inline uint16_t
ppack_bit_to_shift(uint16_t bit)
{
        return (uint16_t)((uint32_t)bit % PPACK_ADDR_UNIT_BITS);
}

/* Catch unsupported configurations at compile time. */
_Static_assert((PPACK_ADDR_UNIT_BITS == 8u) || (PPACK_ADDR_UNIT_BITS == 16u),
               "ppack only supports 8-bit or 16-bit addressable units");

_Static_assert((PPACK_PAYLOAD_BITS > 0u)
                   && ((PPACK_PAYLOAD_BITS % PPACK_ADDR_UNIT_BITS) == 0u),
               "PPACK_PAYLOAD_BITS must be a positive multiple of "
               "PPACK_ADDR_UNIT_BITS");

_Static_assert(PPACK_PAYLOAD_BITS <= PPACK_MAX_PAYLOAD_BITS,
               "PPACK_PAYLOAD_BITS must be at most 65528");

_Static_assert(sizeof(float) == sizeof(uint32_t),
               "ppack requires a 32-bit float "
               "(PPACK_TYPE_F32 wire-format prerequisite)");

#endif /* PPACK_PLATFORM_H_ */
