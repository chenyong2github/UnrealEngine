// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4365 4987)
#endif
#include <cstdint>
#include <type_traits>
#include <utility>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

static_assert(sizeof(char) == 1ul, "Unsupported platform, char is not 8-bits wide.");

namespace terse {

namespace traits {

template<typename ...>
struct sink {
    using type = void;
};

template<typename T, typename = void>
struct needs_allocator : std::false_type {};

template<typename T>
struct needs_allocator<T, typename sink<typename T::allocator_type,
                                        decltype(std::declval<T>().get_allocator())>::type> : std::true_type {};

template<class>
struct true_sink : std::true_type {};

template<class T>
static auto test_serialize(std::int32_t)->true_sink<decltype(std::declval<T>().serialize(std::declval<T&>()))>;

template<class>
static auto test_serialize(std::uint32_t)->std::false_type;

template<class T>
struct has_serialize : decltype(test_serialize<T>(0)) {};

template<class T>
static auto test_load(std::int32_t)->true_sink<decltype(std::declval<T>().load(std::declval<T&>()))>;

template<class>
static auto test_load(std::uint32_t)->std::false_type;

template<class T>
struct has_load : decltype(test_load<T>(0)) {};

template<class T>
static auto test_save(std::int32_t)->true_sink<decltype(std::declval<T>().save(std::declval<T&>()))>;

template<class>
static auto test_save(std::uint32_t)->std::false_type;

template<class T>
struct has_save : decltype(test_save<T>(0)) {};

template<typename TContainer>
using is_batchable = std::is_scalar<typename TContainer::value_type>;

template<typename TContainer>
struct has_wide_elements {
    static constexpr bool value = (sizeof(typename TContainer::value_type) > 1ul);
};

template<typename T>
struct is_pair : public std::false_type {};

template<typename K, typename V>
struct is_pair<std::pair<K, V> > : public std::true_type {};

template<typename T>
struct is_tuple : public std::false_type {};

template<typename K, typename V>
struct is_tuple<std::tuple<K, V> > : public std::true_type {};

}  // namespace traits

}  // namespace terse
