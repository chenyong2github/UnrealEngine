// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Metadata/PCGMetadataMathsOpElement.h"

#include "PCGParamData.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataEntryKeyIterator.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"

#include "Elements/Metadata/PCGMetadataMaths.inl"

namespace PCGMetadataMathsSettings
{
	inline constexpr bool IsUnaryOp(EPCGMedadataMathsOperation Operation)
	{
		return !!(Operation & EPCGMedadataMathsOperation::UnaryOp);
	}

	inline constexpr bool IsBinaryOp(EPCGMedadataMathsOperation Operation)
	{
		return !!(Operation & EPCGMedadataMathsOperation::BinaryOp);
	}

	inline constexpr bool IsTernaryOp(EPCGMedadataMathsOperation Operation)
	{
		return !!(Operation & EPCGMedadataMathsOperation::TernaryOp);
	}

	inline FName GetFirstPinLabel(EPCGMedadataMathsOperation Operation)
	{
		if (PCGMetadataMathsSettings::IsUnaryOp(Operation)
			|| Operation == EPCGMedadataMathsOperation::Clamp
			|| Operation == EPCGMedadataMathsOperation::ClampMin
			|| Operation == EPCGMedadataMathsOperation::ClampMax)
		{
			return PCGPinConstants::DefaultInputLabel;
		}

		if (PCGMetadataMathsSettings::IsBinaryOp(Operation)
			|| Operation == EPCGMedadataMathsOperation::Lerp)
		{
			return PCGMetadataSettingsBaseConstants::DoubleInputFirstLabel;
		}

		return NAME_None;
	}

	inline FName GetSecondPinLabel(EPCGMedadataMathsOperation Operation)
	{
		if (Operation == EPCGMedadataMathsOperation::ClampMin || Operation == EPCGMedadataMathsOperation::Clamp)
		{
			return PCGMetadataSettingsBaseConstants::ClampMinLabel;
		}

		if (Operation == EPCGMedadataMathsOperation::ClampMax)
		{
			return PCGMetadataSettingsBaseConstants::ClampMaxLabel;
		}

		if (PCGMetadataMathsSettings::IsBinaryOp(Operation) || PCGMetadataMathsSettings::IsTernaryOp(Operation))
		{
			return PCGMetadataSettingsBaseConstants::DoubleInputSecondLabel;
		}

		return NAME_None;
	}

	inline FName GetThirdPinLabel(EPCGMedadataMathsOperation Operation)
	{
		if (Operation == EPCGMedadataMathsOperation::Clamp)
		{
			return PCGMetadataSettingsBaseConstants::ClampMaxLabel;
		}

		if (Operation == EPCGMedadataMathsOperation::Lerp)
		{
			return PCGMetadataSettingsBaseConstants::LerpRatioLabel;
		}

		return NAME_None;
	}

	template <typename T>
	void UnaryOp(T& Value, EPCGMedadataMathsOperation Op)
	{
		switch (Op)
		{
		case EPCGMedadataMathsOperation::Sign:
			Value = PCGMetadataMaths::Sign(Value);
			break;
		case EPCGMedadataMathsOperation::Frac:
			Value = PCGMetadataMaths::Frac(Value);
			break;
		case EPCGMedadataMathsOperation::Truncate:
			Value = PCGMetadataMaths::Truncate(Value);
			break;
		case EPCGMedadataMathsOperation::Round:
			Value = PCGMetadataMaths::Round(Value);
			break;
		case EPCGMedadataMathsOperation::Sqrt:
			Value = PCGMetadataMaths::Sqrt(Value);
			break;
		case EPCGMedadataMathsOperation::Abs:
			Value = PCGMetadataMaths::Abs(Value);
			break;
		}
	}

