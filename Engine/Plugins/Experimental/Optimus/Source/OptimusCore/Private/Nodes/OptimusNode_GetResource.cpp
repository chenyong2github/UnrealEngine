// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/OptimusNode_GetResource.h"

#include "OptimusResourceDescription.h"
#include "OptimusNodePin.h"
#include "DataInterfaces/DataInterfaceRawBuffer.h"


int32 UOptimusNode_GetResource::GetDataFunctionIndexFromPin(const UOptimusNodePin* InPin) const
{
	if (!InPin || InPin->GetParentPin() != nullptr)
	{
		return INDEX_NONE;
	}
	if (!ensure(GetPins().Contains(InPin)))
	{
		return INDEX_NONE;
	}

	return URawBufferDataInterface::ReadValueInputIndex;
}

void UOptimusNode_GetResource::ConstructNode()
{
	if (const UOptimusResourceDescription *Res = GetResourceDescription())
	{
		// FIXME: Define context.
		AddPinDirect(
			Res->ResourceName,
			EOptimusNodePinDirection::Output,
			FOptimusNodePinStorageConfig({Optimus::DomainName::Vertex}),
			Res->DataType);
	}
}
