// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TypedElementData.h"

class UActorComponent;

/**
 * Element data that represents an Actor Component.
 */
struct FComponentElementData
{
	UE_DECLARE_TYPED_ELEMENT_DATA_RTTI(FComponentElementData);

	UActorComponent* Component = nullptr;
};
