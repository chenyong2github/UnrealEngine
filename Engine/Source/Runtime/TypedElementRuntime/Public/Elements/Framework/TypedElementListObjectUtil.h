// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementList.h"

namespace TypedElementListObjectUtil
{

/**
 * Test whether there are any objects in the given list of elements.
 */
TYPEDELEMENTRUNTIME_API bool HasObjects(const UTypedElementList* InElementList, const UClass* InRequiredClass = nullptr);

/**
 * Test whether there are any objects in the given list of elements.
 */
template <typename RequiredClassType>
bool HasObjects(const UTypedElementList* InElementList)
{
	return HasObjects(InElementList, RequiredClassType::StaticClass());
}

/**
 * Count the number of objects in the given list of elements.
 */
TYPEDELEMENTRUNTIME_API int32 CountObjects(const UTypedElementList* InElementList, const UClass* InRequiredClass = nullptr);

/**
 * Count the number of objects in the given list of elements.
 */
template <typename RequiredClassType>
int32 CountObjects(const UTypedElementList* InElementList)
{
	return CountObjects(InElementList, RequiredClassType::StaticClass());
}

/**
 * Enumerate the objects from the given list of elements.
 * @note Return true from the callback to continue enumeration.
 */
TYPEDELEMENTRUNTIME_API void ForEachObject(const UTypedElementList* InElementList, TFunctionRef<bool(UObject*)> InCallback, const UClass* InRequiredClass = nullptr);

/**
 * Enumerate the objects from the given list of elements.
 * @note Return true from the callback to continue enumeration.
 */
template <typename RequiredClassType>
void ForEachObject(const UTypedElementList* InElementList, TFunctionRef<bool(RequiredClassType*)> InCallback)
{
	ForEachObject(InElementList, [&InCallback](UObject* InObject)
	{
		return InCallback(CastChecked<RequiredClassType>(InObject));
	}, RequiredClassType::StaticClass());
}

/**
 * Get the array of objects from the given list of elements.
 */
TYPEDELEMENTRUNTIME_API TArray<UObject*> GetObjects(const UTypedElementList* InElementList, const UClass* InRequiredClass = nullptr);

/**
 * Get the array of objects from the given list of elements.
 */
template <typename RequiredClassType>
TArray<RequiredClassType*> GetObjects(const UTypedElementList* InElementList)
{
	TArray<RequiredClassType*> SelectedObjects;
	SelectedObjects.Reserve(InElementList->Num());

	ForEachObject<RequiredClassType>(InElementList, [&SelectedObjects](RequiredClassType* InObject)
	{
		SelectedObjects.Add(InObject);
		return true;
	});

	return SelectedObjects;
}

/**
 * Get the first object of the given type from the given list of elements.
 */
TYPEDELEMENTRUNTIME_API UObject* GetTopObject(const UTypedElementList* InElementList, const UClass* InRequiredClass = nullptr);

/**
 * Get the first object of the given type from the given list of elements.
 */
template <typename RequiredClassType>
RequiredClassType* GetTopObject(const UTypedElementList* InElementList)
{
	return Cast<RequiredClassType>(GetTopObject(InElementList, RequiredClassType::StaticClass()));
}

/**
 * Get the last object of the given type from the given list of elements.
 */
TYPEDELEMENTRUNTIME_API UObject* GetBottomObject(const UTypedElementList* InElementList, const UClass* InRequiredClass = nullptr);

/**
 * Get the last object of the given type from the given list of elements.
 */
template <typename RequiredClassType>
RequiredClassType* GetBottomObject(const UTypedElementList* InElementList)
{
	return Cast<RequiredClassType>(GetBottomObject(InElementList, RequiredClassType::StaticClass()));
}

} // namespace TypedElementListObjectUtil
