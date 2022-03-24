// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifndef UE_ENABLE_TESTHARNESS_ENGINE_SUPPORT
#define UE_ENABLE_TESTHARNESS_ENGINE_SUPPORT 1
#endif

#if UE_ENABLE_TESTHARNESS_ENGINE_SUPPORT
#include "CoreTypes.h"
#include "Containers/ContainersFwd.h"
#include "Containers/StringFwd.h"

#if PLATFORM_MAC
// Fix type redefinition error of FVector
#include "HAL/PlatformMath.h"
#endif // PLATFORM_MAC

#include <ostream>

std::ostream& operator<<(std::ostream& Stream, const TCHAR* Value);
std::ostream& operator<<(std::ostream& Stream, const FString& Value);
std::ostream& operator<<(std::ostream& Stream, const FAnsiStringView& Value);
std::ostream& operator<<(std::ostream& Stream, const FWideStringView& Value);
std::ostream& operator<<(std::ostream& Stream, const FUtf8StringView& Value);
std::ostream& operator<<(std::ostream& Stream, const FAnsiStringBuilderBase& Value);
std::ostream& operator<<(std::ostream& Stream, const FWideStringBuilderBase& Value);
std::ostream& operator<<(std::ostream& Stream, const FUtf8StringBuilderBase& Value);

enum class ESPMode : uint8;
template <class ObjectType, ESPMode InMode> class TSharedRef;
template <class ObjectType, ESPMode InMode> class TSharedPtr;

template <typename ObjectType, ESPMode Mode>
std::ostream& operator<<(std::ostream& Stream, const TSharedRef<ObjectType, Mode>& Value)
{
	return Stream << &Value.Get();
}

template <typename ObjectType, ESPMode Mode>
std::ostream& operator<<(std::ostream& Stream, const TSharedPtr<ObjectType, Mode>& Value)
{
	return Stream << Value.Get();
}

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

// Tell Catch how to print TTuple<KeyType, ValueType>
template <typename... Types> struct TTuple;

template <typename KeyType, typename ValueType>
struct Catch::StringMaker<TTuple<KeyType, ValueType>>
{
	static std::string convert(const TTuple<KeyType, ValueType>& Value)
	{
		return "{ " + StringMaker<KeyType>::convert(Value.Key) + " , " + StringMaker<ValueType>::convert(Value.Value) + " }";
	}
};

// Tests if true and prints message otherwise
#define TestTrue(a, b) \
	INFO((a)); \
	CHECK((b) == true); \

// Quiet version of TestTrue
#define TestTrue_Q(a) \
	CHECK((a) == true); \

// Tests if false and prints message otherwise
#define TestFalse(a, b) \
	INFO((a)); \
	CHECK((b) != true); \

// Quiet version of TestFalse
#define TestFalse_Q(a) \
	CHECK((a) != true); \

// Tests if b == c and prints message otherwise
#define TestEqual(a, b, c) \
	INFO((a)); \
	CHECK((b) == (c)); \

#define TestEqual_Conditional(a, b, c) \
	INFO((a)); \
	CHECKED_IF((b) == (c)) \

// Quiet version of TestEqual
#define TestEqual_Q(a, b) \
	CHECK((a) == (b)); \

#define TestNotEqual(a, b, c) \
	INFO((a)); \
	CHECK((b) != (c)); \

#define TestNull(a, b) \
	INFO((a)); \
	CHECK((b) == nullptr); \

#define TestNotNull(a, b) \
	INFO((a)); \
	CHECK((b) != nullptr); \

#define TestValid(a, b) \
	INFO((a)); \
	CHECK((b).IsValid() == true); \

#define TestInvalid(a, b) \
	INFO((a)); \
	CHECK((b).IsValid() != true); \

#define TestEqualString(a, b, c) \
	INFO((a)); \
	CHECK(FCString::Strcmp(ToCStr((b)), ToCStr((c))) == 0); \

#define TestAddInfo(a) \
	INFO((a)); \

#define TestAddWarning(a) \
	WARN((a)); \

#define TestAddError(a) \
	FAIL_CHECK((a)); \

#endif // UE_ENABLE_TESTHARNESS_ENGINE_SUPPORT