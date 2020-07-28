// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>

namespace trimd {

namespace scalar {

template<typename T>
class T128 {
    public:
        using value_type = typename std::remove_cv<T>::type;

    public:
        T128() : data{} {
        }

        T128(value_type v1, value_type v2, value_type v3, value_type v4) : data({v1, v2, v3, v4}) {
        }

        explicit T128(value_type value) : T128(value, value, value, value) {
        }

        static T128 fromAlignedSource(const value_type* source) {
            return T128{source[0], source[1], source[2], source[3]};
        }

        static T128 fromUnalignedSource(const value_type* source) {
            return T128::fromAlignedSource(source);
        }

        static T128 loadSingleValue(const value_type* source) {
            return T128{source[0], value_type{}, value_type{}, value_type{}};
        }

        template<typename U>
        static void prefetchT0(const U*  /*unused*/) {
            // Intentionally noop
        }

        template<typename U>
        static void prefetchT1(const U*  /*unused*/) {
            // Intentionally noop
        }

        template<typename U>
        static void prefetchT2(const U*  /*unused*/) {
            // Intentionally noop
        }

        template<typename U>
        static void prefetchNTA(const U*  /*unused*/) {
            // Intentionally noop
        }

        void alignedLoad(const value_type* source) {
            data[0] = source[0];
            data[1] = source[1];
            data[2] = source[2];
            data[3] = source[3];
        }

        void unalignedLoad(const value_type* source) {
            alignedLoad(source);
        }

        void alignedStore(value_type* dest) const {
            dest[0] = data[0];
            dest[1] = data[1];
            dest[2] = data[2];
            dest[3] = data[3];
        }

        void unalignedStore(value_type* dest) const {
            alignedStore(dest);
        }

        float sum() const {
            return data[0] + data[1] + data[2] + data[3];
        }

        T128& operator+=(const T128& rhs) {
            data[0] += rhs.data[0];
            data[1] += rhs.data[1];
            data[2] += rhs.data[2];
            data[3] += rhs.data[3];
            return *this;
        }

        T128& operator-=(const T128& rhs) {
            data[0] -= rhs.data[0];
            data[1] -= rhs.data[1];
            data[2] -= rhs.data[2];
            data[3] -= rhs.data[3];
            return *this;
        }

        T128& operator*=(const T128& rhs) {
            data[0] *= rhs.data[0];
            data[1] *= rhs.data[1];
            data[2] *= rhs.data[2];
            data[3] *= rhs.data[3];
            return *this;
        }

        T128& operator/=(const T128& rhs) {
            data[0] /= rhs.data[0];
            data[1] /= rhs.data[1];
            data[2] /= rhs.data[2];
            data[3] /= rhs.data[3];
            return *this;
        }

        static constexpr std::size_t size() {
            return sizeof(decltype(data)) / sizeof(float);
        }

        static constexpr std::size_t alignment() {
            #if defined(__arm__) || defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARM64)
                return std::alignment_of<std::max_align_t>::value;
            #else
                return sizeof(decltype(data));
            #endif
        }

    private:
        std::array<value_type, 4> data;
};

template<typename T>
inline T128<T> operator+(const T128<T>& lhs, const T128<T>& rhs) {
    return T128<T>(lhs) += rhs;
}

template<typename T>
inline T128<T> operator-(const T128<T>& lhs, const T128<T>& rhs) {
    return T128<T>(lhs) -= rhs;
}

template<typename T>
inline T128<T> operator*(const T128<T>& lhs, const T128<T>& rhs) {
    return T128<T>(lhs) *= rhs;
}

template<typename T>
inline T128<T> operator/(const T128<T>& lhs, const T128<T>& rhs) {
    return T128<T>(lhs) /= rhs;
}

template<typename T>
class T256 {
    public:
        using value_type = typename std::remove_cv<T>::type;

    public:
        T256() : data{} {
        }

        T256(value_type v1, value_type v2, value_type v3, value_type v4,
             value_type v5, value_type v6, value_type v7, value_type v8) : data({v1, v2, v3, v4, v5, v6, v7, v8}) {
        }

        explicit T256(value_type value) : T256(value, value, value, value, value, value, value, value) {
        }

        static T256 fromAlignedSource(const value_type* source) {
            return T256{source[0], source[1], source[2], source[3], source[4], source[5], source[6], source[7]};
        }

