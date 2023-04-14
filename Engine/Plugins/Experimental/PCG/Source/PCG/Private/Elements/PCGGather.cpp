// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGGather.h"

#include "PCGContext.h"
#include "PCGPin.h"
	
EPCGDataType UPCGGatherSettings::GetCurrentPinTypes(const UPCGPin* InPin) const
{
	// All pins narrow to input type
	EPCGDataType InputTypeUnion = GetTypeUnionOfIncidentEdges(PCGPinConstants::DefaultInputLabel);
	return InputTypeUnion != EPCGDataType::None ? InputTypeUnion : EPCGDataType::Any;
}

TArray<FPCGPinProperties> UPCGGatherSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::Any);

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGGatherSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Any);

	return PinProperties;
}

FPCGElementPtr UPCGGatherSettings::CreateElement() const
{
	return MakeShared<FPCGGatherElement>();
}

bool FPCGGatherElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGatherElement::Execute);

	Context->OutputData = Context->InputData;

	return true;
}
