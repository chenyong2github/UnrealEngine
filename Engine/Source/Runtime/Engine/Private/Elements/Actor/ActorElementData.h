// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TypedElementData.h"

class AActor;

/**
 * Element data that represents an Actor.
 */
struct FActorElementData
{
	UE_DECLARE_TYPED_ELEMENT_DATA_RTTI(FActorElementData);

	AActor* Actor = nullptr;
};
