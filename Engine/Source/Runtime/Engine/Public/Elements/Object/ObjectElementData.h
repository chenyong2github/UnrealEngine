// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TypedElementData.h"

class UObject;

/**
 * Element data that represents an Object.
 */
struct ENGINE_API FObjectElementData
{
	UE_DECLARE_TYPED_ELEMENT_DATA_RTTI(FObjectElementData);

	UObject* Object = nullptr;
};
