// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGGather.h"

#include "PCGContext.h"
#include "PCGPin.h"

#define LOCTEXT_NAMESPACE "PCGGatherElement"

EPCGDataType UPCGGatherSettings::GetCurrentPinTypes(const UPCGPin* InPin) const
{
	check(InPin);

	if (InPin->Properties.Label == PCGPinConstants::DefaultDependencyOnlyLabel)
	{
		return Super::GetCurrentPinTypes(InPin);
	}
	else
	{
		const EPCGDataType InputTypeUnion = GetTypeUnionOfIncidentEdges(PCGPinConstants::DefaultInputLabel);
		return InputTypeUnion != EPCGDataType::None ? InputTypeUnion : EPCGDataType::Any;
	}
}

TArray<FPCGPinProperties> UPCGGatherSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::Any);
	PinProperties.Emplace(PCGPinConstants::DefaultDependencyOnlyLabel, EPCGDataType::Any, /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true, LOCTEXT("DependencyPinTooltip", "Data passed to this pin will be used to order execution but will otherwise not contribute to the results of this node."));

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

	TArray<FPCGTaggedData> GatheredData = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	if (GatheredData.Num() == Context->InputData.TaggedData.Num())
	{
		Context->OutputData = Context->InputData;
	}
	else
	{
		Context->OutputData.TaggedData = MoveTemp(GatheredData);
	}

	for(FPCGTaggedData& TaggedData : Context->OutputData.TaggedData)
	{
		TaggedData.Pin = PCGPinConstants::DefaultOutputLabel;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE