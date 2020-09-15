// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode_GetResource.h"

#include "OptimusResourceDescription.h"
#include "OptimusNodePin.h"

void UOptimusNode_GetResource::CreatePins()
{
	UOptimusResourceDescription *Res = GetResourceDescription();
	if (Res)
	{
		CreatePinFromDataType(
			Res->ResourceName,
			EOptimusNodePinDirection::Output,
			EOptimusNodePinStorageType::Resource,
			Res->DataType);
	}
}
