// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cstdint>

#if !defined(NO_ENDIAN_H)
    #if defined(USE_ENDIAN_H) || defined(USE_MACHINE_ENDIAN_H) || defined(USE_SYS_ENDIAN_H)
        #define OVERRIDDEN_ENDIAN_H
    #endif

    #if !defined(OVERRIDDEN_ENDIAN_H)
        #if defined(__linux__) || defined(__GLIBC__) || defined(__CYGWIN__) || defined(__ANDROID__)
            #define USE_ENDIAN_H
        #elif defined(__APPLE__)
            #define USE_MACHINE_ENDIAN_H
        #elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
            #define USE_SYS_ENDIAN_H
        #endif
    #endif

    #if defined(USE_ENDIAN_H)
        #include <endian.h>
    #elif defined(USE_MACHINE_ENDIAN_H)
        #include <machine/endian.h>
    #elif defined(USE_SYS_ENDIAN_H)
        #include <sys/endian.h>
    #elif defined(__sun)
        #include <sys/isa_defs.h>
    #elif defined(__MINGW32__) || defined(__MINGW64__) || !(defined(_WIN64) || defined(_WIN32))
        #include <sys/param.h>
    #endif
#endif

#if !defined(TARGET_LITTLE_ENDIAN) && !defined(TARGET_BIG_ENDIAN)
    #if (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)  /*GCC*/ || \
    (defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN)  /*Linux*/ || \
    (defined(_BYTE_ORDER) && _BYTE_ORDER == _LITTLE_ENDIAN)  /*xBSD,Sun*/ || \
    (defined(BYTE_ORDER) && BYTE_ORDER == LITTLE_ENDIAN)  /*Apple,MingW*/ || \
    defined(__LITTLE_ENDIAN__)  /*GCC Mac*/ || defined(__ARMEL__)  /*GCC,Clang*/ || \
    defined(__THUMBEL__)  /*GCC,Clang*/ || defined(__AARCH64EL__)  /*GCC,Clang*/ || \
    defined(_MIPSEL)  /*GCC,Clang*/ || defined(__MIPSEL)  /*GCC,Clang*/ || \
    defined(__MIPSEL__)  /*GCC,Clang*/ || defined(_M_IX86)  /*MSVC*/ || \
    defined(_M_X64)  /*MSVC*/ || defined(_M_IA64)  /*MSVC*/ || \
    defined(_M_AMD64)  /*MSVC*/ || defined(_M_ARM)  /*MSVC*/ || \
    defined(_M_ARM64)  /*MSVC*/
        #define TARGET_LITTLE_ENDIAN
    #elif (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)  /*GCC*/ || \
    (defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN)  /*Linux*/ || \
    (defined(_BYTE_ORDER) && _BYTE_ORDER == _BIG_ENDIAN)  /*xBSD,Sun*/ || \
    (defined(BYTE_ORDER) && BYTE_ORDER == BIG_ENDIAN)  /*Apple,MingW*/ || \
    defined(_M_PPC)  /*MSVC for XBox-360*/ || defined(__BIG_ENDIAN__)  /*GCC Mac*/ || \
    defined(__ARMEB__)  /*GCC,Clang*/ || defined(__THUMBEB__)  /*GCC,Clang*/ || \
    defined(__AARCH64EB__)  /*GCC,Clang*/ || defined(_MIPSEB)  /*GCC,Clang*/ || \
    defined(__MIPSEB)  /*GCC,Clang*/ || defined(__MIPSEB__)  /*GCC,Clang*/
        #define TARGET_BIG_ENDIAN
    #elif defined(_WIN32)
        #define TARGET_LITTLE_ENDIAN
    #endif  // End of byte order checks
#endif  // End of guard for explicitly defined endianness

/*
 * Swap intrinsics
 */
#if defined(__clang__) || (defined(__GNUC__) && \
    ((__GNUC__ == 4 && __GNUC_MINOR__ >= 8) || __GNUC__ >= 5))
    #define bswap16(x) __builtin_bswap16((x))
    #define bswap32(x) __builtin_bswap32((x))
    #define bswap64(x) __builtin_bswap64((x))
