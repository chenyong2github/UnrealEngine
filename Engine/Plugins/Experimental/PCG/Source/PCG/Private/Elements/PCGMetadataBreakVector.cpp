// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGMetadataBreakVector.h"

#include "PCGParamData.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

namespace PCGMetadataBreakVectorElement {
	void DuplicateTaggedData(const FPCGTaggedData& InTaggedData, FPCGTaggedData& OutTaggedData, UPCGMetadata*& OutMetadata)
	{
		if (const UPCGSpatialData* SpatialInput = Cast<const UPCGSpatialData>(InTaggedData.Data))
		{
			UPCGSpatialData* NewSpatialData = Cast<UPCGSpatialData>(StaticDuplicateObject(SpatialInput, const_cast<UPCGSpatialData*>(SpatialInput), FName()));
			NewSpatialData->InitializeFromData(SpatialInput);
			OutTaggedData.Data = NewSpatialData;

			OutMetadata = NewSpatialData->Metadata;
		}
		else if (const UPCGParamData* ParamsInput = Cast<const UPCGParamData>(InTaggedData.Data))
		{
			UPCGParamData* NewParamData = Cast<UPCGParamData>(StaticDuplicateObject(ParamsInput, const_cast<UPCGParamData*>(ParamsInput), FName()));
			OutTaggedData.Data = NewParamData;

			NewParamData->Metadata->Initialize(ParamsInput->Metadata);
			OutMetadata = NewParamData->Metadata;
		}
	}

	/** Creates a new double attribute, or clears the attribute if it already exists and is  a 'double' type */
	void ClearOrCreateDoubleAttribute(UPCGMetadata* Metadata, const FName& DestinationAttribute, double DefaultValue, FPCGMetadataAttribute<double>*& OutAttribute)
	{
		if (!Metadata)
		{
			UE_LOG(LogPCG, Error, TEXT("Failed to create metadata"));
			return;
		}

		if (Metadata->HasAttribute(DestinationAttribute))
		{
			UE_LOG(LogPCG, Warning, TEXT("Attribute %s already exists and has been overwritten"), *DestinationAttribute.ToString());

			if (Metadata->GetConstAttribute(DestinationAttribute)->GetTypeId() == PCG::Private::MetadataTypes<double>::Id)
			{
				// TODO: If DestinationAttribute is an existing double attribute, it will retain its old DefaultValue instead of using the given DefaultValue
				Metadata->ClearAttribute(DestinationAttribute);
			}
			else
			{
				Metadata->DeleteAttribute(DestinationAttribute);
				Metadata->CreateDoubleAttribute(DestinationAttribute, DefaultValue, /*bAllowsInterpolation=*/true, /*bOverrideParent=*/false);
			}
		}
		else
		{
			Metadata->CreateDoubleAttribute(DestinationAttribute, DefaultValue, /*bAllowsInterpolation=*/true, /*bOverrideParent=*/false);
		}

		OutAttribute = static_cast<FPCGMetadataAttribute<double>*>(Metadata->GetMutableAttribute(DestinationAttribute));
	}

	/** Copies the entry to value key relationship stored in the given Metadata, including its parents */
	void CopyEntryToValueKeyMap(const UPCGMetadata* MetadataToCopy, const FPCGMetadataAttributeBase* AttributeToCopy, FPCGMetadataAttributeBase* OutAttribute)
	{
		if (!OutAttribute)
		{
			UE_LOG(LogPCG, Error, TEXT("Failed to create output attribute"));
			return;
		}

		const PCGMetadataEntryKey EntryKeyCount = MetadataToCopy->GetItemCountForChild();
		for (PCGMetadataEntryKey EntryKey = 0; EntryKey < EntryKeyCount; ++EntryKey)
		{
			const PCGMetadataValueKey ValueKey = AttributeToCopy->GetValueKey(EntryKey);
			OutAttribute->SetValueFromValueKey(EntryKey, ValueKey);
		}
	}
}

TArray<FPCGPinProperties> UPCGMetadataBreakVectorSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGMetadataBreakVectorConstants::ParamsLabel, EPCGDataType::Param);
	PinProperties.Emplace(PCGMetadataBreakVectorConstants::SourceLabel, EPCGDataType::Any);

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGMetadataBreakVectorSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGMetadataBreakVectorConstants::XLabel, EPCGDataType::Any);
	PinProperties.Emplace(PCGMetadataBreakVectorConstants::YLabel, EPCGDataType::Any);
	PinProperties.Emplace(PCGMetadataBreakVectorConstants::ZLabel, EPCGDataType::Any);
	PinProperties.Emplace(PCGMetadataBreakVectorConstants::WLabel, EPCGDataType::Any);

	return PinProperties;
}

FPCGElementPtr UPCGMetadataBreakVectorSettings::CreateElement() const
{
	return MakeShared<FPCGMetadataBreakVectorElement>();
}

bool FPCGMetadataBreakVectorElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGMetadataBreakVectorElement::Execute);

	const UPCGMetadataBreakVectorSettings* Settings = Context->GetInputSettings<UPCGMetadataBreakVectorSettings>();
	check(Settings);

	const TArray<FPCGTaggedData>& Inputs = Context->InputData.TaggedData;
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	const TArray<FPCGTaggedData>& ParamData = Context->InputData.GetParamsByPin(PCGMetadataBreakVectorConstants::ParamsLabel);
	UPCGParamData* Params = nullptr;

	// TODO: this only captures the first Param
	for (const FPCGTaggedData& TaggedDatum : ParamData)
	{
		if (UPCGParamData* ParamDatum = Cast<UPCGParamData>(TaggedDatum.Data))
		{
			Params = ParamDatum;
			break;
		}
	}

	const FName SourceAttributeName = PCG_GET_OVERRIDEN_VALUE(Settings, SourceAttributeName, Params);

	// These exist to facilitate unit testing of the MetadataBreakVector element in isolation, when it otherwise would not have any pin connections to generate output
	bool bXPinConnected = Settings->bForceConnectX;
	bool bYPinConnected = Settings->bForceConnectY;
	bool bZPinConnected = Settings->bForceConnectZ;
	bool bWPinConnected = Settings->bForceConnectW;

	if (Context->Node)
	{
		bXPinConnected |= Context->Node->IsOutputPinConnected(PCGMetadataBreakVectorConstants::XLabel);
		bYPinConnected |= Context->Node->IsOutputPinConnected(PCGMetadataBreakVectorConstants::YLabel);
		bZPinConnected |= Context->Node->IsOutputPinConnected(PCGMetadataBreakVectorConstants::ZLabel);
		bWPinConnected |= Context->Node->IsOutputPinConnected(PCGMetadataBreakVectorConstants::WLabel);
	}

	for (const FPCGTaggedData& Input : Inputs)
	{
		if (!Input.Data)
		{
			PCGE_LOG(Error, "Unable to get data from input");
			continue;
		}

		UPCGMetadata* SourceMetadata = nullptr;
		if (const UPCGSpatialData* SpatialInput = Cast<const UPCGSpatialData>(Input.Data))
		{
			SourceMetadata = SpatialInput->Metadata;
		}
		else if (const UPCGParamData* ParamsInput = Cast<const UPCGParamData>(Input.Data))
		{
			SourceMetadata = ParamsInput->Metadata;
		}
		else // If the Data type does not handle Metadata, forward it into the valid pins
		{
			if (bXPinConnected)
			{
				FPCGTaggedData& Data = Outputs.Add_GetRef(Input);
				Data.Pin = PCGMetadataBreakVectorConstants::XLabel;
			}

			if (bYPinConnected)
			{
				FPCGTaggedData& Data = Outputs.Add_GetRef(Input);
				Data.Pin = PCGMetadataBreakVectorConstants::YLabel;
			}

			if (bZPinConnected)
			{
				FPCGTaggedData& Data = Outputs.Add_GetRef(Input);
				Data.Pin = PCGMetadataBreakVectorConstants::ZLabel;
			}

			if (bWPinConnected)
			{
				FPCGTaggedData& Data = Outputs.Add_GetRef(Input);
				Data.Pin = PCGMetadataBreakVectorConstants::WLabel;
			}

			continue;
		}

		if (!SourceMetadata)
		{
			PCGE_LOG(Warning, "Invalid metadata");
			continue;
		}

		const FName LocalSourceAttributeName = (SourceAttributeName == NAME_None) ? SourceMetadata->GetSingleAttributeNameOrNone() : SourceAttributeName;
		FName DestinationAttributeForX = NAME_None;
		FName DestinationAttributeForY = NAME_None;
		FName DestinationAttributeForZ = NAME_None;
		FName DestinationAttributeForW = NAME_None;

		if (LocalSourceAttributeName != NAME_None)
		{
			DestinationAttributeForX = FName(LocalSourceAttributeName.ToString() + ".X");
			DestinationAttributeForY = FName(LocalSourceAttributeName.ToString() + ".Y");
			DestinationAttributeForZ = FName(LocalSourceAttributeName.ToString() + ".Z");
			DestinationAttributeForW = FName(LocalSourceAttributeName.ToString() + ".W");
		}

		const FPCGMetadataAttributeBase* SourceAttribute = SourceMetadata->GetConstAttribute(LocalSourceAttributeName);
		if (!SourceAttribute)
		{
			PCGE_LOG(Error, "Attribute %s does not exist", *LocalSourceAttributeName.ToString());
			continue;
		}

		if (SourceAttribute->GetTypeId() != PCG::Private::MetadataTypes<FVector>::Id 
			&& SourceAttribute->GetTypeId() != PCG::Private::MetadataTypes<FVector4>::Id
			&& SourceAttribute->GetTypeId() != PCG::Private::MetadataTypes<FRotator>::Id)
		{
			PCGE_LOG(Error, "Attribute %s is not a breakable type", *LocalSourceAttributeName.ToString());
			continue;
		}

		FVector4 DefaultValue;

		if (SourceAttribute->GetTypeId() == PCG::Private::MetadataTypes<FVector>::Id)
		{
			DefaultValue = static_cast<const FPCGMetadataAttribute<FVector>*>(SourceAttribute)->GetValue(PCGDefaultValueKey);
		}
		else if (SourceAttribute->GetTypeId() == PCG::Private::MetadataTypes<FVector4>::Id)
		{
			DefaultValue = static_cast<const FPCGMetadataAttribute<FVector4>*>(SourceAttribute)->GetValue(PCGDefaultValueKey);
		}
		else //if (SourceAttribute->GetTypeId() == PCG::Private::MetadataTypes<FRotator>::Id)
		{
			const FRotator Rotator = static_cast<const FPCGMetadataAttribute<FRotator>*>(SourceAttribute)->GetValue(PCGDefaultValueKey);
			DefaultValue.X = Rotator.Roll;
			DefaultValue.Y = Rotator.Pitch;
			DefaultValue.Z = Rotator.Yaw;
		}

		FPCGMetadataAttribute<double>* AttributeX = nullptr;
		FPCGMetadataAttribute<double>* AttributeY = nullptr;
		FPCGMetadataAttribute<double>* AttributeZ = nullptr;
		FPCGMetadataAttribute<double>* AttributeW = nullptr;

		if (bXPinConnected)
		{
			FPCGTaggedData& OutputX = Outputs.Add_GetRef(Input);
			OutputX.Pin = PCGMetadataBreakVectorConstants::XLabel;

			UPCGMetadata* OutMetadata = nullptr;

			PCGMetadataBreakVectorElement::DuplicateTaggedData(Input, OutputX, OutMetadata);
			PCGMetadataBreakVectorElement::ClearOrCreateDoubleAttribute(OutMetadata, DestinationAttributeForX, DefaultValue.X, AttributeX);
			PCGMetadataBreakVectorElement::CopyEntryToValueKeyMap(SourceMetadata, SourceAttribute, AttributeX);
		}

		if (bYPinConnected)
		{
			FPCGTaggedData& OutputY = Outputs.Add_GetRef(Input);
			OutputY.Pin = PCGMetadataBreakVectorConstants::YLabel;

			UPCGMetadata* OutMetadata = nullptr;

			PCGMetadataBreakVectorElement::DuplicateTaggedData(Input, OutputY, OutMetadata);
			PCGMetadataBreakVectorElement::ClearOrCreateDoubleAttribute(OutMetadata, DestinationAttributeForY, DefaultValue.Y, AttributeY);
			PCGMetadataBreakVectorElement::CopyEntryToValueKeyMap(SourceMetadata, SourceAttribute, AttributeY);
		}

		if (bZPinConnected)
		{
			FPCGTaggedData& OutputZ = Outputs.Add_GetRef(Input);
			OutputZ.Pin = PCGMetadataBreakVectorConstants::ZLabel;

			UPCGMetadata* OutMetadata = nullptr;

			PCGMetadataBreakVectorElement::DuplicateTaggedData(Input, OutputZ, OutMetadata);
			PCGMetadataBreakVectorElement::ClearOrCreateDoubleAttribute(OutMetadata, DestinationAttributeForZ, DefaultValue.Z, AttributeZ);
			PCGMetadataBreakVectorElement::CopyEntryToValueKeyMap(SourceMetadata, SourceAttribute, AttributeZ);
		}

		if (bWPinConnected && SourceAttribute->GetTypeId() == PCG::Private::MetadataTypes<FVector4>::Id)
		{
			FPCGTaggedData& OutputW = Outputs.Add_GetRef(Input);
			OutputW.Pin = PCGMetadataBreakVectorConstants::WLabel;

			UPCGMetadata* OutMetadata = nullptr;

			PCGMetadataBreakVectorElement::DuplicateTaggedData(Input, OutputW, OutMetadata);
			PCGMetadataBreakVectorElement::ClearOrCreateDoubleAttribute(OutMetadata, DestinationAttributeForW, DefaultValue.W, AttributeW);
			PCGMetadataBreakVectorElement::CopyEntryToValueKeyMap(SourceMetadata, SourceAttribute, AttributeW);
		}

		// Copy all Value pairs from the parent hierarchy into our new attributes. Assumes that adding values in order of the parent hierarchy will result in identical ValueKey->Value mappings.
		const PCGMetadataValueKey NumValueKeys = SourceAttribute->GetValueKeyOffsetForChild();
		for (PCGMetadataValueKey ValueKey = 0; ValueKey < NumValueKeys; ++ValueKey)
		{
			FVector4 Value;

			if (SourceAttribute->GetTypeId() == PCG::Private::MetadataTypes<FVector>::Id)
			{
				Value = static_cast<const FPCGMetadataAttribute<FVector>*>(SourceAttribute)->GetValue(ValueKey);
			}
			else if (SourceAttribute->GetTypeId() == PCG::Private::MetadataTypes<FVector4>::Id)
			{
				Value = static_cast<const FPCGMetadataAttribute<FVector4>*>(SourceAttribute)->GetValue(ValueKey);
			}
			else //if (SourceAttribute->GetTypeId() == PCG::Private::MetadataTypes<FRotator>::Id)
			{
				const FRotator Rotator = static_cast<const FPCGMetadataAttribute<FRotator>*>(SourceAttribute)->GetValue(ValueKey);
				Value.X = Rotator.Roll;
				Value.Y = Rotator.Pitch;
				Value.Z = Rotator.Yaw;
			}

			if (AttributeX)
			{
				AttributeX->AddValue(Value.X);
			}

			if (AttributeY)
			{
				AttributeY->AddValue(Value.Y);
			}

			if (AttributeZ)
			{
				AttributeZ->AddValue(Value.Z);
			}

			if (AttributeW)
			{
				AttributeW->AddValue(Value.W);
			}
		}
	}

	return true;
}
