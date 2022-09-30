// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGElement.h"
#include "PCGSettings.h"

#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataEntryKeyIterator.h"

#include "Containers/StaticArray.h"

#include "PCGMetadataOpElementBase.generated.h"

class FPCGMetadataAttributeBase;
class IPCGMetadataEntryIterator;

namespace PCGMetadataSettingsBaseConstants
{
	const FName DoubleInputFirstLabel = TEXT("InA");
	const FName DoubleInputSecondLabel = TEXT("InB");

	const FName ClampMinLabel = TEXT("Min");
	const FName ClampMaxLabel = TEXT("Max");
	const FName LerpRatioLabel = TEXT("Ratio");
	const FName TransformLabel = TEXT("Transform");
}

// Defines behavior when number of entries doesn't match in inputs
UENUM()
enum class EPCGMetadataSettingsBaseMode
{
	Inferred     UMETA(Tooltip = "Broadcast for ParamData and no broadcast for SpatialData."),
	NoBroadcast  UMETA(ToolTip = "If number of entries doesn't match, will use the default value."),
	Broadcast    UMETA(ToolTip = "If there is no entry or a single entry, will repeat this value.")
};

UENUM()
enum class EPCGMetadataSettingsBaseTypes
{
	AutoUpcastTypes,
	StrictTypes
};

/**
 * Base class for all Metadata operations
 * A metadata operation can work on 2 different type of inputs: ParamData and SpatialData
 * Each of those inputs can have some metadata.
 * The output will contain the metadata of the first input (all its attributes) + the result of the operation (in a separate attribute)
 * The new attribute can collide with one of the attributes in the incoming metadata. In this case, the attribute value will be overridden by the result
 * of the operation. It will also override the type of the attribute if it doesn't match the original.
 * 
 * You can specify the name of the attribute for each input and for the output. If they are None, they will take the default attribute.
 * Attribute names can also be overridden by ParamData, just connect the Param pin with some param data that matches exactly the name of the property you want to override.
 * 
 * Each operation has some requirements for the input types, and can broadcast some values into others (example Vector + Float -> Vector).
 * For example, if the op only accept booleans, all other value types will throw an error.
 * 
 * If there are multiple values for an attribute, the operation will be done on all values. If one input has N elements and the second has 1 element,
 * the second will be repeated for each element of the first for the operation. We only support N-N operations and N-1 operation (ie. The number of values
 * needs to be all the same or 1)
 * 
 * If the node doesn't provide an output, check the logs to know why it failed.
 */
UCLASS(BlueprintType, Abstract, ClassGroup = (Procedural))
class PCG_API UPCGMetadataSettingsBase : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Metadata; }
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	//~End UPCGSettings interface

	virtual FName GetInputAttributeNameWithOverride(uint32 Index, UPCGParamData* Params) const { return NAME_None; };

	virtual FName GetInputPinLabel(uint32 Index) const { return PCGPinConstants::DefaultInputLabel; }
	virtual uint32 GetInputPinNum() const { return 1; };

	virtual FName GetOutputPinLabel(uint32 Index) const { return PCGPinConstants::DefaultOutputLabel; }
	virtual uint32 GetOutputPinNum() const { return 1; }

	virtual bool IsSupportedInputType(uint16 TypeId, uint32 InputIndex, bool& bHasSpecialRequirement) const { return false; };
	virtual uint16 GetOutputType(uint16 InputTypeId) const { return InputTypeId; };
	virtual FName GetOutputAttributeName(FName BaseName, uint32 Index) const { return BaseName; }

	bool IsMoreComplexType(uint16 FirstType, uint16 SecondType) const;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Output)
	FName OutputAttributeName = NAME_None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGMetadataSettingsBaseMode Mode = EPCGMetadataSettingsBaseMode::Inferred;

	static constexpr uint32 MaxNumberOfOutputs = 4;

#if WITH_EDITORONLY_DATA
	// Useful for unit tests. Allow to force a connection to allow the node to do its operation, even if nothing is connected to it.
	TStaticArray<bool, MaxNumberOfOutputs> ForceOutputConnections{};
#endif // WITH_EDITORONLY_DATA
};


