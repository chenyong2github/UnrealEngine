// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifndef UE5_ENABLE_TESTHARNESS_ENGINE_SUPPORT
#define UE5_ENABLE_TESTHARNESS_ENGINE_SUPPORT 1
#endif

#if UE5_ENABLE_TESTHARNESS_ENGINE_SUPPORT
#include "CoreTypes.h"
#include "Containers/ContainersFwd.h"
#include "Containers/StringFwd.h"
#endif

#if defined(THIRD_PARTY_INCLUDES_START) && defined(THIRD_PARTY_INCLUDES_END)
THIRD_PARTY_INCLUDES_START
#endif // defined(THIRD_PARTY_INCLUDES_START) && defined(THIRD_PARTY_INCLUDES_END)

#ifdef _MSC_VER
#pragma pack(push, 8)
#pragma warning(push)
#pragma warning(disable: 4005) // 'identifier': macro redefinition
#pragma warning(disable: 4582) // 'type': constructor is not implicitly called
#pragma warning(disable: 4583) // 'type': destructor is not implicitly called
#endif
#include "catch.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#pragma pack(pop)
#endif

#if defined(THIRD_PARTY_INCLUDES_START) && defined(THIRD_PARTY_INCLUDES_END)
THIRD_PARTY_INCLUDES_END
#endif // defined(THIRD_PARTY_INCLUDES_START) && defined(THIRD_PARTY_INCLUDES_END)

#if UE5_ENABLE_TESTHARNESS_ENGINE_SUPPORT

enum class ESPMode : uint8;
template<class ObjectType, ESPMode InMode> class TSharedRef;
template<class ObjectType, ESPMode InMode> class TSharedPtr;
template <typename... Types> struct TTuple;

namespace UE::Private::LowLevelTestsRunner
{
	std::string CStringToStdString(const TCHAR* Value);
	std::string FStringToStdString(const FString& Value);
	std::string StringViewToStdString(const FStringView& Value);
}

// Tell Catch how to print FString
template <>
struct Catch::StringMaker<FString>
{
	static std::string convert(const FString& Value)
	{
		return UE::Private::LowLevelTestsRunner::FStringToStdString(Value);
	}
};

// Tell Catch how to print FStringView
template <>
struct Catch::StringMaker<FStringView>
{
	static std::string convert(const FStringView& Value)
	{
		return UE::Private::LowLevelTestsRunner::StringViewToStdString(Value);
	}
};

// Tell Catch how to print TCHAR strings
template <int SZ>
struct Catch::StringMaker<TCHAR[SZ]>
{
	static std::string convert(const TCHAR* Value)
	{
		return UE::Private::LowLevelTestsRunner::CStringToStdString(Value);
	}
};

// Tell Catch how to print TTuple<KeyType, ValueType>
template <typename KeyType, typename ValueType>
struct Catch::StringMaker<TTuple<KeyType, ValueType>>
{
	static std::string convert(const TTuple<KeyType, ValueType>& Value)
	{
		return "{ " + StringMaker<KeyType>::convert(Value.Key) + " , " + StringMaker<ValueType>::convert(Value.Value) + " }";
	}
};

// Tell Catch how to print TSharedRef
template<typename T, ESPMode Mode>
struct Catch::StringMaker<TSharedRef<T, Mode>>
{
	static std::string convert(const TSharedRef<T, Mode>& Value)
	{
		return StringMaker<const T*>::convert(&Value.Get());
	}
};

// Tell Catch how to print TSharedPtr
template<typename T, ESPMode Mode>
struct Catch::StringMaker<TSharedPtr<T, Mode>>
{
	static std::string convert(const TSharedPtr<T, Mode>& Value)
	{
		return StringMaker<const T*>::convert(Value.Get());
	}
};

template <typename KeyT, typename ValueT>
inline bool operator==(const TMap<KeyT, ValueT>& Left, const TMap<KeyT, ValueT>& Right)
{
	bool bIsEqual = Left.Num() == Right.Num();
	if (bIsEqual)
	{
		for (const auto& Pair : Left)
		{
			const ValueT* RightValue = Right.Find(Pair.Key);
			bIsEqual = bIsEqual && RightValue != nullptr && Pair.Value == *RightValue;
		}
	}
	return bIsEqual;
}

#endif // #if UE5_ENABLE_TESTHARNESS_ENGINE_SUPPORT
