// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/UnrealString.h"
#include "UObject/NameTypes.h"
#include <sstream>
#include <type_traits>

namespace CQTestConvert
{
	using PrintStream = std::ostringstream;
}

namespace
{
	template <typename T, typename... Args>
	class THasToString
	{
		template <typename C, typename = decltype(std::declval<C>().ToString(std::declval<Args>()...))>
		static std::true_type test(int);
		template<typename C>
		static std::false_type test(...);

	public:
		static constexpr bool value = decltype(test<T>(0))::value;
	};

	template<typename T>
	class THasOStream
	{
		template <typename C, typename = decltype(std::declval<CQTestConvert::PrintStream&>().operator<<(std::declval<C>()))>
		static std::true_type test(int);
		template <typename C, typename = decltype(operator<<(std::declval<CQTestConvert::PrintStream&>(), std::declval<C>()))>
		static std::true_type test(char);
		template<typename C>
		static std::false_type test(...);

	public:
		static constexpr bool value = decltype(test<T>(0))::value;
	};

	static_assert(THasToString<FName>::value, "FName should have ToString");
	static_assert(THasOStream<int>::value, "int should have an OStream operator");
	static_assert(THasOStream<int32>::value, "int32 should have an OStream operator");

	struct SomeTestStruct
	{
	};

	static_assert(!THasToString<SomeTestStruct>::value, "Struct without ToString should not have ToString");
	static_assert(!THasOStream<SomeTestStruct>::value, "Struct without OStream operator should not have OStream");
} // namespace


namespace CQTestConvert
{
	template <typename T>
	inline FString ToString(const T& Input)
	{
		if constexpr (THasToString<T>::value)
		{
			return Input.ToString();
		}
		else if constexpr (THasOStream<T>::value)
		{
			PrintStream stream;
			stream << Input;
			return FString(stream.str().c_str(), stream.str().length());
		}
		else if constexpr (std::is_enum_v<T>)
		{
			return ToString(static_cast<std::underlying_type_t<T>>(Input));
		}
		else
		{
			ensureMsgf(false, TEXT("Did not find ToString, ostream operator, or CQTestConvert::ToString template specialization for provided type. Cast provided type to something else, or see CqTestConvertTests.cpp for examples"));
			return TEXT("value");
		}
	}

	template <>
	inline FString ToString(const FString& Input)
	{
		return Input;
	}

	template<typename TElement, typename TAllocator>
	inline FString ToString(const TArray<TElement, TAllocator>& Input)
	{
		FString result = FString::JoinBy(Input, TEXT(", "), [](const TElement& e) {
			return CQTestConvert::ToString(e);
		});
		return TEXT("[") + result + TEXT("]");
	}

	template <typename TElement, typename TAllocator>
	inline FString ToString(const TSet<TElement, TAllocator>& Input)
	{
		FString result = FString::JoinBy(Input, TEXT(", "), [](const TElement& e) {
			return CQTestConvert::ToString(e);
		});
		return TEXT("[") + result + TEXT("]");
	}

	template<typename TKey, typename TValue>
	inline FString ToString(const TMap<TKey, TValue>& Input)
	{
		return FString::JoinBy(Input, TEXT(", "), [](const auto& kvp) {
			return TEXT("{") + CQTestConvert::ToString(kvp.Key) + TEXT(":") + CQTestConvert::ToString(kvp.Value) + TEXT("}");
		});
	}
}
