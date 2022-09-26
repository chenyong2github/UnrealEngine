// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MutableRuntime/Private/Platform.h"
#include "MutableRuntime/Public/MutableMemory.h"

#include <unordered_map>
#include <map>
#include <set>


namespace mu
{

    //!
	MUTABLERUNTIME_API void mutable_memlog();


    //! Custom memory allocator to be used by STL continers inside the library.
    //! It uses the functions from the memory hooks
    template <class T>
    class MemAlloc
    {
    public:

        // type definitions
        using value_type = T;
        using pointer = T*;
        using const_pointer = const T*;
        using reference = T&;
        using const_reference = const T&;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;

        // constructors and destructor
        // - nothing to do because the allocator has no state
        MemAlloc() noexcept = default;

        template <class U>
        MemAlloc( const MemAlloc<U>& ) noexcept
        {
        }

        // allocate but don't initialize num elements of type T
        pointer allocate (size_type num, const void* = nullptr)
        {
            auto ret = (pointer)(mutable_malloc(num*sizeof(T)));
            return ret;
        }

        // deallocate storage p of deleted elements
        void deallocate (pointer p, size_type)
        {
			mutable_free((void*)p);
        }
    };

    //! Return that all specializations of this allocator are interchangeable
    template<class T1, class T2>
    bool operator==( const MemAlloc<T1>&, const MemAlloc<T2>& ) noexcept
    {
        return true;
    }

    template<class T1, class T2>
    bool operator!=( const MemAlloc<T1>& a, const MemAlloc<T2>& b ) noexcept
    {
        return !(a==b);
    }


    //! STL-like containers using this allocator
    template< typename T >
    using vector = std::vector<T, MemAlloc<T> >;

    template< typename T >
    using basic_string = std::basic_string<T, std::char_traits<T>, MemAlloc<T> >;

    using string = std::basic_string<char, std::char_traits<char>, MemAlloc<char> >;

    template< typename K, typename T >
    using map = std::map< K, T, std::less<K>, MemAlloc< std::pair<const K,T> > >;

    template< typename T >
    using set = std::set< T, std::less<T>, MemAlloc<T> >;

    template< typename T >
    using multiset = std::multiset< T, std::less<T>, MemAlloc<T> >;

    template< typename K, typename T >
    using unordered_map = std::unordered_map< K, T, std::hash<K>, std::equal_to<K>, MemAlloc< std::pair<const K,T> > >;

    template< typename K, typename T >
    using pair = std::pair<K, T>;

}


#ifdef __GNUC__
#include <string>
namespace std
{
    // This is necessary for GCC, but it may be inefficient, causing additional string copies.
    // In theory it can be avoided in C++0x17 with the use of string_view
    template<>
    struct hash<mu::string>
    {
        std::size_t operator()(mu::string const& s) const noexcept
        {
            return std::hash<std::basic_string<char, std::char_traits<char>>>{}(std::basic_string<char, std::char_traits<char>>(s.c_str()));
        }
    };
}
#endif //#ifdef __GNUC__