class FPCGMetadataElementBase : public FSimplePCGElement
{
public:
	// FPCGMetadataElementBase relies on StaticDuplicateObject (when we duplicate tagged data), so we cannot run outside of the main thread
	virtual bool CanExecuteOnlyOnMainThread(const UPCGSettings* InSettings) const override { return true; }

	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return true; }

	struct FOperationData
	{
		TArray<const FPCGMetadataAttributeBase*> SourceAttributes;
		int32 NumberOfElementsToProcess = -1;
		uint16 MostComplexInputType;
		uint16 OutputType;
		TArray<FPCGMetadataAttributeBase*> OutputAttributes;
		const UPCGMetadataSettingsBase* Settings = nullptr;
		TArray<TUniquePtr<IPCGMetadataEntryIterator>> Iterators;
	};

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;

	virtual bool DoOperation(FOperationData& OperationData) const = 0;

	/* All operations return true if it succeeded false otherwise.
	* For each operation, the callback takes as many inputs as needed, as const ref of InType.
	* and must return OutType by value.
	*/
	template <typename InType, typename OutType>
	bool DoUnaryOp(FOperationData& OperationData, TFunction<OutType(const InType&)> UnaryOp) const;

	template <typename InType1, typename InType2, typename OutType>
	bool DoBinaryOp(FOperationData& OperationData, TFunction<OutType(const InType1&, const InType2&)> BinaryOp) const;

	template <typename InType1, typename InType2, typename InType3, typename OutType>
	bool DoTernaryOp(FOperationData& OperationData, TFunction<OutType(const InType1&, const InType2&, const InType3&)> TernaryOp) const;

	template <typename InType1, typename InType2, typename InType3, typename InType4, typename OutType>
	bool DoQuaternaryOp(FOperationData& OperationData, TFunction<OutType(const InType1&, const InType2&, const InType3&, const InType4&)> QuaternaryOp) const;

	/* For multiple outputs, the callback won't return anything. But the callback should write results in
	* the array passed as ref. Number of requested outputs is also passed as a parameter.
	* Note that the array passed is "reserved" but not "resized", meaning that you need to push your results
	* and not access the items directly with operator[].
	*/
	template <typename InType, typename OutType>
	bool DoUnaryOpMultipleOutputs(FOperationData& OperationData, TFunction<void(const InType&, TArray<OutType>&, uint32)> UnaryOp) const;
};

template <typename InType, typename OutType>
inline bool FPCGMetadataElementBase::DoUnaryOp(FOperationData& OperationData, TFunction<OutType(const InType&)> UnaryOp) const
{
	check(OperationData.SourceAttributes[0]);
	check(OperationData.OutputAttributes[0]);

	// All values in OperationData.Iterators are UniquePtr. Dereference them here to make the syntax less cumbersome.
	check(OperationData.Iterators[0]);
	IPCGMetadataEntryIterator& Iterator1 = *OperationData.Iterators[0];

	FPCGMetadataAttribute<OutType>* OutputAttribute = static_cast<FPCGMetadataAttribute<OutType>*>(OperationData.OutputAttributes[0]);
	InType DefaultValue = PCGMetadataAttribute::GetValueWithBroadcast<InType>(OperationData.SourceAttributes[0], PCGInvalidEntryKey);
	OutType DefaultOutputValue = UnaryOp(DefaultValue);
	OutputAttribute->SetDefaultValue(DefaultOutputValue);

	for (int32 i = 0; i < OperationData.NumberOfElementsToProcess; ++i)
	{
		PCGMetadataEntryKey EntryKey = *Iterator1;

		// If the entry key is invalid, nothing to do
		if (EntryKey != PCGInvalidEntryKey)
		{
			InType Value = PCGMetadataAttribute::GetValueWithBroadcast<InType>(OperationData.SourceAttributes[0], EntryKey);

			OutType OutputValue = UnaryOp(Value);

			OutputAttribute->SetValue(EntryKey, OutputValue);
		}

		++Iterator1;
	}

	return true;
}

