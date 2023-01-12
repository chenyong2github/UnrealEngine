// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGAttributeFilterElement.h"

#include "Data/PCGSpatialData.h"
#include "PCGParamData.h"
#include "PCGContext.h"
#include "PCGPin.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGAttributeFilterElement)

namespace PCGAttributeFilterConstants
{
	const FName NodeName = TEXT("FilterAttribute");
}

namespace PCGAttributeFilterSettings
{
	TArray<FString> GenerateNameArray(const FString& InString)
	{
		TArray<FString> Result;
		InString.ParseIntoArrayWS(Result, TEXT(","));
		return Result;
	}
}

void UPCGAttributeFilterSettings::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (!AttributesToKeep_DEPRECATED.IsEmpty())
	{
		SelectedAttributes.Empty();
		Operation = EPCGAttributeFilterOperation::KeepSelectedAttributes;
		// Can't use FString::Join since it is an array of FName
		for (int i = 0; i < AttributesToKeep_DEPRECATED.Num(); ++i)
		{
			if (i != 0)
			{
				SelectedAttributes += TEXT(",");
			}

			SelectedAttributes += AttributesToKeep_DEPRECATED[i].ToString();
		}

		AttributesToKeep_DEPRECATED.Empty();
	}
#endif // WITH_EDITOR
}

#if WITH_EDITOR
FName UPCGAttributeFilterSettings::GetDefaultNodeName() const
{
	return PCGAttributeFilterConstants::NodeName;
}
#endif

FName UPCGAttributeFilterSettings::AdditionalTaskName() const
{
	TArray<FString> AttributesToKeep = PCGAttributeFilterSettings::GenerateNameArray(SelectedAttributes);

	FString NodeName = PCGAttributeFilterConstants::NodeName.ToString();

	switch (Operation)
	{
	case EPCGAttributeFilterOperation::KeepSelectedAttributes:
		NodeName += TEXT(" (Keep)");
		break;
	case EPCGAttributeFilterOperation::DeleteSelectedAttributes:
		NodeName += TEXT(" (Delete)");
		break;
	}

	// If we filter only one attribute, show its name
	if (AttributesToKeep.Num() == 1)
	{
		return FName(FString::Printf(TEXT("%s: %s"), *NodeName, *AttributesToKeep[0]));
	}
	else
	{
		return FName(NodeName);
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

	const bool bAddAttributesFromParent = (Settings->Operation == EPCGAttributeFilterOperation::DeleteSelectedAttributes);

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
			NewSpatialData->Metadata->Initialize(ParentMetadata, bAddAttributesFromParent);

			// No need to inherit metadata since we already initialized it.
			NewSpatialData->InitializeFromData(InputSpatialData, /*InMetadataParentOverride=*/ nullptr, /*bInheritMetadata=*/ false);

			OutputData = NewSpatialData;
		}
		else if (const UPCGParamData* InputParamData = Cast<UPCGParamData>(InputData))
		{
			ParentMetadata = InputParamData->Metadata;

			UPCGParamData* NewParamData = NewObject<UPCGParamData>();
			Metadata = NewParamData->Metadata;

			Metadata->Initialize(InputParamData->Metadata, bAddAttributesFromParent);
			OutputData = NewParamData;
		}
		else
		{
			PCGE_LOG(Error, "Invalid data as input. Only support spatial and params");
			continue;
		}

		TArray<FString> AttributesToKeep = PCGAttributeFilterSettings::GenerateNameArray(Settings->SelectedAttributes);

		// Then add/remove each attribute in the list explicitly
		for (const FString& AttributeName : AttributesToKeep)
		{
			switch (Settings->Operation)
			{
			case EPCGAttributeFilterOperation::KeepSelectedAttributes:
				Metadata->AddAttribute(ParentMetadata, FName(AttributeName));
				break;
			case EPCGAttributeFilterOperation::DeleteSelectedAttributes:
				Metadata->DeleteAttribute(FName(AttributeName));
				break;
			}
		}

		TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
		FPCGTaggedData& Output = Outputs.Emplace_GetRef();
		Output.Data = OutputData;
	}

	return true;
}

