// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Metadata/PCGMetadataOpElementBase.h"

#include "PCGParamData.h"
#include "Data/PCGSpatialData.h"
#include "Data/PCGPointData.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataEntryKeyIterator.h"

TArray<FPCGPinProperties> UPCGMetadataSettingsBase::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	for (uint32 i = 0; i < GetInputPinNum(); ++i)
	{
		const FName PinLabel = GetInputPinLabel(i);
		if (PinLabel != NAME_None)
		{
			PinProperties.Emplace(PinLabel, EPCGDataType::Any, /*bAllowMultipleConnections=*/false);
		}
	}

	PinProperties.Emplace(PCGPinConstants::DefaultParamsLabel, EPCGDataType::Param, /*bAllowMultipleConnections=*/false);

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGMetadataSettingsBase::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Any);

	return PinProperties;
}

bool UPCGMetadataSettingsBase::IsMoreComplexType(uint16 FirstType, uint16 SecondType) const
{
	return FirstType != SecondType && FirstType <= (uint16)(EPCGMetadataTypes::Count) && SecondType <= (uint16)(EPCGMetadataTypes::Count) && PCG::Private::BroadcastableTypes[SecondType][FirstType];
}

bool FPCGMetadataElementBase::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGMetadataElementBase::Execute);

	const UPCGMetadataSettingsBase* Settings = Context->GetInputSettings<UPCGMetadataSettingsBase>();
	check(Settings);

	const TArray<FPCGTaggedData>& Inputs = Context->InputData.TaggedData;
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	// Nothing to do if we have no output
	if (!Context->Node || !Context->Node->IsOutputPinConnected(PCGPinConstants::DefaultOutputLabel))
	{
		return true;
	}

	const TArray<FPCGTaggedData>& ParamData = Context->InputData.GetParamsByPin(PCGPinConstants::DefaultParamsLabel);
	UPCGParamData* Params = ParamData.IsEmpty() ? nullptr : Cast<UPCGParamData>(ParamData[0].Data);

	FName OutputAttributeName = PCG_GET_OVERRIDEN_VALUE(Settings, OutputAttributeName, Params);

	const uint32 NumberOfInputs = Settings->GetInputPinNum();

	// Gathering all the inputs metadata
	TArray<const UPCGMetadata*> SourceMetadata;
	TArray<const UPCGData*> Data;
	SourceMetadata.SetNum(NumberOfInputs);
	Data.SetNum(NumberOfInputs);

	for (uint32 i = 0; i < NumberOfInputs; ++i)
	{
		TArray<FPCGTaggedData> InputData = Context->InputData.GetInputsByPin(Settings->GetInputPinLabel(i));
		if (InputData.IsEmpty())
		{
			PCGE_LOG(Error, "Invalid inputs for pin %d", i);
			return true;
		}

		// By construction, there can only be one of then(hence the 0 index)
		check(InputData.Num() == 1);
		Data[i] = InputData[0].Data;

		// Only gather Spacial and Params input. 
		if (const UPCGSpatialData* SpatialInput = Cast<const UPCGSpatialData>(Data[i]))
		{
			SourceMetadata[i] = SpatialInput->Metadata;
		}
		else if (const UPCGParamData* ParamsInput = Cast<const UPCGParamData>(Data[i]))
		{
			SourceMetadata[i] = ParamsInput->Metadata;
		}
		else
		{
			PCGE_LOG(Error, "Invalid inputs for pin %d", i);
			return true;
		}
	}

	FOperationData OperationData;
	OperationData.Iterators.SetNum(NumberOfInputs);
	TArray<int32> NumberOfElements;
	NumberOfElements.SetNum(NumberOfInputs);

	OperationData.SourceAttributes.SetNum(NumberOfInputs);

	OperationData.MostComplexInputType = (uint16)EPCGMetadataTypes::Unknown;
	OperationData.NumberOfElementsToProcess = -1;

	for (uint32 i = 0; i < NumberOfInputs; ++i)
	{
		FName SourceAttributeName = Settings->GetInputAttributeNameWithOverride(i, Params);
		if (SourceAttributeName == NAME_None)
		{
			SourceAttributeName = SourceMetadata[i]->GetSingleAttributeNameOrNone();
		}

		OperationData.SourceAttributes[i] = SourceMetadata[i]->GetConstAttribute(SourceAttributeName);

		if (OperationData.SourceAttributes[i] == nullptr)
		{
			PCGE_LOG(Error, "Attribute %s does not exist for input %d", *SourceAttributeName.ToString(), i);
			return true;
		}

		// Then verify that the type is OK
		bool bHasSpecialRequirement = false;
		if (!Settings->IsSupportedInputType(OperationData.SourceAttributes[i]->GetTypeId(), i, bHasSpecialRequirement))
		{
			PCGE_LOG(Error, "Attribute %s is not a supported type for input %d", *SourceAttributeName.ToString(), i);
			return true;
		}

		if (!bHasSpecialRequirement)
		{
			// In this case, check if we have a more complex type, or if we can broadcast to the most complex type.
			if (OperationData.MostComplexInputType == (uint16)EPCGMetadataTypes::Unknown || Settings->IsMoreComplexType(OperationData.SourceAttributes[i]->GetTypeId(), OperationData.MostComplexInputType))
			{
				OperationData.MostComplexInputType = OperationData.SourceAttributes[i]->GetTypeId();
			}
			else if (OperationData.MostComplexInputType != OperationData.SourceAttributes[i]->GetTypeId() && !PCG::Private::IsBroadcastable(OperationData.SourceAttributes[i]->GetTypeId(), OperationData.MostComplexInputType))
			{
				PCGE_LOG(Error, "Attribute %s cannot be broadcasted to match types for input %d", *SourceAttributeName.ToString(), i);
				return true;
			}
		}

		// Finally check that we have the right number of inputs, depending on the source
		if (const UPCGPointData* PointData = Cast<UPCGPointData>(Data[i]))
		{
			NumberOfElements[i] = PointData->GetPoints().Num();

			// If we are not the first input, we only have an iterator if we have a single point and we broadcast.
			bool bShouldBroadcast = i != 0 && NumberOfElements[i] == 1 && Settings->Mode == EPCGMetadataSettingsBaseMode::Broadcast;
			if (i == 0 || NumberOfElements[i] == NumberOfElements[0] || bShouldBroadcast)
			{
				OperationData.Iterators[i] = MakeUnique<FPCGMetadataEntryPointIterator>(PointData, bShouldBroadcast);
			}
		}
		else
		{
			NumberOfElements[i] = OperationData.SourceAttributes[i]->GetNumberOfEntriesWithParents();

			// No element means only dealing with the default value.
			if (NumberOfElements[i] == 0)
			{
				OperationData.Iterators[i] = MakeUnique<FPCGMetadataEntryConstantIterator>(PCGInvalidEntryKey, /*bRepeat=*/ true);
			}
			else
			{
				// Broadcast only if we have a single element and we are in broadcast mode, or inferred mode and we are param data.
				bool bShouldBroadcast = i != 0 && 
					NumberOfElements[i] == 1 && 
					(Settings->Mode == EPCGMetadataSettingsBaseMode::Broadcast || (Settings->Mode == EPCGMetadataSettingsBaseMode::Inferred && Data[i]->IsA<UPCGParamData>()));

				if (i == 0 || NumberOfElements[i] == NumberOfElements[0] || bShouldBroadcast)
				{
					OperationData.Iterators[i] = MakeUnique<FPCGMetadataEntryAttributeIterator>(*OperationData.SourceAttributes[i], bShouldBroadcast);
				}
			}
		}

		if (OperationData.NumberOfElementsToProcess == -1)
		{
			OperationData.NumberOfElementsToProcess = NumberOfElements[i];
		}
	}

	// At this point, we verified everything, so we can go forward with the computation, depending on the most complex type
	// So first forward outputs and create the attribute
	OperationData.OutputAttribute = nullptr;
	auto CreateAttribute = [&SourceAttributes = OperationData.SourceAttributes, &Inputs, &Outputs, &SourceMetadata, OutputAttributeName, &OutputAttribute = OperationData.OutputAttribute](auto DummyOutValue) -> void
	{
		using AttributeType = decltype(DummyOutValue);

		AttributeType DefaultValue = PCGMetadataAttribute::GetValueWithBroadcast<AttributeType>(SourceAttributes[0], PCGDefaultValueKey);

		FPCGTaggedData& OutputData = Outputs.Add_GetRef(Inputs[0]);
		OutputData.Pin = PCGPinConstants::DefaultOutputLabel;

		UPCGMetadata* OutMetadata = nullptr;

		PCGMetadataElementCommon::DuplicateTaggedData(Inputs[0], OutputData, OutMetadata);
		OutputAttribute = PCGMetadataElementCommon::ClearOrCreateAttribute(OutMetadata, OutputAttributeName, DefaultValue);
		PCGMetadataElementCommon::CopyEntryToValueKeyMap(SourceMetadata[0], SourceAttributes[0], OutputAttribute);
	};

	OperationData.OutputType = Settings->GetOutputType(OperationData.MostComplexInputType);

	PCGMetadataAttribute::CallbackWithRightType(OperationData.OutputType, CreateAttribute);

	if (OperationData.OutputAttribute == nullptr)
	{
		PCGE_LOG(Error, "Error while creating output attribute");
		Outputs.Empty();
		return true;
	}

	OperationData.Settings = Settings;

	return DoOperation(OperationData);
}