#elif defined(__linux__) || defined(__GLIBC__)
    #include <byteswap.h>
    #define bswap16(x) bswap_16((x))
    #define bswap32(x) bswap_32((x))
    #define bswap64(x) bswap_64((x))
#elif defined(_MSC_VER)
    #include <stdlib.h>
    #define bswap16(x) _byteswap_ushort((x))
    #define bswap32(x) _byteswap_ulong((x))
    #define bswap64(x) _byteswap_uint64((x))
#elif defined(__APPLE__)
    #include <libkern/OSByteOrder.h>
    #define bswap16(x) OSSwapInt16((x))
    #define bswap32(x) OSSwapInt32((x))
    #define bswap64(x) OSSwapInt64((x))
#elif defined(__FreeBSD__) || defined(__NetBSD__)
    #include <sys/endian.h>  // This defines the intrinsics as per the chosen naming convention
#elif defined(__OpenBSD__)
    #include <sys/endian.h>
    #define bswap16(x) swap16((x))
    #define bswap32(x) swap32((x))
    #define bswap64(x) swap64((x))
#elif defined(__sun) || defined(sun)
    #include <sys/byteorder.h>
    #define bswap16(x) BSWAP_16((x))
    #define bswap32(x) BSWAP_32((x))
    #define bswap64(x) BSWAP_64((x))
#else
    static inline std::uint16_t bswap16(std::uint16_t x) {
        return (((x& std::uint16_t{0x00FF}) << 8) |
                ((x& std::uint16_t{0xFF00}) >> 8));
    }

    static inline std::uint32_t bswap32(std::uint32_t x) {
        return (((x& std::uint32_t{0x000000FF}) << 24) |
                ((x& std::uint32_t{0x0000FF00}) << 8) |
                ((x& std::uint32_t{0x00FF0000}) >> 8) |
                ((x& std::uint32_t{0xFF000000}) >> 24));
    }

    static inline std::uint64_t bswap64(std::uint64_t x) {
        return (((x& std::uint64_t{0x00000000000000FF}) << 56) |
                ((x& std::uint64_t{0x000000000000FF00}) << 40) |
                ((x& std::uint64_t{0x0000000000FF0000}) << 24) |
                ((x& std::uint64_t{0x00000000FF000000}) << 8) |
                ((x& std::uint64_t{0x000000FF00000000}) >> 8) |
                ((x& std::uint64_t{0x0000FF0000000000}) >> 24) |
                ((x& std::uint64_t{0x00FF000000000000}) >> 40) |
                ((x& std::uint64_t{0xFF00000000000000}) >> 56));
    }

#endif

// Target architecture specific ntoh and hton for all relevant sizes
// In case of big endian architectures this is a noop
#if defined(TARGET_LITTLE_ENDIAN)
    #define ntoh16(x) bswap16((x))
    #define hton16(x) bswap16((x))
    #define ntoh32(x) bswap32((x))
    #define hton32(x) bswap32((x))
    #define ntoh64(x) bswap64((x))
    #define hton64(x) bswap64((x))
#elif defined(TARGET_BIG_ENDIAN)
    #define ntoh16(x) (x)
    #define hton16(x) (x)
    #define ntoh32(x) (x)
    #define hton32(x) (x)
    #define ntoh64(x) (x)
    #define hton64(x) (x)
#else
    #error "Platform not supported, no byte swap functions defined."
#endif

inline std::uint8_t ntoh(std::uint8_t x) {
    return x;
}

inline std::uint16_t ntoh(std::uint16_t x) {
    return ntoh16(x);
}

inline std::uint32_t ntoh(std::uint32_t x) {
    return ntoh32(x);
}

inline std::uint64_t ntoh(std::uint64_t x) {
    return ntoh64(x);
}

inline std::uint8_t hton(std::uint8_t x) {
    return x;
}

inline std::uint16_t hton(std::uint16_t x) {
    return hton16(x);
}

inline std::uint32_t hton(std::uint32_t x) {
    return hton32(x);
}

inline std::uint64_t hton(std::uint64_t x) {
    return hton64(x);
}
