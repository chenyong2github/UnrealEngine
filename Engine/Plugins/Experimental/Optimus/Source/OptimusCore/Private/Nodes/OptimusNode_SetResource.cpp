// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/OptimusNode_SetResource.h"

#include "OptimusNodePin.h"
#include "OptimusResourceDescription.h"
#include "DataInterfaces/DataInterfaceRawBuffer.h"


int32 UOptimusNode_SetResource::GetDataFunctionIndexFromPin(const UOptimusNodePin* InPin) const
{
	if (!InPin || InPin->GetParentPin() != nullptr)
	{
		return INDEX_NONE;
	}
	if (!ensure(GetPins().Contains(InPin)))
	{
		return INDEX_NONE;
	}

	return URawBufferDataInterface::WriteValueOutputIndex;
}


void UOptimusNode_SetResource::ConstructNode()
{
	if (const UOptimusResourceDescription* Res = GetResourceDescription())
	{
		AddPinDirect(
		    Res->ResourceName,
		    EOptimusNodePinDirection::Input,
			FOptimusNodePinStorageConfig({Optimus::DomainName::Vertex}),
		    Res->DataType);
	}
}
