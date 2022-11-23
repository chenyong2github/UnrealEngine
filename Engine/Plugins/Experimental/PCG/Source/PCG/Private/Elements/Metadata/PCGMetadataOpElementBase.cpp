// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Metadata/PCGMetadataOpElementBase.h"

#include "PCGParamData.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSpatialData.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "Metadata/PCGMetadata.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGMetadataOpElementBase)

void UPCGMetadataSettingsBase::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (OutputAttributeName_DEPRECATED != NAME_None)
	{
		OutputTarget.Selection = EPCGAttributePropertySelection::Attribute;
		OutputTarget.AttributeName = OutputAttributeName_DEPRECATED;
		OutputAttributeName_DEPRECATED = NAME_None;
	}
#endif // WITH_EDITOR
}

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

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGMetadataSettingsBase::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	
	for (uint32 i = 0; i < GetOutputPinNum(); ++i)
	{
		const FName PinLabel = GetOutputPinLabel(i);
		if (PinLabel != NAME_None)
		{
			PinProperties.Emplace(PinLabel, EPCGDataType::Any);
		}
	}

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

	const uint32 NumberOfInputs = Settings->GetInputPinNum();
	const uint32 NumberOfOutputs = Settings->GetOutputPinNum();

	check(NumberOfInputs > 0);
	check(NumberOfOutputs <= UPCGMetadataSettingsBase::MaxNumberOfOutputs);

	const TArray<FPCGTaggedData>& Inputs = Context->InputData.TaggedData;
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	// Gathering all the inputs metadata
	TArray<const UPCGMetadata*> SourceMetadata;
	TArray<FPCGTaggedData> InputTaggedData;
	SourceMetadata.SetNum(NumberOfInputs);
	InputTaggedData.SetNum(NumberOfInputs);

	for (uint32 i = 0; i < NumberOfInputs; ++i)
	{
		TArray<FPCGTaggedData> InputData = Context->InputData.GetInputsByPin(Settings->GetInputPinLabel(i));
		if (InputData.Num() != 1)
		{
			PCGE_LOG(Error, "Invalid inputs for pin %d", i);
			return true;
		}

		// By construction, there can only be one of then(hence the 0 index)
		InputTaggedData[i] = MoveTemp(InputData[0]);

		// Only gather Spacial and Params input. 
		if (const UPCGSpatialData* SpatialInput = Cast<const UPCGSpatialData>(InputTaggedData[i].Data))
		{
			SourceMetadata[i] = SpatialInput->Metadata;
		}
		else if (const UPCGParamData* ParamsInput = Cast<const UPCGParamData>(InputTaggedData[i].Data))
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
	OperationData.InputIterators.SetNum(NumberOfInputs);
	TArray<int32> NumberOfElements;
	NumberOfElements.SetNum(NumberOfInputs);

	OperationData.MostComplexInputType = (uint16)EPCGMetadataTypes::Unknown;
	OperationData.NumberOfElementsToProcess = -1;

	for (uint32 i = 0; i < NumberOfInputs; ++i)
	{
		// First we verify if the input data match the first one.
		if (i != 0 &&
			InputTaggedData[0].Data->GetClass() != InputTaggedData[i].Data->GetClass() &&
			!InputTaggedData[i].Data->IsA<UPCGParamData>())
		{
			PCGE_LOG(Error, "Input %d is not of the same type than input 0 and is not a param data. Not supported.", i);
			return true;
		}

		FPCGAttributePropertySelector InputSource = Settings->GetInputSource(i);

		if (InputSource.Selection == EPCGAttributePropertySelection::Attribute && InputSource.AttributeName == NAME_None)
		{
			InputSource.AttributeName = SourceMetadata[i]->GetLatestAttributeNameOrNone();
		}

		OperationData.InputIterators[i] = PCGMetadataAttributeWrapper::CreateIteratorWrapper(InputTaggedData[i].Data, InputSource);

		if (!OperationData.InputIterators[i].IsValid())
		{
			PCGE_LOG(Error, "Attribute/Property %s does not exist for input %d", *InputSource.GetName().ToString(), i);
			return true;
		}

		uint16 AttributeTypeId = OperationData.InputIterators[i].GetType();

		// Then verify that the type is OK
		bool bHasSpecialRequirement = false;
		if (!Settings->IsSupportedInputType(AttributeTypeId, i, bHasSpecialRequirement))
		{
			PCGE_LOG(Error, "Attribute/Property %s is not a supported type for input %d", *InputSource.GetName().ToString(), i);
			return true;
		}

		if (!bHasSpecialRequirement)
		{
			// In this case, check if we have a more complex type, or if we can broadcast to the most complex type.
			if (OperationData.MostComplexInputType == (uint16)EPCGMetadataTypes::Unknown || Settings->IsMoreComplexType(AttributeTypeId, OperationData.MostComplexInputType))
			{
				OperationData.MostComplexInputType = AttributeTypeId;
			}
			else if (OperationData.MostComplexInputType != AttributeTypeId && !PCG::Private::IsBroadcastable(AttributeTypeId, OperationData.MostComplexInputType))
			{
				PCGE_LOG(Error, "Attribute %s cannot be broadcasted to match types for input %d", *InputSource.GetName().ToString(), i);
				return true;
			}
		}

		NumberOfElements[i] = OperationData.InputIterators[i].Num();

		if (OperationData.NumberOfElementsToProcess == -1)
		{
			OperationData.NumberOfElementsToProcess = NumberOfElements[i];
		}

		// Verify that the number of elements makes sense
		if (OperationData.NumberOfElementsToProcess % NumberOfElements[i] != 0)
		{
			PCGE_LOG(Error, "Mismatch between the number of elements in input 0 (%d) and in input %d (%d).", OperationData.NumberOfElementsToProcess, i, NumberOfElements[i]);
			return true;
		}
	}

	// At this point, we verified everything, so we can go forward with the computation, depending on the most complex type
	// So first forward outputs and create the attribute
	OperationData.OutputIterators.SetNum(Settings->GetOutputPinNum());

	FPCGAttributePropertySelector OutputTarget = Settings->OutputTarget;
	if (OutputTarget.Selection == EPCGAttributePropertySelection::Attribute && OutputTarget.AttributeName == NAME_None)
	{
		OutputTarget.AttributeName = OperationData.InputIterators[0].GetWrapper().GetName();
	}

	auto CreateAttribute = [&](uint32 OutputIndex, auto DummyOutValue) -> bool
	{
		using AttributeType = decltype(DummyOutValue);

		FPCGTaggedData& OutputData = Outputs.Add_GetRef(InputTaggedData[0]);
		OutputData.Pin = Settings->GetOutputPinLabel(OutputIndex);

		UPCGMetadata* OutMetadata = nullptr;

		FName OutputName = OutputTarget.GetName();

		bool bShouldClearOrCreateAttribute = false;
		bool bShouldCopyEntryMap = false;

		if (UPCGPointData* PointData = Cast<UPCGPointData>(InputTaggedData[0].Data))
		{
			int16 PropertyType = -1;
			if (PCGMetadataAttributeWrapper::IsPropertyWithType(InputTaggedData[0].Data, OutputName, &PropertyType))
			{
				// We matched a property, check if the output type is valid
				if (!PCG::Private::IsBroadcastable(PCG::Private::MetadataTypes<AttributeType>::Id, PropertyType))
				{
					PCGE_LOG(Error, "Property %s cannot be broadcasted to match types for input", *OutputName.ToString());
					return false;
				}
			}
			else
			{
				// Otherwise it is an attribute, we will create a clean one, so output type will match.
				bShouldClearOrCreateAttribute = true;
			}
		}
		else
		{
			// If it is not a point data, we will only deal with attributes, and the entry map will be necessary.
			bShouldClearOrCreateAttribute = true;
			bShouldCopyEntryMap = true;
		}

		PCGMetadataElementCommon::DuplicateTaggedData(InputTaggedData[0], OutputData, OutMetadata);
		if (bShouldClearOrCreateAttribute)
		{
			AttributeType DefaultValue{};
			OperationData.InputIterators[0].GetWrapper().Get<AttributeType>(PCGInvalidEntryKey, DefaultValue);

			FPCGMetadataAttributeBase* OutputAttribute = PCGMetadataElementCommon::ClearOrCreateAttribute(OutMetadata, OutputName, DefaultValue);

			if (!OutputAttribute)
			{
				return false;
			}

			if (bShouldCopyEntryMap)
			{
				PCGMetadataElementCommon::CopyEntryToValueKeyMap(SourceMetadata[0], OperationData.InputIterators[0].GetWrapper().GetAttribute(), OutputAttribute);
			}
		}

		// Cast to use the non-const version of this method.
		OperationData.OutputIterators[OutputIndex] = PCGMetadataAttributeWrapper::CreateIteratorWrapper(Cast<UPCGData>(OutputData.Data), OutputTarget);

		return OperationData.OutputIterators[OutputIndex].IsValid();
	};

	auto CreateAllSameAttributes = [&](auto DummyOutValue) -> bool
	{
		for (uint32 i = 0; i < NumberOfOutputs; ++i)
		{
			if (!CreateAttribute(i, DummyOutValue))
			{
				return false;
			}
		}

		return true;
	};

	OperationData.OutputType = Settings->GetOutputType(OperationData.MostComplexInputType);

	bool bCreateAttributeSucceeded = true;

	if (!Settings->HasDifferentOutputTypes())
	{
		bCreateAttributeSucceeded = PCGMetadataAttribute::CallbackWithRightType(OperationData.OutputType, CreateAllSameAttributes);
	}
	else
	{
		TArray<uint16> OutputTypes = Settings->GetAllOutputTypes();
		check(OutputTypes.Num() == NumberOfOutputs);

		for (uint32 i = 0; i < NumberOfOutputs && bCreateAttributeSucceeded; ++i)
		{
			bCreateAttributeSucceeded &= PCGMetadataAttribute::CallbackWithRightType(OutputTypes[i],
				[&](auto DummyOutValue) -> bool {
					return CreateAttribute(i, DummyOutValue);
				});
		}
	}

	if (!bCreateAttributeSucceeded)
	{
		PCGE_LOG(Error, "Error while creating output attributes");
		Outputs.Empty();
		return true;
	}

	OperationData.Settings = Settings;

	if (!DoOperation(OperationData))
	{
		PCGE_LOG(Error, "Error while performing the metadata operation, check logs for more information");
		Outputs.Empty();
	}

	return true;
}

