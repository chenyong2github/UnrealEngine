// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode_SetResource.h"

#include "OptimusNodePin.h"
#include "OptimusResourceDescription.h"


void UOptimusNode_SetResource::CreatePins()
{
	UOptimusResourceDescription* Res = GetResourceDescription();
	if (Res)
	{
		CreatePinFromDataType(
		    Res->ResourceName,
		    EOptimusNodePinDirection::Input,
		    EOptimusNodePinStorageType::Resource,
		    Res->DataType);
	}
}
