// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifndef UE5_ENABLE_TESTHARNESS_ENGINE_SUPPORT
#define UE5_ENABLE_TESTHARNESS_ENGINE_SUPPORT 1
#endif

#if UE5_ENABLE_TESTHARNESS_ENGINE_SUPPORT
#include "CoreMinimal.h"
#include "Containers/StringConv.h"
#endif

#if defined(THIRD_PARTY_INCLUDES_START) && defined(THIRD_PARTY_INCLUDES_END)
THIRD_PARTY_INCLUDES_START
#endif // defined(THIRD_PARTY_INCLUDES_START) && defined(THIRD_PARTY_INCLUDES_END)

#ifdef _MSC_VER
#pragma warning(disable: 4005 4582 4583) // 4005 is a macro redefinition. It's for "TEXT" and harmless in this case. 4582 and 4583 shouldn't be enabled by the engine in the first place.
#pragma pack(push, 8)
#endif
#include "catch.hpp"
#ifdef _MSC_VER
#pragma pack(pop)
#endif

#include <stdio.h>
#include <atomic>

#if defined(THIRD_PARTY_INCLUDES_START) && defined(THIRD_PARTY_INCLUDES_END)
THIRD_PARTY_INCLUDES_END
#endif // defined(THIRD_PARTY_INCLUDES_START) && defined(THIRD_PARTY_INCLUDES_END)


#if UE5_ENABLE_TESTHARNESS_ENGINE_SUPPORT

inline std::string FStringToStdString(const FString& Value)
{
	static std::string quote(R"(")");
	return quote + std::string(TCHAR_TO_UTF8(*Value)) + quote;
}

inline FDateTime TestDateTime(const TCHAR* DateString)
{
	FDateTime Output;
	REQUIRE(FDateTime::ParseIso8601(DateString, Output));
	return Output;
}


namespace Catch
{
	// Tell Catch how to print FStrings
	template <>
	class StringMaker<FString>
	{
	public:
		static std::string convert(const FString& Value) { return FStringToStdString(Value);  }
	};

	// Tell Catch how to print TCHAR strings
	template <int SZ>
	class StringMaker<TCHAR[SZ]>
	{
	public:
		static std::string convert(const TCHAR* Value) { return FStringToStdString(FString((int32)SZ, Value)); }
	};

	// Tell Catch how to print TPairs
	template <typename PairKeyType, typename PairValueType>
	class StringMaker<TPair<PairKeyType, PairValueType>>
	{
	public:
		static std::string convert(const TPair<PairKeyType, PairValueType>& Value)
		{
			return "{ " + StringMaker<PairKeyType>::convert(Value.Key) + " , " + StringMaker<PairValueType>::convert(Value.Value) + " }";
		}
	};

	// Tell Catch how to print TSharedRef
	template<typename T, ESPMode Mode>
	class StringMaker<TSharedRef<T, Mode>>
	{
	public:
		static std::string convert(const TSharedRef<T, Mode>& Value)
		{
			return "0x" + std::string(TCHAR_TO_ANSI(*FString::Printf(TEXT("%p"), &Value.Get())));
		}
	};

	// Tell Catch how to print TSharedPtr
	template<typename T, ESPMode Mode>
	class StringMaker<TSharedPtr<T, Mode>>
	{
	public:
		static std::string convert(const TSharedPtr<T, Mode>& Value)
		{
			return "0x" + std::string(TCHAR_TO_ANSI(*FString::Printf(TEXT("%p"), Value.Get())));
		}
	};
}


template <typename KeyT, typename ValueT>
static bool operator==(const TMap<KeyT, ValueT>& Left, const TMap<KeyT, ValueT>& Right)
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