// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>
#include "UObject/Class.h"

#ifndef WITH_STRUCTUTILS_DEBUG
#define WITH_STRUCTUTILS_DEBUG (!(UE_BUILD_SHIPPING || UE_BUILD_SHIPPING_WITH_EDITOR || UE_BUILD_TEST) && 1)
#endif // WITH_STRUCTUTILS_DEBUG

struct FSharedStruct;
struct FConstSharedStruct;
struct FStructView;
struct FConstStructView;
struct FInstancedStruct;
class FReferenceCollector;

namespace UE::StructUtils
{
	extern STRUCTUTILS_API uint32 GetStructCrc32(const UScriptStruct& ScriptStruct, const uint8* StructMemory, const uint32 CRC = 0);
	extern STRUCTUTILS_API uint32 GetStructCrc32(const FStructView& StructView, const uint32 CRC = 0);
	extern STRUCTUTILS_API uint32 GetStructCrc32(const FConstStructView& StructView, const uint32 CRC = 0);
	extern STRUCTUTILS_API uint32 GetStructCrc32(const FSharedStruct& SharedView, const uint32 CRC = 0);
	extern STRUCTUTILS_API uint32 GetStructCrc32(const FConstSharedStruct& SharedView, const uint32 CRC = 0);

	template <typename T>
	typename TEnableIf<!TIsDerivedFrom<T, UObject>::IsDerived, UScriptStruct*>::Type GetAsUStruct()
	{
		return T::StaticStruct();
	}

	template <typename T>
	typename TEnableIf<TIsDerivedFrom<T, UObject>::IsDerived, UClass*>::Type GetAsUStruct()
	{
		return T::StaticClass();
	}

	template<typename T>
	using EnableIfSharedInstancedOrViewStruct = std::enable_if_t<std::is_same<FStructView, T>::value
		|| std::is_same<FConstStructView, T>::value
		|| std::is_same<FSharedStruct, T>::value
		|| std::is_same<FConstSharedStruct, T>::value
		|| std::is_same<FInstancedStruct, T>::value, T>;

	template<typename T>
	using EnableIfNotSharedInstancedOrViewStruct = std::enable_if_t<!(std::is_same<FStructView, T>::value
		|| std::is_same<FConstStructView, T>::value
		|| std::is_same<FSharedStruct, T>::value
		|| std::is_same<FConstSharedStruct, T>::value
		|| std::is_same<FInstancedStruct, T>::value), T>;
}

/* Predicate useful to find a struct of a specific type in an container */
struct FStructTypeEqualOperator
{
	const UScriptStruct* TypePtr;

	FStructTypeEqualOperator(const UScriptStruct* InTypePtr) : TypePtr(InTypePtr) {}

	template <typename T, UE::StructUtils::EnableIfSharedInstancedOrViewStruct<T>* = nullptr>
	FStructTypeEqualOperator(const T& Struct) : TypePtr(Struct.GetScriptStruct()) {}

	template <typename T, UE::StructUtils::EnableIfSharedInstancedOrViewStruct<T>* = nullptr>
	bool operator()(const T& Struct) const { return Struct.GetScriptStruct() == TypePtr; }
};

struct FScriptStructSortOperator
{
	template <typename T>
	bool operator()(const T& A, const T& B) const
	{
		return (A.GetStructureSize() > B.GetStructureSize())
			|| (A.GetStructureSize() == B.GetStructureSize() && B.GetFName().FastLess(A.GetFName()));
	}
};

struct FStructTypeSortOperator
{
	template <typename T, UE::StructUtils::EnableIfSharedInstancedOrViewStruct<T>* = nullptr>
	bool operator()(const T& A, const T& B) const
	{
		const UScriptStruct* AScriptStruct = A.GetScriptStruct();
		const UScriptStruct* BScriptStruct = B.GetScriptStruct();
		if (!AScriptStruct)
		{
			return true;
		}
		else if (!BScriptStruct)
		{
			return false;
		}
		return FScriptStructSortOperator()(*AScriptStruct, *BScriptStruct);
	}
};
