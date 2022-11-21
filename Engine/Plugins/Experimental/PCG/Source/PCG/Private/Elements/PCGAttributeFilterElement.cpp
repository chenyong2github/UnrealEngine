// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGAttributeFilterElement.h"

#include "PCGData.h"
#include "PCGParamData.h"
#include "Data/PCGPointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGAttributeFilterElement)

namespace PCGAttributeFilterConstants
{
	const FName NodeName = TEXT("FilterAttribute");
}

#if WITH_EDITOR
FName UPCGAttributeFilterSettings::GetDefaultNodeName() const
{
	return PCGAttributeFilterConstants::NodeName;
}
#endif

FName UPCGAttributeFilterSettings::AdditionalTaskName() const
{
	// If we filter only one attribute, show its name
	if (AttributesToKeep.Num() == 1)
	{
		return FName(FString::Printf(TEXT("%s: %s"), *PCGAttributeFilterConstants::NodeName.ToString(), *AttributesToKeep[0].ToString()));
	}
	else
	{
		return NAME_None;
	}
}

TArray<FPCGPinProperties> UPCGAttributeFilterSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Any);

	return PinProperties;
}

FPCGElementPtr UPCGAttributeFilterSettings::CreateElement() const
{
	return MakeShared<FPCGAttributeFilterElement>();
}

bool FPCGAttributeFilterElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGAttributeFilterElement::Execute);

	check(Context);

	const UPCGAttributeFilterSettings* Settings = Context->GetInputSettings<UPCGAttributeFilterSettings>();

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

	for (const FPCGTaggedData& InputTaggedData : Inputs)
	{
		const UPCGData* InputData = InputTaggedData.Data;
		UPCGData* OutputData = nullptr;

		const UPCGMetadata* ParentMetadata = nullptr;
		UPCGMetadata* Metadata = nullptr;

		if (const UPCGSpatialData* InputSpatialData = Cast<UPCGSpatialData>(InputData))
		{
			ParentMetadata = InputSpatialData->Metadata;

			UPCGSpatialData* NewSpatialData = InputSpatialData->DuplicateData(/*bInitializeFromThisData=*/false);
			Metadata = NewSpatialData->Metadata;
			NewSpatialData->Metadata->Initialize(ParentMetadata, /*bAddAttributesFromParent=*/false);

			// No need to inherit metadata since we already initialized it.
			NewSpatialData->InitializeFromData(InputSpatialData, /*InMetadataParentOverride=*/ nullptr, /*bInheritMetadata=*/ false);

			OutputData = NewSpatialData;
		}
		else if (const UPCGParamData* InputParamData = Cast<UPCGParamData>(InputData))
		{
			ParentMetadata = InputParamData->Metadata;

			UPCGParamData* NewParamData = NewObject<UPCGParamData>();
			Metadata = NewParamData->Metadata;

			Metadata->Initialize(InputParamData->Metadata, /*bAddAttributesFromParent=*/false);
			OutputData = NewParamData;
		}
		else
		{
			PCGE_LOG(Error, "Invalid data as input. Only support spatial and params");
			continue;
		}

		// Then add each attribute in the list explicitly
		for (const FName& AttributeName : Settings->AttributesToKeep)
		{
			Metadata->AddAttribute(ParentMetadata, AttributeName);
		}

		TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
		FPCGTaggedData& Output = Outputs.Emplace_GetRef();
		Output.Data = OutputData;
	}

	return true;
}