        static T256 fromUnalignedSource(const value_type* source) {
            return T256::fromAlignedSource(source);
        }

        static T256 loadSingleValue(const value_type* source) {
            return T256{source[0], value_type{}, value_type{}, value_type{}, value_type{}, value_type{}, value_type{}, value_type{}};
        }

        template<typename U>
        static void prefetchT0(const U*  /*unused*/) {
            // Intentionally noop
        }

        template<typename U>
        static void prefetchT1(const U*  /*unused*/) {
            // Intentionally noop
        }

        template<typename U>
        static void prefetchT2(const U*  /*unused*/) {
            // Intentionally noop
        }

        template<typename U>
        static void prefetchNTA(const U*  /*unused*/) {
            // Intentionally noop
        }

        void alignedLoad(const value_type* source) {
            data[0] = source[0];
            data[1] = source[1];
            data[2] = source[2];
            data[3] = source[3];
            data[4] = source[4];
            data[5] = source[5];
            data[6] = source[6];
            data[7] = source[7];
        }

        void unalignedLoad(const value_type* source) {
            alignedLoad(source);
        }

        void alignedStore(value_type* dest) const {
            dest[0] = data[0];
            dest[1] = data[1];
            dest[2] = data[2];
            dest[3] = data[3];
            dest[4] = data[4];
            dest[5] = data[5];
            dest[6] = data[6];
            dest[7] = data[7];
        }

        void unalignedStore(value_type* dest) const {
            alignedStore(dest);
        }

        float sum() const {
            return data[0] + data[1] + data[2] + data[3] + data[4] + data[5] + data[6] + data[7];
        }

        T256& operator+=(const T256& rhs) {
            data[0] += rhs.data[0];
            data[1] += rhs.data[1];
            data[2] += rhs.data[2];
            data[3] += rhs.data[3];
            data[4] += rhs.data[4];
            data[5] += rhs.data[5];
            data[6] += rhs.data[6];
            data[7] += rhs.data[7];
            return *this;
        }

        T256& operator-=(const T256& rhs) {
            data[0] -= rhs.data[0];
            data[1] -= rhs.data[1];
            data[2] -= rhs.data[2];
            data[3] -= rhs.data[3];
            data[4] -= rhs.data[4];
            data[5] -= rhs.data[5];
            data[6] -= rhs.data[6];
            data[7] -= rhs.data[7];
            return *this;
        }

        T256& operator*=(const T256& rhs) {
            data[0] *= rhs.data[0];
            data[1] *= rhs.data[1];
            data[2] *= rhs.data[2];
            data[3] *= rhs.data[3];
            data[4] *= rhs.data[4];
            data[5] *= rhs.data[5];
            data[6] *= rhs.data[6];
            data[7] *= rhs.data[7];
            return *this;
        }

        T256& operator/=(const T256& rhs) {
            data[0] /= rhs.data[0];
            data[1] /= rhs.data[1];
            data[2] /= rhs.data[2];
            data[3] /= rhs.data[3];
            data[4] /= rhs.data[4];
            data[5] /= rhs.data[5];
            data[6] /= rhs.data[6];
            data[7] /= rhs.data[7];
            return *this;
        }

        static constexpr std::size_t size() {
            return sizeof(decltype(data)) / sizeof(float);
        }

        static constexpr std::size_t alignment() {
            #if defined(__arm__) || defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARM64)
                return std::alignment_of<std::max_align_t>::value;
            #else
                return sizeof(decltype(data));
            #endif
        }

    private:
        std::array<value_type, 8> data;
};

template<typename T>
inline T256<T> operator+(const T256<T>& lhs, const T256<T>& rhs) {
    return T256<T>(lhs) += rhs;
}

template<typename T>
inline T256<T> operator-(const T256<T>& lhs, const T256<T>& rhs) {
    return T256<T>(lhs) -= rhs;
}

template<typename T>
inline T256<T> operator*(const T256<T>& lhs, const T256<T>& rhs) {
    return T256<T>(lhs) *= rhs;
}

template<typename T>
inline T256<T> operator/(const T256<T>& lhs, const T256<T>& rhs) {
    return T256<T>(lhs) /= rhs;
}

using F128 = T128<float>;
using F256 = T256<float>;

}  // namespace scalar

}  // namespace trimd
