// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include <cstddef>
#include <memory>

namespace VivoxClientApi
{
	void* Allocate(size_t n);
	void Deallocate(void* p);
	char* StrDup(const char* s);

	template <class T>
	class custom_allocator
	{
	public:
		using pointer = T*;
		using const_pointer = typename std::pointer_traits<pointer>::template rebind<const T>;
		using void_pointer = typename std::pointer_traits<pointer>::template rebind<void>;
		using const_void_pointer = typename std::pointer_traits<pointer>::template rebind<const void>;
		using value_type = T;
		using difference_type = typename std::pointer_traits<pointer>::difference_type;
		using size_type = typename std::make_unsigned<difference_type>::type;
		using reference = T&;
		using const_reference = const T&;

		template <class U>
		struct rebind { typedef custom_allocator<U> other; };

		custom_allocator() noexcept {}
		template <class U>
		custom_allocator(const custom_allocator<U>&) noexcept {}

		pointer allocate(std::size_t n)
		{
			return static_cast<pointer>(Allocate(n * sizeof(value_type)));
		}

		void deallocate(pointer p, std::size_t n) noexcept
		{
			Deallocate(p);
		}
	};

	template <class T, class U>
	bool operator==(const custom_allocator<T>&, const custom_allocator<U>&) noexcept
	{
		return true;
	}

	template <class T, class U>
	bool operator !=(const custom_allocator<T>& a, const custom_allocator<U>& b) noexcept
	{
		return !(a == b);
	}
}