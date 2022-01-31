// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"

#include "IOptimusDataInterfaceProvider.generated.h"

UINTERFACE()
class UOptimusDataInterfaceProvider :
	public UInterface
{
	GENERATED_BODY()
};


class IOptimusDataInterfaceProvider
{
	GENERATED_BODY()

public:
	/**
	 * Returns the data interface class that should be generated from the node that implements
	 * this interface.
	 */
	virtual UClass *GetDataInterfaceClass() const = 0;
};
