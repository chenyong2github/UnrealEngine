// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGAttributeGetFromPointIndexElement.h"

#include "PCGData.h"
#include "PCGHelpers.h"
#include "PCGParamData.h"
#include "Data/PCGPointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Helpers/PCGSettingsHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGAttributeGetFromPointIndexElement)

#if WITH_EDITOR
FName UPCGAttributeGetFromPointIndexSettings::GetDefaultNodeName() const
{
	return TEXT("GetAttributeFromPointIndex");
}
#endif

TArray<FPCGPinProperties> UPCGAttributeGetFromPointIndexSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, /*bInAllowMultipleConnections=*/ false);
	PinProperties.Emplace(PCGPinConstants::DefaultParamsLabel, EPCGDataType::Param, /*bInAllowMultipleConnections=*/ false);

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGAttributeGetFromPointIndexSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGAttributeGetFromPointIndexConstants::OutputAttributeLabel, EPCGDataType::Param);
	PinProperties.Emplace(PCGAttributeGetFromPointIndexConstants::OutputPointLabel, EPCGDataType::Point);

	return PinProperties;
}

FPCGElementPtr UPCGAttributeGetFromPointIndexSettings::CreateElement() const
{
	return MakeShared<FPCGAttributeGetFromPointIndexElement>();
}

bool FPCGAttributeGetFromPointIndexElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGAttributeGetFromPointIndexElement::Execute);

	check(Context);

	const UPCGAttributeGetFromPointIndexSettings* Settings = Context->GetInputSettings<UPCGAttributeGetFromPointIndexSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

	if (Inputs.Num() != 1)
	{
		PCGE_LOG(Error, "Input pin doesn't have the right number of inputs.");
		return true;
	}

	const UPCGPointData* PointData = Cast<UPCGPointData>(Inputs[0].Data);

	if (!PointData)
	{
		PCGE_LOG(Error, "Input is not a point data.");
		return true;
	}

	const UPCGParamData* ParamData = Context->InputData.GetParams();

	const int32 Index = PCG_GET_OVERRIDEN_VALUE(Settings, Index, ParamData);

	if (Index < 0 || Index >= PointData->GetPoints().Num())
	{
		PCGE_LOG(Error, "Index is out of bounds. Index: %d; Number of Points: %d", Index, PointData->GetPoints().Num());
		return true;
	}

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
	const FPCGPoint& Point = PointData->GetPoints()[Index];

	// Only create a point if we are connected, or in editor with the component being inspected.
	if (Context->IsOutputConnectedOrInspecting(PCGAttributeGetFromPointIndexConstants::OutputPointLabel))
	{
		UPCGPointData* OutputPointData = NewObject<UPCGPointData>();
		OutputPointData->InitializeFromData(PointData);
		OutputPointData->GetMutablePoints().Add(Point);

		FPCGTaggedData& Output = Outputs.Emplace_GetRef();
		Output.Data = OutputPointData;
		Output.Pin = PCGAttributeGetFromPointIndexConstants::OutputPointLabel;
	}

	if (PointData->Metadata && PointData->Metadata->HasAttribute(Settings->InputAttributeName))
	{
		const FPCGMetadataAttributeBase* Attribute = PointData->Metadata->GetConstAttribute(Settings->InputAttributeName);
		UPCGParamData* OutputParamData = NewObject<UPCGParamData>();

		auto ExtractAttribute = [AttributeName = Settings->InputAttributeName, &Point, &OutputParamData, Attribute](auto DummyValue)
		{
			using AttributeType = decltype(DummyValue);

			const FPCGMetadataAttribute<AttributeType>* TypedAttribute = static_cast<const FPCGMetadataAttribute<AttributeType>*>(Attribute);

			FPCGMetadataAttributeBase* NewAttributeBase =
				OutputParamData->Metadata->CreateAttribute<AttributeType>(AttributeName, TypedAttribute->GetValueFromItemKey(Point.MetadataEntry), TypedAttribute->AllowsInterpolation(), /*bOverrideParent=*/false);

			check(NewAttributeBase);

			PCGMetadataEntryKey EntryKey = OutputParamData->Metadata->AddEntry();
			NewAttributeBase->SetValueFromValueKey(EntryKey, PCGDefaultValueKey);
		};

		PCGMetadataAttribute::CallbackWithRightType(Attribute->GetTypeId(), ExtractAttribute);

		FPCGTaggedData& Output = Outputs.Emplace_GetRef();
		Output.Data = OutputParamData;
		Output.Pin = PCGAttributeGetFromPointIndexConstants::OutputAttributeLabel;
	}

	return true;
}