template <typename InType1, typename InType2, typename OutType>
inline bool FPCGMetadataElementBase::DoBinaryOp(FOperationData& OperationData, TFunction<OutType(const InType1&, const InType2&)> BinaryOp) const
{
	check(OperationData.SourceAttributes[0]);
	check(OperationData.SourceAttributes[1]);
	check(OperationData.OutputAttributes[0]);

	// All values in OperationData.Iterators are UniquePtr. Dereference them here to make the syntax less cumbersome.
	// Also some iterators can be null, it means they need to use the first iterator (that should never be null).
	// We just need to make sure to not increment the first iterator multiple times in one loop.
	check(OperationData.Iterators[0]);

	bool bShouldIncrementIterator2 = OperationData.Iterators.Num() >= 2 && OperationData.Iterators[1];

	IPCGMetadataEntryIterator& Iterator1 = *OperationData.Iterators[0];
	IPCGMetadataEntryIterator& Iterator2 = bShouldIncrementIterator2 ? *OperationData.Iterators[1] : Iterator1;

	FPCGMetadataAttribute<OutType>* OutputAttribute = static_cast<FPCGMetadataAttribute<OutType>*>(OperationData.OutputAttributes[0]);
	InType1 DefaultValue1 = PCGMetadataAttribute::GetValueWithBroadcast<InType1>(OperationData.SourceAttributes[0], PCGInvalidEntryKey);
	InType2 DefaultValue2 = PCGMetadataAttribute::GetValueWithBroadcast<InType2>(OperationData.SourceAttributes[1], PCGInvalidEntryKey);
	OutType DefaultOutputValue = BinaryOp(DefaultValue1, DefaultValue2);
	OutputAttribute->SetDefaultValue(DefaultOutputValue);

	for (int32 i = 0; i < OperationData.NumberOfElementsToProcess; ++i)
	{
		PCGMetadataEntryKey EntryKey1 = *Iterator1;

		// If the entry key is invalid, nothing to do
		if (EntryKey1 != PCGInvalidEntryKey)
		{
			PCGMetadataEntryKey EntryKey2 = *Iterator2;

			InType1 Value1 = PCGMetadataAttribute::GetValueWithBroadcast<InType1>(OperationData.SourceAttributes[0], EntryKey1);
			InType2 Value2 = PCGMetadataAttribute::GetValueWithBroadcast<InType2>(OperationData.SourceAttributes[1], EntryKey2);

			OutType OutputValue = BinaryOp(Value1, Value2);

			OutputAttribute->SetValue(EntryKey1, OutputValue);
		}

		++Iterator1;
		if (bShouldIncrementIterator2)
		{
			++Iterator2;
		}
	}

	return true;
}

