// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Metadata/PCGMetadataRenameElement.h"

#include "Data/PCGSpatialData.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "PCGContext.h"
#include "PCGPin.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGMetadataRenameElement)

TArray<FPCGPinProperties> UPCGMetadataRenameSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::Any, /*bInAllowMultipleConnections=*/ true);
	PinProperties.Emplace(PCGPinConstants::DefaultParamsLabel, EPCGDataType::Param, /*bInAllowMultipleConnections=*/ false);

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGMetadataRenameSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Any);

	return PinProperties;
}

FPCGElementPtr UPCGMetadataRenameSettings::CreateElement() const
{
	return MakeShared<FPCGMetadataRenameElement>();
}

bool FPCGMetadataRenameElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGMetadataOperationElement::Execute);

	const UPCGMetadataRenameSettings* Settings = Context->GetInputSettings<UPCGMetadataRenameSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	TArray<FPCGTaggedData> InputParams = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultParamsLabel);
	const UPCGParamData* Params = InputParams.IsEmpty() ? nullptr : Cast<const UPCGParamData>(InputParams[0].Data);

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
	
	const FName AttributeToRename = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGMetadataRenameSettings, AttributeToRename), Settings->AttributeToRename, Params);
	const FName NewAttributeName = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGMetadataRenameSettings, NewAttributeName), Settings->NewAttributeName, Params);

	if (NewAttributeName == NAME_None)
	{
		UE_LOG(LogPCG, Warning, TEXT("Metadata rename operation cannot rename from %s to %s"), *AttributeToRename.ToString(), *NewAttributeName.ToString());
		// Bypass
		Context->OutputData = Context->InputData;
		return true;
	}

	for (const FPCGTaggedData& Input : Inputs)
	{
		FPCGTaggedData& Output = Outputs.Add_GetRef(Input);

		const UPCGMetadata* Metadata = nullptr;

		if (const UPCGSpatialData* SpatialInput = Cast<const UPCGSpatialData>(Input.Data))
		{
			Metadata = SpatialInput->Metadata;
			Output.Data = SpatialInput;
		}
		else if (const UPCGParamData* ParamInput = Cast<UPCGParamData>(Input.Data))
		{
			Metadata = ParamInput->Metadata;
			Output.Data = ParamInput;
		}

		if (!Metadata)
		{
			UE_LOG(LogPCG, Warning, TEXT("Input is not supported, only supports spatial and params"));
			Output.Data = nullptr;
			continue;
		}

		const FName LocalAttributeToRename = ((AttributeToRename != NAME_None) ? AttributeToRename : Metadata->GetLatestAttributeNameOrNone());

		if (!Metadata->HasAttribute(LocalAttributeToRename))
		{
			continue;
		}

		UPCGMetadata* NewMetadata = nullptr;
		PCGMetadataElementCommon::DuplicateTaggedData(Input, Output, NewMetadata);

		if (!NewMetadata || !NewMetadata->RenameAttribute(LocalAttributeToRename, NewAttributeName))
		{
			UE_LOG(LogPCG, Warning, TEXT("Failed to rename attribute from %s to %s"), *LocalAttributeToRename.ToString(), *NewAttributeName.ToString());
		}
	}

	// Pass-through settings
	Outputs.Append(Context->InputData.GetAllSettings());

	return true;
}