	template <typename T>
	void BinaryOp(T& Value1, const T& Value2, EPCGMedadataMathsOperation Op)
	{
		switch (Op)
		{
		case EPCGMedadataMathsOperation::Add:
			Value1 = Value1 + Value2;
			break;
		case EPCGMedadataMathsOperation::Subtract:
			Value1 = Value1 - Value2;
			break;
		case EPCGMedadataMathsOperation::Multiply:
			Value1 = Value1 * Value2;
			break;
		case EPCGMedadataMathsOperation::Divide:
			Value1 = Value1 / Value2;
			break;
		case EPCGMedadataMathsOperation::Max:
			Value1 = PCGMetadataMaths::Max(Value1, Value2);
			break;
		case EPCGMedadataMathsOperation::Min:
			Value1 = PCGMetadataMaths::Min(Value1, Value2);
			break;
		case EPCGMedadataMathsOperation::ClampMin:
			Value1 = PCGMetadataMaths::Clamp(Value1, Value2, Value1);
			break;
		case EPCGMedadataMathsOperation::ClampMax:
			Value1 = PCGMetadataMaths::Clamp(Value1, Value1, Value2);
			break;
		case EPCGMedadataMathsOperation::Pow:
			Value1 = PCGMetadataMaths::Pow(Value1, Value2);
			break;
		}
	}

	template <typename T>
	void TernaryOp(T& Value1, const T& Value2, const T& Value3, EPCGMedadataMathsOperation Op)
	{
		switch (Op)
		{
		case EPCGMedadataMathsOperation::Clamp:
			Value1 = PCGMetadataMaths::Clamp(Value1, Value2, Value3);
			break;
		case EPCGMedadataMathsOperation::Lerp:
			Value1 = PCGMetadataMaths::Lerp(Value1, Value2, Value3);
			break;
		}
	}
}

FName UPCGMetadataMathsSettings::GetInputPinLabel(uint32 Index) const
{
	switch (Index)
	{
	case 0:
		return PCGMetadataMathsSettings::GetFirstPinLabel(Operation);
	case 1:
		return PCGMetadataMathsSettings::GetSecondPinLabel(Operation);
	case 2:
		return PCGMetadataMathsSettings::GetThirdPinLabel(Operation);
	default:
		return NAME_None;
	}
}

uint32 UPCGMetadataMathsSettings::GetInputPinNum() const
{
	if (PCGMetadataMathsSettings::IsUnaryOp(Operation))
	{
		return 1;
	}

	if (PCGMetadataMathsSettings::IsBinaryOp(Operation))
	{
		return 2;
	}

	if (PCGMetadataMathsSettings::IsTernaryOp(Operation))
	{
		return 3;
	}

	return 0;
}

// By default: Float/Double, Int32/Int64, Vector2, Vector, Vector4
bool UPCGMetadataMathsSettings::IsSupportedInputType(uint16 TypeId, uint32 InputIndex, bool& bHasSpecialRequirement) const
{
	bHasSpecialRequirement = false;
	return TypeId <= (uint16)EPCGMetadataTypes::Vector4;
}

FName UPCGMetadataMathsSettings::GetInputAttributeNameWithOverride(uint32 Index, UPCGParamData* Params) const
{
	switch (Index)
	{
	case 0:
		return PCG_GET_OVERRIDEN_VALUE(this, Input1AttributeName, Params);
	case 1:
		return PCG_GET_OVERRIDEN_VALUE(this, Input2AttributeName, Params);
	case 2:
		return PCG_GET_OVERRIDEN_VALUE(this, Input3AttributeName, Params);
	default:
		return NAME_None;
	}
}

#if WITH_EDITOR
FName UPCGMetadataMathsSettings::GetDefaultNodeName() const
{
	if (const UEnum* EnumPtr = FindObject<UEnum>(nullptr, TEXT("EPCGMedadataMathsOperation"), true))
	{ 
		return EnumPtr->GetNameByValue(static_cast<int>(Operation)); 
	}

	return TEXT("Metadata Maths Node");
}
#endif // WITH_EDITOR

FPCGElementPtr UPCGMetadataMathsSettings::CreateElement() const
{
	return MakeShared<FPCGMetadataMathsElement>();
}

