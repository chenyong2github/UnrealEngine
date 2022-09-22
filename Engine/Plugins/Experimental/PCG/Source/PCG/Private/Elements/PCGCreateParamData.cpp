// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/PCGCreateParamData.h"

#include "PCGParamData.h"
#include "Metadata/PCGMetadata.h"
#include "Elements/Metadata/PCGMetadataOpElementBase.h"

#if WITH_EDITOR
FName UPCGCreateParamDataSettings::GetDefaultNodeName() const
{
	return TEXT("CreateParamDataNode");
}
#endif

TArray<FPCGPinProperties> UPCGCreateParamDataSettings::InputPinProperties() const
{
	return TArray<FPCGPinProperties>();
}

TArray<FPCGPinProperties> UPCGCreateParamDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Param);

	return PinProperties;
}

FPCGElementPtr UPCGCreateParamDataSettings::CreateElement() const
{
	return MakeShared<FPCGCreateParamDataElement>();
}

bool FPCGCreateParamDataElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGCreateParamDataElement::Execute);

	check(Context);

	const UPCGCreateParamDataSettings* Settings = Context->GetInputSettings<UPCGCreateParamDataSettings>();
	check(Settings);

	// If we have no output connected, nothing to do
	if (!Context->Node || !Context->Node->IsOutputPinConnected(PCGPinConstants::DefaultOutputLabel))
	{
		PCGE_LOG(Verbose, "Node is not connected, nothing to do");
		return true;
	}

	// From there, we should be able to create the data.
	UPCGParamData* ParamData = NewObject<UPCGParamData>();
	UPCGMetadata* Metadata = ParamData->MutableMetadata();
	check(Metadata);
	PCGMetadataEntryKey EntryKey = Metadata->AddEntry();

	auto CreateAndSetAttribute = [&Metadata, EntryKey, AttributeName = Settings->OutputAttributeName](const auto& Value)
	{
		using AttributeType = std::remove_const_t<std::remove_reference_t<decltype(Value)>>;

		FPCGMetadataAttributeBase* BaseAttribute = Metadata->CreateAttribute<AttributeType>(AttributeName, Value, false, false);

		FPCGMetadataAttribute<AttributeType>* Attribute = static_cast<FPCGMetadataAttribute<AttributeType>*>(BaseAttribute);
		Attribute->SetValue(EntryKey, Value);
	};

	switch (Settings->Type)
	{

	case EPCGMetadataTypes::Integer64:
		CreateAndSetAttribute(Settings->IntValue);
		break;
	case EPCGMetadataTypes::Double:
		CreateAndSetAttribute(Settings->DoubleValue);
		break;
	case EPCGMetadataTypes::Vector2:
		CreateAndSetAttribute(Settings->Vector2Value);
		break;
	case EPCGMetadataTypes::Vector:
		CreateAndSetAttribute(Settings->VectorValue);
		break;
	case EPCGMetadataTypes::Vector4:
		CreateAndSetAttribute(Settings->Vector4Value);
		break;
	case EPCGMetadataTypes::Quaternion:
		CreateAndSetAttribute(Settings->QuatValue);
		break;
	case EPCGMetadataTypes::Transform:
		CreateAndSetAttribute(Settings->TransformValue);
		break;
	case EPCGMetadataTypes::String:
		CreateAndSetAttribute(Settings->StringValue);
		break;
	case EPCGMetadataTypes::Boolean:
		CreateAndSetAttribute(Settings->BoolValue);
		break;
	case EPCGMetadataTypes::Rotator:
		CreateAndSetAttribute(Settings->RotatorValue);
		break;
	case EPCGMetadataTypes::Name:
		CreateAndSetAttribute(Settings->NameValue);
		break;
	default:
		break;
	}

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
	FPCGTaggedData& Output = Outputs.Emplace_GetRef();
	Output.Data = ParamData;

	return true;
}