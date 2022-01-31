// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"

#include "IOptimusValueProvider.generated.h"

UINTERFACE()
class UOptimusValueProvider :
	public UInterface
{
	GENERATED_BODY()
};


/** FIXME: A stop-gap shader value provider until we have a proper pin evaluation that handles
  * paths that have a constant, computed, varying and a mix thereof, results.
  */
class IOptimusValueProvider
{
	GENERATED_BODY()

public:
	// Returns the stored value as a shader-compatible value.
	virtual TArray<uint8> GetShaderValue() const = 0;
};
