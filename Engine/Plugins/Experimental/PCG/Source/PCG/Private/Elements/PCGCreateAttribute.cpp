// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGCreateAttribute.h"

#include "PCGData.h"
#include "PCGParamData.h"
#include "Data/PCGPointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"

#if WITH_EDITOR
FName UPCGCreateAttributeSettings::GetDefaultNodeName() const
{
	return TEXT("CreateAttribute");
}
#endif

TArray<FPCGPinProperties> UPCGCreateAttributeSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::Any, /*bInAllowMultipleConnections=*/ true);
	PinProperties.Emplace(PCGPinConstants::DefaultParamsLabel, EPCGDataType::Param, /*bInAllowMultipleConnections=*/ false);

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGCreateAttributeSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Any);

	return PinProperties;
}

FPCGElementPtr UPCGCreateAttributeSettings::CreateElement() const
{
	return MakeShared<FPCGCreateAttributeElement>();
}

bool FPCGCreateAttributeElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGCreateAttributeElement::Execute);

	check(Context);

	const UPCGCreateAttributeSettings* Settings = Context->GetInputSettings<UPCGCreateAttributeSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Params = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultParamsLabel);
	UPCGParamData* ParamData = nullptr;

	if (!Params.IsEmpty())
	{
		ParamData = CastChecked<UPCGParamData>(Params[0].Data);
	}

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

	if (Inputs.IsEmpty())
	{
		PCGE_LOG(Error, "No input connected");
		return true;
	}

	for (const FPCGTaggedData& InputTaggedData : Inputs)
	{
		const UPCGData* InputData = InputTaggedData.Data;

		const UPCGMetadata* ParentMetadata = nullptr;

		if (const UPCGPointData* InputPointData = Cast<UPCGPointData>(InputData))
		{
			ParentMetadata = InputPointData->Metadata;
		}
		else if (const UPCGParamData* InputParamData = Cast<UPCGParamData>(InputData))
		{
			ParentMetadata = InputParamData->Metadata;
		}
		else
		{
			PCGE_LOG(Error, "Invalid data as input. Only support points and params");
			return true;
		}

		UPCGMetadata* Metadata = NewObject<UPCGMetadata>();
		if (Settings->bKeepExistingAttributes)
		{
			Metadata->Initialize(ParentMetadata);
		}

		FPCGMetadataAttributeBase* Attribute = Settings->ClearOrCreateAttribute(Metadata, ParamData);

		if (!Attribute)
		{
			PCGE_LOG(Error, "Error while creating attribute %s", *Settings->OutputAttributeName.ToString());
			return true;
		}

		TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
		FPCGTaggedData& Output = Outputs.Emplace_GetRef();

		if (const UPCGPointData* InputPointData = Cast<UPCGPointData>(InputData))
		{
			UPCGPointData* PointOutputData = NewObject<UPCGPointData>();
			PointOutputData->Metadata = Metadata;
			PointOutputData->InitializeFromData(InputPointData);

			// Copy the points
			TArray<FPCGPoint>& Points = PointOutputData->GetMutablePoints();
			Points = InputPointData->GetPoints();

			Settings->SetAttribute(Attribute, Metadata, Points, ParamData);

			Output.Data = PointOutputData;
		}
		else if (const UPCGParamData* InputParamData = Cast<UPCGParamData>(InputData))
		{
			UPCGParamData* OutputParamData = NewObject<UPCGParamData>();
			OutputParamData->Metadata = Metadata;

			PCGMetadataEntryKey EntryKey = Metadata->AddEntry();
			Settings->SetAttribute(Attribute, Metadata, EntryKey, ParamData);

			Output.Data = OutputParamData;
		}
	}

	return true;
}