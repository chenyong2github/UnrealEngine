// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementData.h"

class UActorComponent;

/**
 * Element data that represents an Actor Component.
 */
struct ENGINE_API FComponentElementData
{
	UE_DECLARE_TYPED_ELEMENT_DATA_RTTI(FComponentElementData);

	UActorComponent* Component = nullptr;
};
