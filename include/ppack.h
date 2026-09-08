/**
 * SPDX-License-Identifier: MIT
 *
 * @file: ppack.h
 *
 * @brief
 *    Public API for the ppack library - a generic payload serialisation
 *    library for bit-aligned data fields.
 */

#ifndef PPACK_H_
#define PPACK_H_

#ifdef __cplusplus
extern "C" {
#endif

/* ================ INCLUDES ================================================ */

#include <stddef.h>
#include <stdint.h>

#include "ppack_platform.h"

/*
 * Re-export the platform abstraction so users picking up ppack.h get
 * ppack_byte_t and PPACK_PAYLOAD_UNITS for portable buffer declarations
 * without needing a separate include.
 */

/**
 * @brief Number of addressable units occupied by a payload of
 *        @c PPACK_PAYLOAD_BITS bits.
 *
 * On 16-bit MAU platforms, ppack allocates one addressable unit per
 *        logical octet. This ensures compatibility with other primitives.
 *
 * Use this to declare a portable payload buffer:
 * @code
 * ppack_byte_t payload[PPACK_PAYLOAD_UNITS];
 * @endcode
 */
/* cppcheck-suppress misra-c2012-2.5 ; @deviation Public API macro for caller
 * buffer sizing. */
#define PPACK_PAYLOAD_UNITS (PPACK_PAYLOAD_BITS / PPACK_ADDR_UNIT_BITS)

/**
 * @defgroup ppack_api Ppack Library
 *
 * @brief Generic payload serialisation/deserialisation for bit-aligned fields.
 *
 * @details
 *    The ppack library serialises and deserialises C structures into a
 *    variable-size payload (any multiple of 8 bits, up to 65528 bits)
 *    using arbitrary bit-aligned field descriptors. It supports
 *    @c uint8_t, @c uint16_t, @c int16_t, @c uint32_t, @c int32_t,
 *    IEEE-754 @c float, and raw bitfields up to 32 bits wide.
 *    Transport-specific limits must be checked by the caller.
 *
 *    The payload size is supplied at the call site via the
 *    @c payload_bits argument; classic 8-byte CAN payloads use
 *    @c payload_bits=64, CAN-FD frames may use up to 512.
 *
 *    The @c base_ptr parameter for pack/unpack operations should point
 *    to the structure that the @c ptr_offset members refer to. The
 *    unpack operation overwrites the structure's members with the
 *    decoded values.
 *
 *    Requires C11 (uses @c _Static_assert in @c ppack_platform.h).
 *
 *    ## Wire format
 *
 *    The payload is treated as N bits numbered 0..N-1, where N is the
 *    @c payload_bits argument.
 *    Bit @c K is in logical byte @c (K / 8) at position @c (K mod 8).
 *    Logical byte 0 is the first byte on the wire.
 *
 *    Bit 0 is the least significant bit within each logical byte.
 *    Multi-byte fields use little-endian order. This matches DBC
 *    @c byte_order=1. A 16-bit field at @c start_bit=0 with value
 *    @c 0x1234 writes @c 0x34 to byte 0 and @c 0x12 to byte 1.
 *
 *    @c PPACK_TYPE_F32 copies 32 IEEE-754 bits directly.
 *    The wire bytes follow the host @c uint32_t byte order.
 *    Any two little-endian hosts interoperate (for example,
 *    x86_64, ARM Cortex-M, AArch64, 16-bit MAU platforms).
 *    Little-endian and big-endian hosts do not interoperate without
 *    explicit byte swapping in user code.
 *
 *    Declare the payload buffer as @c ppack_byte_t
 *    payload[PPACK_PAYLOAD_UNITS]. This model works on all MAU sizes.
 *    The macro declares a 64-bit payload by default: eight storage units
 *    on both 8-bit and 16-bit MAU targets. Override @c PPACK_PAYLOAD_BITS
 *    for other sizes, or declare @c ppack_byte_t payload[N / 8u] and pass
 *    N as the runtime size. Increasing the runtime size does not resize
 *    the buffer. The caller must provide sufficient storage.
 *
 *    ## Scaled-field semantics
 *
 *    When @c behaviour is @c PPACK_BEHAVIOUR_SCALED the library applies:
 *    @code
 *      pack:    raw      = (physical - offset) / scale  -> integer
 *      unpack:  physical = (float)raw * scale + offset
 *    @endcode
 *
 *    @b Saturation: ppack_pack clamps the raw integer to the
 *    destination type's range (for example, 0..65535 for
 *    @c PPACK_TYPE_UINT16). Validate the input at the call site
 *    if your application must detect out-of-range physical values.
 *
 *    @b Quantization: ppack_pack truncates the float-to-integer cast
 *    toward zero (standard C cast rules). The maximum round-trip error
 *    is one LSB, equal to @c scale.
 *    Add @c 0.5f*scale*sign(physical) at the call site for
 *    round-to-nearest behaviour.
 *
 *    @b Float-range clamp: for @c PPACK_TYPE_UINT32 scaled fields,
 *    ppack_pack clamps to @c 4294967040. This is the largest float
 *    value that fits in a @c uint32_t.
 *    For @c PPACK_TYPE_INT32 it clamps to @c 2147483520.
 *    The values @c UINT32_MAX and @c INT32_MAX round up to
 *    @c 2^32 or @c 2^31 as 32-bit floats. That overflows the cast.
 *
 *    @b Non-finite values: ppack_pack rejects a SCALED field when
 *    the result is NaN. The function returns @c -PPACK_ERR_INVALARG.
 *    NaN can come from the source value or the scale/offset calculation.
 *    NaN has no valid integer representation.
 *    Positive and negative infinity saturate to the type's clamp bounds.
 *    @c PPACK_TYPE_F32 fields copy bits directly.
 *    They carry NaN and infinity values unchanged.
 *
 *    ## 16-bit MAU Platform Notes
 *
 *    On 16-bit MAU platforms, @c CHAR_BIT is 16.
 *    Any @c uint8_t the toolchain provides uses 16-bit storage.
 *    ppack detects this from @c CHAR_BIT and @c UCHAR_MAX.
 *    The library selects a 16-bit storage unit internally.
 *    It keeps an 8-bit logical layout to match other primitives.
 *    Two contract points apply on this platform:
 *
 *    @li A @c PPACK_TYPE_UINT8 field carries only the low 8 bits of its
 *        struct member through the wire. On pack, the upper 8 bits of
 *        the member's 16-bit storage are read but masked out. On
 *        unpack, the member's FULL storage unit is written: the low 8
 *        bits receive the wire value and the upper 8 bits are zeroed.
 *        Do not keep sentinel data in the upper bits of a
 *        @c PPACK_TYPE_UINT8 member; it will not survive an unpack.
 *
 *    @li The @c ptr_offset value comes from @c offsetof(), which
 *        returns @c char-units. On 16-bit MAU platforms a @c char is 16
 *        bits, so the value is naturally in 16-bit units; the library
 *        does not need to translate it. Always use @c offsetof() rather
 *        than computing offsets by hand.
 *
 * @{
 */

/* ================ DEFINES ================================================= */

/* ---------------- Error Codes --------------------------------------------- */

/** @brief Success */
#define PPACK_SUCCESS       0

/** @brief Invalid arguments (NULL pointer, zero field count, etc.) */
#define PPACK_ERR_INVALARG  1

/* Note: error code 2 is reserved for future use. */

/** @brief Requested item not found */
#define PPACK_ERR_NOTFOUND  3

/** @brief Null pointer detected */
#define PPACK_ERR_NULLPTR   4

/** @brief Field descriptor overflows the payload boundary */
#define PPACK_ERR_OVERFLOW  5

/* ================ STRUCTURES ============================================== */

/**
 * @brief Data types supported by the library.
 *
 * The struct member type below indicates the C type the user must
 * declare in their structure for the corresponding @c PPACK_TYPE_*
 * value, when @c behaviour is @c PPACK_BEHAVIOUR_RAW. For
 * @c PPACK_BEHAVIOUR_SCALED the struct member must always be @c float.
 */
enum ppack_type {
        PPACK_TYPE_UINT8 = 0, /**< 8-bit unsigned  (struct member: uint8_t)  */
        PPACK_TYPE_UINT16,    /**< 16-bit unsigned (struct member: uint16_t) */
        PPACK_TYPE_INT16,     /**< 16-bit signed   (struct member: int16_t)  */
        PPACK_TYPE_INT32,     /**< 32-bit signed   (struct member: int32_t)  */
        PPACK_TYPE_UINT32,    /**< 32-bit unsigned (struct member: uint32_t) */
        PPACK_TYPE_F32,       /**< 32-bit float    (struct member: float)    */
        PPACK_TYPE_BITS,      /**< Raw bitfield    (struct member: uint32_t) */
};

/**
 * @brief Field behaviour mode.
 *
 * @note  @c PPACK_TYPE_F32 ignores @c behaviour and always performs a
 *        raw bit-copy. @c PPACK_TYPE_BITS also ignores @c behaviour
 *        and always treats its source as a raw 32-bit pattern.
 *        @c PPACK_TYPE_UINT8 rejects @c PPACK_BEHAVIOUR_SCALED with
 *        @c PPACK_ERR_INVALARG.
 */
enum ppack_behaviour {
        PPACK_BEHAVIOUR_RAW,    /**< Raw value, no scaling */
        PPACK_BEHAVIOUR_SCALED, /**< Apply scale and offset */
};

/**
 * @brief Describes a field within a payload.
 *
 * @details
 * - ::ppack_field::type  Data type of the field.
 * - ::ppack_field::start_bit  Starting bit position (0..payload_bits-1).
 * - ::ppack_field::bit_length  Number of bits (1-32).
 * - ::ppack_field::ptr_offset  Offset from offsetof() into the base structure.
 * - ::ppack_field::scale  Scaling factor, ignored for F32 and BITS.
 * - ::ppack_field::offset  Offset after scaling, ignored for F32 and BITS.
 * - ::ppack_field::behaviour  Raw or scaled mode, see ppack_behaviour.
 *
 * @note  Set @c bit_length to match the natural width of @c type.
 *        The library accepts larger values (it enforces
 *        @c bit_length <= 32 and
 *        @c start_bit + bit_length <= payload_bits).
 *        Only the low bits of the source carry data.
 *        The upper wire bits are zero or sign-extended.
 *
 * @note  @c PPACK_TYPE_F32 always copies bits directly.
 *        The library ignores @c behaviour, @c scale, and @c offset for this
 * type. This requires matching IEEE-754 layout and endianness on all nodes.
 *
 * @note  For @c PPACK_BEHAVIOUR_SCALED the struct member must be @c float.
 *        For @c PPACK_BEHAVIOUR_RAW  the struct member must match the
 *        native C type implied by @c type (see @c ppack_type).
 */
struct ppack_field {
        enum ppack_type type;
        uint16_t start_bit;
        uint16_t bit_length;
        size_t ptr_offset;
        float scale;
        float offset;
        enum ppack_behaviour behaviour;
};

/* ================ TYPEDEFS ================================================ */

/* ================ MACROS ================================================== */

/* ================ GLOBAL VARIABLES ======================================== */

/* ================ GLOBAL PROTOTYPES ======================================= */

/**
 * @brief Pack a payload from given fields into a buffer.
 *
 * ppack_pack reads each field from the @c base_ptr structure,
 * applies any scaling, and writes the bits to @c payload.
 * It places each value at the field's @c start_bit position.
 * Bits outside all field ranges are set to zero.
 *
 * @param[in]  base_ptr     Pointer to source structure
 * @param[out] payload      Destination buffer of exactly @c payload_bits
 *                          bits. Declare as @c ppack_byte_t
 *                          @c [PPACK_PAYLOAD_UNITS] for the default
 *                          64-bit payload, or size manually as
 *                          @c [payload_bits / PPACK_ADDR_UNIT_BITS]
 *                          for non-default sizes.
 * @param[in]  payload_bits Payload size in bits. Must be a positive
 *                          multiple of @c PPACK_ADDR_UNIT_BITS and no
 *                          greater than @c PPACK_MAX_PAYLOAD_BITS.
 * @param[in]  fields       Array of field descriptors
 * @param[in]  field_count  Number of fields
 *
 * @return PPACK_SUCCESS on success
 * @return -PPACK_ERR_NULLPTR   if @c base_ptr or @c payload is NULL
 * @return -PPACK_ERR_INVALARG  on invalid arguments. This includes:
 *                               @c field_count is 0, @c fields is NULL,
 *                               @c payload_bits is 0, @c payload_bits is not
 *                               a multiple of @c PPACK_ADDR_UNIT_BITS,
 *                               @c payload_bits exceeds the maximum,
 *                               @c bit_length is 0 or greater than 32,
 *                               scaling is set for
 *                               @c PPACK_TYPE_UINT8, or a SCALED field
 *                               produces a NaN result.
 * @return -PPACK_ERR_OVERFLOW  when @c start_bit plus bit_length exceeds
 *                               @c payload_bits, or @c scale is 0.0 on a
 *                               SCALED field.
 * @return -PPACK_ERR_NOTFOUND  if an unknown field type is encountered
 *
 * @note  Not thread-safe. Callers sharing @c base_ptr or @c payload
 *        across threads or ISRs must provide their own mutual exclusion.
 *
 * @note  Out-of-range scaled values clamp to the destination type's
 *        range. The function rejects NaN with @c -PPACK_ERR_INVALARG.
 *        See the "Scaled-field semantics" section above.
 *
 * @note  The function does not roll back on failure.
 *        ppack_pack clears the payload buffer before it validates the
 *        field descriptors. On an error return the buffer contains zeroed
 *        data and possibly partial writes.
 *        Do not transmit a payload after a non-zero return.
 */
int ppack_pack(const void *base_ptr, void *payload, size_t payload_bits,
               const struct ppack_field *fields, size_t field_count);

/**
 * @brief Unpack a payload from a buffer into specified field locations.
 *
 * ppack_unpack reads @c payload_bits bits from @c payload.
 * It writes each decoded value to the matching offset in @c base_ptr.
 * Bytes inside struct members that have no field mapping keep their current
 * values.
 *
 * @param[out] base_ptr     Pointer to destination structure
 * @param[in]  payload      Source buffer of exactly @c payload_bits
 *                          bits. Declare as @c ppack_byte_t
 *                          @c [PPACK_PAYLOAD_UNITS] for the default
 *                          64-bit payload, or size manually as
 *                          @c [payload_bits / PPACK_ADDR_UNIT_BITS]
 *                          for non-default sizes.
 * @param[in]  payload_bits Payload size in bits. Must be a positive
 *                          multiple of @c PPACK_ADDR_UNIT_BITS and no
 *                          greater than @c PPACK_MAX_PAYLOAD_BITS.
 * @param[in]  fields       Array of field descriptors
 * @param[in]  field_count  Number of fields
 *
 * @return PPACK_SUCCESS on success
 * @return -PPACK_ERR_NULLPTR   if @c base_ptr or @c payload is NULL
 * @return -PPACK_ERR_INVALARG  on invalid arguments. This includes:
 *                               @c field_count is 0, @c fields is NULL,
 *                               @c payload_bits is 0, @c payload_bits is not
 *                               a multiple of @c PPACK_ADDR_UNIT_BITS,
 *                               @c payload_bits exceeds the maximum,
 *                               @c bit_length is 0 or greater than 32,
 *                               or scaling is set for
 *                               @c PPACK_TYPE_UINT8.
 * @return -PPACK_ERR_OVERFLOW  if @c start_bit + bit_length exceeds
 *                               @c payload_bits
 * @return -PPACK_ERR_NOTFOUND  if an unknown field type is encountered
 *
 * @note  Not thread-safe. Callers sharing @c base_ptr or @c payload
 *        across threads or ISRs must provide their own mutual exclusion.
 *
 * @note  The function does not roll back on failure.
 *        ppack_unpack decodes fields in descriptor order.
 *        On an error return it has already written all fields before
 *        the failing descriptor.
 *        Do not use the destination structure after a non-zero return.
 */
int ppack_unpack(void *base_ptr, const void *payload, size_t payload_bits,
                 const struct ppack_field *fields, size_t field_count);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* PPACK_H_ */