bool FPCGMetadataMathsElement::DoOperation(FOperationData& OperationData) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGMetadataMathsElement::Execute);

	const UPCGMetadataMathsSettings* Settings = CastChecked<UPCGMetadataMathsSettings>(OperationData.Settings);

	// All values in OperationData.Iterators are UniquePtr. Dereference them here to make the syntax less cumbersome.
	// Also some iterators can be null, it means they need to use the first iterator (that should never be null).
	// We just need to make sure to not increment the first iterator multiple times in one loop.

	check(OperationData.Iterators[0]);

	IPCGMetadataEntryIterator& Iterator1 = *OperationData.Iterators[0];
	IPCGMetadataEntryIterator& Iterator2 = (OperationData.Iterators.Num() >= 2 && OperationData.Iterators[1]) ? *OperationData.Iterators[1] : Iterator1;
	IPCGMetadataEntryIterator& Iterator3 = (OperationData.Iterators.Num() >= 3 && OperationData.Iterators[2]) ? *OperationData.Iterators[2] : Iterator1;

	bool bShouldIncrementIterator2 = &Iterator1 != &Iterator2;
	bool bShouldIncrementIterator3 = &Iterator1 != &Iterator3;

	// And then do the operation for all elements
	if (PCGMetadataMathsSettings::IsUnaryOp(Settings->Operation))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGMetadataMathsElement::ExecuteInternal::UnaryOp);
		auto UnaryFunc = [Operation = Settings->Operation, &OperationData, &Iterator1](auto DummyOutValue) -> void
		{
			using AttributeType = decltype(DummyOutValue);

			// Need to remove types that would not compile
			if constexpr (PCG::Private::MetadataTypes<AttributeType>::Id > (uint16)EPCGMetadataTypes::Vector4)
			{
				return;
			}
			else
			{
				FPCGMetadataAttribute<AttributeType>* OutputAttribute = static_cast<FPCGMetadataAttribute<AttributeType>*>(OperationData.OutputAttribute);
				AttributeType DefaultValue = PCGMetadataAttribute::GetValueWithBroadcast<AttributeType>(OperationData.SourceAttributes[0], PCGInvalidEntryKey);
				PCGMetadataMathsSettings::UnaryOp(DefaultValue, Operation);
				OutputAttribute->SetDefaultValue(DefaultValue);

				for (int32 i = 0; i < OperationData.NumberOfElementsToProcess; ++i)
				{
					PCGMetadataEntryKey EntryKey = *Iterator1;

					// If the entry key is invalid, nothing to do
					if (EntryKey == PCGInvalidEntryKey)
					{
						AttributeType Value = PCGMetadataAttribute::GetValueWithBroadcast<AttributeType>(OperationData.SourceAttributes[0], EntryKey);

						PCGMetadataMathsSettings::UnaryOp(Value, Operation);

						OutputAttribute->SetValue(EntryKey, Value);
					}

					++Iterator1;
				}
			}
		};

		PCGMetadataAttribute::CallbackWithRightType(OperationData.OutputType, UnaryFunc);
	}
	else if (PCGMetadataMathsSettings::IsBinaryOp(Settings->Operation))
	{
		auto BinaryFunc = [Operation = Settings->Operation, &OperationData, &Iterator1, &Iterator2, bShouldIncrementIterator2](auto DummyOutValue) -> void
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FPCGMetadataMathsElement::ExecuteInternal::BinaryOp);
			using AttributeType = decltype(DummyOutValue);

			// Need to remove types that would not compile
			if constexpr (PCG::Private::MetadataTypes<AttributeType>::Id > (uint16)EPCGMetadataTypes::Vector4)
			{
				return;
			}
			else
			{
				FPCGMetadataAttribute<AttributeType>* OutputAttribute = static_cast<FPCGMetadataAttribute<AttributeType>*>(OperationData.OutputAttribute);
				AttributeType DefaultValue1 = PCGMetadataAttribute::GetValueWithBroadcast<AttributeType>(OperationData.SourceAttributes[0], PCGInvalidEntryKey);
				AttributeType DefaultValue2 = PCGMetadataAttribute::GetValueWithBroadcast<AttributeType>(OperationData.SourceAttributes[1], PCGInvalidEntryKey);
				PCGMetadataMathsSettings::BinaryOp(DefaultValue1, DefaultValue2, Operation);
				OutputAttribute->SetDefaultValue(DefaultValue1);

				for (int32 i = 0; i < OperationData.NumberOfElementsToProcess; ++i)
				{
					PCGMetadataEntryKey EntryKey1 = *Iterator1;
					PCGMetadataEntryKey EntryKey2 = *Iterator2;

					// If the entry key is invalid, nothing to do
					if (EntryKey1 == PCGInvalidEntryKey)
					{
						AttributeType Value1 = PCGMetadataAttribute::GetValueWithBroadcast<AttributeType>(OperationData.SourceAttributes[0], EntryKey1);
						AttributeType Value2 = PCGMetadataAttribute::GetValueWithBroadcast<AttributeType>(OperationData.SourceAttributes[1], EntryKey2);

						PCGMetadataMathsSettings::BinaryOp(Value1, Value2, Operation);

						OutputAttribute->SetValue(EntryKey1, Value1);
					}

					++Iterator1;
					if (bShouldIncrementIterator2)
					{
						++Iterator2;
					}
				}
			}
		};

		PCGMetadataAttribute::CallbackWithRightType(OperationData.OutputType, BinaryFunc);
	}
	else if (PCGMetadataMathsSettings::IsTernaryOp(Settings->Operation))
	{
		auto TernaryFunc = [Operation = Settings->Operation, &OperationData, &Iterator1, &Iterator2, &Iterator3, bShouldIncrementIterator2, bShouldIncrementIterator3](auto DummyOutValue) -> void
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FPCGMetadataMathsElement::ExecuteInternal::TernaryOp);
			using AttributeType = decltype(DummyOutValue);

			// Need to remove types that would not compile
			if constexpr (PCG::Private::MetadataTypes<AttributeType>::Id > (uint16)EPCGMetadataTypes::Vector4)
			{
				return;
			}
			else
			{
				FPCGMetadataAttribute<AttributeType>* OutputAttribute = static_cast<FPCGMetadataAttribute<AttributeType>*>(OperationData.OutputAttribute);
				AttributeType DefaultValue1 = PCGMetadataAttribute::GetValueWithBroadcast<AttributeType>(OperationData.SourceAttributes[0], PCGInvalidEntryKey);
				AttributeType DefaultValue2 = PCGMetadataAttribute::GetValueWithBroadcast<AttributeType>(OperationData.SourceAttributes[1], PCGInvalidEntryKey);
				AttributeType DefaultValue3 = PCGMetadataAttribute::GetValueWithBroadcast<AttributeType>(OperationData.SourceAttributes[2], PCGInvalidEntryKey);
				PCGMetadataMathsSettings::TernaryOp(DefaultValue1, DefaultValue2, DefaultValue3, Operation);
				OutputAttribute->SetDefaultValue(DefaultValue1);

				for (PCGMetadataValueKey ValueKey = 0; ValueKey < OperationData.NumberOfElementsToProcess; ++ValueKey)
				{
					PCGMetadataEntryKey EntryKey1 = *Iterator1;
					PCGMetadataEntryKey EntryKey2 = *Iterator2;
					PCGMetadataEntryKey EntryKey3 = *Iterator3;

					// If the entry key is invalid, nothing to do
					if (EntryKey1 == PCGInvalidEntryKey)
					{
						AttributeType Value1 = PCGMetadataAttribute::GetValueWithBroadcast<AttributeType>(OperationData.SourceAttributes[0], EntryKey1);
						AttributeType Value2 = PCGMetadataAttribute::GetValueWithBroadcast<AttributeType>(OperationData.SourceAttributes[1], EntryKey2);
						AttributeType Value3 = PCGMetadataAttribute::GetValueWithBroadcast<AttributeType>(OperationData.SourceAttributes[2], EntryKey3);

						PCGMetadataMathsSettings::TernaryOp(Value1, Value2, Value3, Operation);

						OutputAttribute->SetValue(EntryKey1, Value1);
					}

					++Iterator1;
					if (bShouldIncrementIterator2)
					{
						++Iterator2;
					}

					if (bShouldIncrementIterator3)
					{
						++Iterator3;
					}
				}
			}
		};

		PCGMetadataAttribute::CallbackWithRightType(OperationData.OutputType, TernaryFunc);
	}

	return true;
}