// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode_GetResource.h"

#include "OptimusResourceDescription.h"
#include "OptimusNodePin.h"

void UOptimusNode_GetResource::CreatePins()
{
	UOptimusResourceDescription *Res = GetResourceDescription();
	if (Res)
	{
		// FIXME: Define context.
		AddPin(
			Res->ResourceName,
			EOptimusNodePinDirection::Output,
			FOptimusNodePinStorageConfig(1, TEXT("Vertex")),
			Res->DataType);
	}
}
