// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Metadata/PCGMetadataBooleanOpElement.h"

#include "PCGParamData.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataEntryKeyIterator.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"

FName UPCGMetadataBooleanSettings::GetInputPinLabel(uint32 Index) const
{
	switch (Index)
	{
	case 0:
		return (Operation == EPCGMedadataBooleanOperation::Not) ? PCGPinConstants::DefaultInputLabel : PCGMetadataSettingsBaseConstants::DoubleInputFirstLabel;
	case 1:
		return PCGMetadataSettingsBaseConstants::DoubleInputSecondLabel;
	default:
		return NAME_None;
	}
}

uint32 UPCGMetadataBooleanSettings::GetInputPinNum() const
{
	return (Operation == EPCGMedadataBooleanOperation::Not) ? 1 : 2;
}

bool UPCGMetadataBooleanSettings::IsSupportedInputType(uint16 TypeId, uint32 InputIndex, bool& bHasSpecialRequirement) const
{
	bHasSpecialRequirement = false;
	return TypeId == (uint16)EPCGMetadataTypes::Boolean;
}

FName UPCGMetadataBooleanSettings::GetInputAttributeNameWithOverride(uint32 Index, UPCGParamData* Params) const
{
	switch (Index)
	{
	case 0:
		return PCG_GET_OVERRIDEN_VALUE(this, Input1AttributeName, Params);
	case 1:
		return PCG_GET_OVERRIDEN_VALUE(this, Input2AttributeName, Params);
	default:
		return NAME_None;
	}
}

uint16 UPCGMetadataBooleanSettings::GetOutputType(uint16 InputTypeId) const
{
	return (uint16)EPCGMetadataTypes::Boolean;
}

#if WITH_EDITOR
FName UPCGMetadataBooleanSettings::GetDefaultNodeName() const
{
	if (const UEnum* EnumPtr = FindObject<UEnum>(nullptr, TEXT("EPCGMedadataBooleanOperation"), true))
	{ 
		return EnumPtr->GetNameByValue(static_cast<int>(Operation)); 
	}

	return TEXT("Metadata Boolean Node");
}
#endif // WITH_EDITOR

FPCGElementPtr UPCGMetadataBooleanSettings::CreateElement() const
{
	return MakeShared<FPCGMetadataBooleanElement>();
}

bool FPCGMetadataBooleanElement::DoOperation(FOperationData& OperationData) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGMetadataBooleanElement::Execute);

	const UPCGMetadataBooleanSettings* Settings = CastChecked<UPCGMetadataBooleanSettings>(OperationData.Settings);

	auto BoolFunc = [Settings](bool& Value1, const bool& Value2)
	{
		switch (Settings->Operation)
		{
		case EPCGMedadataBooleanOperation::And:
			Value1 = (Value1 && Value2);
			break;
		case EPCGMedadataBooleanOperation::Not:
			Value1 = !Value1;
			break;
		case EPCGMedadataBooleanOperation::Or:
			Value1 = (Value1 || Value2);
			break;
		case EPCGMedadataBooleanOperation::Xor:
			Value1 = (Value1 != Value2);
			break;
		}
	};

	// All values in OperationData.Iterators are UniquePtr. Dereference them here to make the syntax less cumbersome.
	// Also some iterators can be null, it means they need to use the first iterator (that should never be null).
	// We just need to make sure to not increment the first iterator multiple times in one loop.

	check(OperationData.Iterators[0]);

	IPCGMetadataEntryIterator& Iterator1 = *OperationData.Iterators[0];
	IPCGMetadataEntryIterator& Iterator2 = (OperationData.Iterators.Num() >= 2 && OperationData.Iterators[1]) ? *OperationData.Iterators[1] : Iterator1;

	bool bShouldIncrementIterator2 = &Iterator1 != &Iterator2;

	PCGMetadataEntryKey EntryKey1 = 0;
	PCGMetadataEntryKey EntryKey2 = 0;

	bool Value1 = false;
	bool Value2 = false;

	FPCGMetadataAttribute<bool>* OutputAttribute = static_cast<FPCGMetadataAttribute<bool>*>(OperationData.OutputAttribute);
	bool DefaultValue1 = PCGMetadataAttribute::GetValueWithBroadcast<bool>(OperationData.SourceAttributes[0], PCGInvalidEntryKey);
	bool DefaultValue2 = (Settings->Operation != EPCGMedadataBooleanOperation::Not) ? PCGMetadataAttribute::GetValueWithBroadcast<bool>(OperationData.SourceAttributes[1], PCGInvalidEntryKey) : false;
	BoolFunc(DefaultValue1, DefaultValue2);
	OutputAttribute->SetDefaultValue(DefaultValue1);

	for (int32 i = 0; i < OperationData.NumberOfElementsToProcess; ++i)
	{
		EntryKey1 = *Iterator1;

		// If the entry key is invalid, nothing to do
		if (EntryKey1 != PCGInvalidEntryKey)
		{
			Value1 = PCGMetadataAttribute::GetValueWithBroadcast<bool>(OperationData.SourceAttributes[0], EntryKey1);

			if (Settings->Operation != EPCGMedadataBooleanOperation::Not)
			{
				EntryKey2 = *Iterator2;
				Value2 = PCGMetadataAttribute::GetValueWithBroadcast<bool>(OperationData.SourceAttributes[1], EntryKey2);
			}

			BoolFunc(Value1, Value2);

			OutputAttribute->SetValue(EntryKey1, Value1);
		}

		++Iterator1;
		if (bShouldIncrementIterator2)
		{
			++Iterator2;
		}
	}

	return true;
}