template <typename InType1, typename InType2, typename InType3, typename OutType>
inline bool FPCGMetadataElementBase::DoTernaryOp(FOperationData& OperationData, TFunction<OutType(const InType1&, const InType2&, const InType3&)> TernaryOp) const
{
	check(OperationData.SourceAttributes[0]);
	check(OperationData.SourceAttributes[1]);
	check(OperationData.SourceAttributes[2]);
	check(OperationData.OutputAttributes[0]);

	// All values in OperationData.Iterators are UniquePtr. Dereference them here to make the syntax less cumbersome.
	// Also some iterators can be null, it means they need to use the first iterator (that should never be null).
	// We just need to make sure to not increment the first iterator multiple times in one loop.
	check(OperationData.Iterators[0]);

	bool bShouldIncrementIterator2 = OperationData.Iterators.Num() >= 2 && OperationData.Iterators[1];
	bool bShouldIncrementIterator3 = OperationData.Iterators.Num() >= 3 && OperationData.Iterators[2];

	IPCGMetadataEntryIterator& Iterator1 = *OperationData.Iterators[0];
	IPCGMetadataEntryIterator& Iterator2 = bShouldIncrementIterator2 ? *OperationData.Iterators[1] : Iterator1;
	IPCGMetadataEntryIterator& Iterator3 = bShouldIncrementIterator3 ? *OperationData.Iterators[2] : Iterator1;

	FPCGMetadataAttribute<OutType>* OutputAttribute = static_cast<FPCGMetadataAttribute<OutType>*>(OperationData.OutputAttributes[0]);
	InType1 DefaultValue1 = PCGMetadataAttribute::GetValueWithBroadcast<InType1>(OperationData.SourceAttributes[0], PCGInvalidEntryKey);
	InType2 DefaultValue2 = PCGMetadataAttribute::GetValueWithBroadcast<InType2>(OperationData.SourceAttributes[1], PCGInvalidEntryKey);
	InType3 DefaultValue3 = PCGMetadataAttribute::GetValueWithBroadcast<InType3>(OperationData.SourceAttributes[2], PCGInvalidEntryKey);
	OutType DefaultOutputValue = TernaryOp(DefaultValue1, DefaultValue2, DefaultValue3);
	OutputAttribute->SetDefaultValue(DefaultOutputValue);

	for (int32 i = 0; i < OperationData.NumberOfElementsToProcess; ++i)
	{
		PCGMetadataEntryKey EntryKey1 = *Iterator1;

		// If the entry key is invalid, nothing to do
		if (EntryKey1 != PCGInvalidEntryKey)
		{
			PCGMetadataEntryKey EntryKey2 = *Iterator2;
			PCGMetadataEntryKey EntryKey3 = *Iterator3;

			InType1 Value1 = PCGMetadataAttribute::GetValueWithBroadcast<InType1>(OperationData.SourceAttributes[0], EntryKey1);
			InType2 Value2 = PCGMetadataAttribute::GetValueWithBroadcast<InType2>(OperationData.SourceAttributes[1], EntryKey2);
			InType3 Value3 = PCGMetadataAttribute::GetValueWithBroadcast<InType3>(OperationData.SourceAttributes[2], EntryKey3);

			OutType OutputValue = TernaryOp(Value1, Value2, Value3);

			OutputAttribute->SetValue(EntryKey1, OutputValue);
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

	return true;
}

template <typename InType1, typename InType2, typename InType3, typename InType4, typename OutType>
inline bool FPCGMetadataElementBase::DoQuaternaryOp(FOperationData& OperationData, TFunction<OutType(const InType1&, const InType2&, const InType3&, const InType4&)> QuaternaryOp) const
{
	check(OperationData.SourceAttributes[0]);
	check(OperationData.SourceAttributes[1]);
	check(OperationData.SourceAttributes[2]);
	check(OperationData.SourceAttributes[3]);
	check(OperationData.OutputAttributes[0]);

	// All values in OperationData.Iterators are UniquePtr. Dereference them here to make the syntax less cumbersome.
	// Also some iterators can be null, it means they need to use the first iterator (that should never be null).
	// We just need to make sure to not increment the first iterator multiple times in one loop.
	check(OperationData.Iterators[0]);

	bool bShouldIncrementIterator2 = OperationData.Iterators.Num() >= 2 && OperationData.Iterators[1];
	bool bShouldIncrementIterator3 = OperationData.Iterators.Num() >= 3 && OperationData.Iterators[2];
	bool bShouldIncrementIterator4 = OperationData.Iterators.Num() >= 4 && OperationData.Iterators[3];

	IPCGMetadataEntryIterator& Iterator1 = *OperationData.Iterators[0];
	IPCGMetadataEntryIterator& Iterator2 = bShouldIncrementIterator2 ? *OperationData.Iterators[1] : Iterator1;
	IPCGMetadataEntryIterator& Iterator3 = bShouldIncrementIterator3 ? *OperationData.Iterators[2] : Iterator1;
	IPCGMetadataEntryIterator& Iterator4 = bShouldIncrementIterator4 ? *OperationData.Iterators[3] : Iterator1;

	FPCGMetadataAttribute<OutType>* OutputAttribute = static_cast<FPCGMetadataAttribute<OutType>*>(OperationData.OutputAttributes[0]);
	InType1 DefaultValue1 = PCGMetadataAttribute::GetValueWithBroadcast<InType1>(OperationData.SourceAttributes[0], PCGInvalidEntryKey);
	InType2 DefaultValue2 = PCGMetadataAttribute::GetValueWithBroadcast<InType2>(OperationData.SourceAttributes[1], PCGInvalidEntryKey);
	InType3 DefaultValue3 = PCGMetadataAttribute::GetValueWithBroadcast<InType3>(OperationData.SourceAttributes[2], PCGInvalidEntryKey);
	InType3 DefaultValue4 = PCGMetadataAttribute::GetValueWithBroadcast<InType3>(OperationData.SourceAttributes[3], PCGInvalidEntryKey);
	OutType DefaultOutputValue = QuaternaryOp(DefaultValue1, DefaultValue2, DefaultValue3, DefaultValue4);
	OutputAttribute->SetDefaultValue(DefaultOutputValue);

	for (int32 i = 0; i < OperationData.NumberOfElementsToProcess; ++i)
	{
		PCGMetadataEntryKey EntryKey1 = *Iterator1;

		// If the entry key is invalid, nothing to do
		if (EntryKey1 != PCGInvalidEntryKey)
		{
			PCGMetadataEntryKey EntryKey2 = *Iterator2;
			PCGMetadataEntryKey EntryKey3 = *Iterator3;
			PCGMetadataEntryKey EntryKey4 = *Iterator4;

			InType1 Value1 = PCGMetadataAttribute::GetValueWithBroadcast<InType1>(OperationData.SourceAttributes[0], EntryKey1);
			InType2 Value2 = PCGMetadataAttribute::GetValueWithBroadcast<InType2>(OperationData.SourceAttributes[1], EntryKey2);
			InType3 Value3 = PCGMetadataAttribute::GetValueWithBroadcast<InType3>(OperationData.SourceAttributes[2], EntryKey3);
			InType4 Value4 = PCGMetadataAttribute::GetValueWithBroadcast<InType4>(OperationData.SourceAttributes[3], EntryKey4);

			OutType OutputValue = QuaternaryOp(Value1, Value2, Value3, Value4);

			OutputAttribute->SetValue(EntryKey1, OutputValue);
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

		if (bShouldIncrementIterator4)
		{
			++Iterator4;
		}
	}

	return true;
}

template <typename InType, typename OutType>
inline bool FPCGMetadataElementBase::DoUnaryOpMultipleOutputs(FOperationData & OperationData, TFunction<void(const InType&, TArray<OutType>&, uint32)> UnaryOp) const
{
	check(OperationData.SourceAttributes[0]);

	// All values in OperationData.Iterators are UniquePtr. Dereference them here to make the syntax less cumbersome.
	check(OperationData.Iterators[0]);
	IPCGMetadataEntryIterator& Iterator1 = *OperationData.Iterators[0];

	const uint32 NumOutputs = OperationData.OutputAttributes.Num();

	TArray<OutType> OutputValues;
	TArray<FPCGMetadataAttribute<OutType>*> OutputAttributes;

	// Output value array is reserved not resized. Allow us to validate that the callback properly pushed
	// the expected amount of outputs.
	OutputValues.Reserve(NumOutputs);
	OutputAttributes.SetNum(NumOutputs);

	InType DefaultValue = PCGMetadataAttribute::GetValueWithBroadcast<InType>(OperationData.SourceAttributes[0], PCGInvalidEntryKey);
	UnaryOp(DefaultValue, OutputValues, NumOutputs);

	if (OutputValues.Num() != NumOutputs)
	{
		UE_LOG(LogPCG, Error, TEXT("[DoUnaryOpMultipleOutputs] Callback didn't provided the number of expected outputs."));
		return false;
	}

	for (uint32 j = 0; j < NumOutputs; ++j)
	{
		// Output Attributes can be null if there is nothing connected to the output pin.
		OutputAttributes[j] = static_cast<FPCGMetadataAttribute<OutType>*>(OperationData.OutputAttributes[j]);
		if (OutputAttributes[j])
		{
			OutputAttributes[j]->SetDefaultValue(OutputValues[j]);
		}
	}

	for (int32 i = 0; i < OperationData.NumberOfElementsToProcess; ++i)
	{
		PCGMetadataEntryKey EntryKey = *Iterator1;

		// If the entry key is invalid, nothing to do
		if (EntryKey != PCGInvalidEntryKey)
		{
			InType Value = PCGMetadataAttribute::GetValueWithBroadcast<InType>(OperationData.SourceAttributes[0], EntryKey);

			// Make sure the array is emptied (but reserved)
			OutputValues.Empty(/*Slack=*/NumOutputs);
			UnaryOp(Value, OutputValues, NumOutputs);

			if (OutputValues.Num() != NumOutputs)
			{
				UE_LOG(LogPCG, Error, TEXT("[DoUnaryOpMultipleOutputs] Callback didn't provided the number of expected outputs."));
				return false;
			}

			for (uint32 j = 0; j < NumOutputs; ++j)
			{
				if (OutputAttributes[j])
				{
					OutputAttributes[j]->SetValue(EntryKey, OutputValues[j]);
				}
			}
		}

		++Iterator1;
	}

	return true;
}