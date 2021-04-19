// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class FProperty;
class FNumericProperty;
class UObject;

class LEVELSNAPSHOTS_API FPropertyInfoHelpers
{
public:

	static FProperty* GetParentProperty(const FProperty* Property);

	/* Returns true if the property is a struct, map, array, set, etc */
	static bool IsPropertyContainer(const FProperty* Property);
	/* Returns true if the property is a set, array, map, etc but NOT a struct */
	static bool IsPropertyCollection(const FProperty* Property);

	/* Is the property an element of a struct, set, array, map, etc */
	static bool IsPropertyInContainer(const FProperty* Property);
	/* Is the property an element of a set, array, map, etc */
	static bool IsPropertyInCollection(const FProperty* Property);

	static bool IsPropertyInStruct(const FProperty* Property);

	static bool IsPropertyInMap(const FProperty* Property);

	/* A quick property flag check. Assumes the property flags are properly set/deserialized */
	static bool IsPropertyComponentFast(const FProperty* Property);

	/* A quick property flag check. Assumes the property flags are properly set/deserialized */
	static bool IsPropertySubObject(const FProperty* Property);

	static bool AreNumericPropertiesNearlyEqual(const FNumericProperty* Property, const void* ValuePtrA, const void* ValuePtrB);
	
};
