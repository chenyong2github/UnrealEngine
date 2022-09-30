// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Metadata/PCGMetadataBreakVector.h"

#include "PCGParamData.h"
#include "Data/PCGSpatialData.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

namespace PCGMetadataBreakVectorSettings
{
	template <typename InType = FVector>
	inline void DoBreak(const InType& Value, TArray<double>& OutValues, uint32 NumOutputs)
	{
		check(NumOutputs == 4);

		OutValues.Add(Value.X);
		OutValues.Add(Value.Y);
		OutValues.Add(Value.Z);
		OutValues.Add(0.0);
	}

	template <>
	inline void DoBreak<FRotator>(const FRotator& Value, TArray<double>& OutValues, uint32 NumOutputs)
	{
		check(NumOutputs == 4);

		OutValues.Add(Value.Roll);
		OutValues.Add(Value.Pitch);
		OutValues.Add(Value.Yaw);
		OutValues.Add(0.0);
	}

	template <>
	inline void DoBreak<FVector2D>(const FVector2D& Value, TArray<double>& OutValues, uint32 NumOutputs)
	{
		check(NumOutputs == 4);

		OutValues.Add(Value.X);
		OutValues.Add(Value.Y);
		OutValues.Add(0.0);
		OutValues.Add(0.0);
	}

	template <>
	inline void DoBreak<FVector4>(const FVector4& Value, TArray<double>& OutValues, uint32 NumOutputs)
	{
		check(NumOutputs == 4);

		OutValues.Add(Value.X);
		OutValues.Add(Value.Y);
		OutValues.Add(Value.Z);
		OutValues.Add(Value.W);
	}

	inline constexpr bool IsValidType(uint16 TypeId)
	{
		return TypeId == (uint16)EPCGMetadataTypes::Vector2 ||
			TypeId == (uint16)EPCGMetadataTypes::Vector ||
			TypeId == (uint16)EPCGMetadataTypes::Vector4 ||
			TypeId == (uint16)EPCGMetadataTypes::Rotator;
	}

	template <typename T>
	inline constexpr bool IsValidType()
	{
		return IsValidType(PCG::Private::MetadataTypes<T>::Id);
	}
}

FName UPCGMetadataBreakVectorSettings::GetInputAttributeNameWithOverride(uint32 Index, UPCGParamData* Params) const
{
	return PCG_GET_OVERRIDEN_VALUE(this, InputAttributeName, Params);
}

FName UPCGMetadataBreakVectorSettings::GetOutputPinLabel(uint32 Index) const 
{
	switch (Index)
	{
	case 0:
		return PCGMetadataBreakVectorConstants::XLabel;
	case 1:
		return PCGMetadataBreakVectorConstants::YLabel;
	case 2:
		return PCGMetadataBreakVectorConstants::ZLabel;
	default:
		return PCGMetadataBreakVectorConstants::WLabel;
	}
}

uint32 UPCGMetadataBreakVectorSettings::GetOutputPinNum() const
{
	return 4;
}

bool UPCGMetadataBreakVectorSettings::IsSupportedInputType(uint16 TypeId, uint32 InputIndex, bool& bHasSpecialRequirement) const
{
	bHasSpecialRequirement = false;
	return PCGMetadataBreakVectorSettings::IsValidType(TypeId);
}

uint16 UPCGMetadataBreakVectorSettings::GetOutputType(uint16 InputTypeId) const
{
	return (uint16)EPCGMetadataTypes::Double;
}

FName UPCGMetadataBreakVectorSettings::GetOutputAttributeName(FName BaseName, uint32 Index) const
{
	if (BaseName == NAME_None)
	{
		return NAME_None;
	}

	switch (Index)
	{
	case 0:
		return FName(BaseName.ToString() + "." + PCGMetadataBreakVectorConstants::XLabel.ToString());
	case 1:
		return FName(BaseName.ToString() + "." + PCGMetadataBreakVectorConstants::YLabel.ToString());
	case 2:
		return FName(BaseName.ToString() + "." + PCGMetadataBreakVectorConstants::ZLabel.ToString());
	default:
		return FName(BaseName.ToString() + "." + PCGMetadataBreakVectorConstants::WLabel.ToString());
	}
}

FPCGElementPtr UPCGMetadataBreakVectorSettings::CreateElement() const
{
	return MakeShared<FPCGMetadataBreakVectorElement>();
}

bool FPCGMetadataBreakVectorElement::DoOperation(FOperationData& OperationData) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGMetadataBreakVectorElement::Execute);

	const UPCGMetadataBreakVectorSettings* Settings = static_cast<const UPCGMetadataBreakVectorSettings*>(OperationData.Settings);
	check(Settings);

	auto BreakFunc = [this, &OperationData](auto DummyValue) -> bool
	{
		using AttributeType = decltype(DummyValue);

		if constexpr (!PCGMetadataBreakVectorSettings::IsValidType<AttributeType>())
		{
			return false;
		}
		else
		{
			return DoUnaryOpMultipleOutputs<AttributeType, double>(OperationData, PCGMetadataBreakVectorSettings::DoBreak<AttributeType>);
		}
	};

	return PCGMetadataAttribute::CallbackWithRightType(OperationData.MostComplexInputType, BreakFunc);
}
