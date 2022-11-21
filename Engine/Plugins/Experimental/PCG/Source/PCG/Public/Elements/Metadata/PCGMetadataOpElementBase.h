// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGElement.h"
#include "PCGSettings.h"

#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataAttributeWrapper.h"

#include "Containers/StaticArray.h"

#include "PCGMetadataOpElementBase.generated.h"

class FPCGMetadataAttributeBase;

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
 * Base class for all Metadata operations.
 * Metadata operation can work with attributes or properties. For example you could compute the addition between all points density and a constant from
 * a param data.
 * The output will be the duplication of the first input, with the same metadata + the result of the operation (either in an attribute or a property)
 * The new attribute can collide with one of the attributes in the incoming metadata. In this case, the attribute value will be overridden by the result
 * of the operation. It will also override the type of the attribute if it doesn't match the original.
 * 
 * We only support operations between points and between spatial data. They all need to match (or be a param data)
 * For example, if input 0 is a point data and input 1 is a spatial data, we fail.
 * 
 * You can specify the name of the attribute for each input and for the output.
 * If the input name is None, it will take the lastest attribute in the input metadata.
 * If the output name is None, it will take the input name.
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
	// ~Begin UObject interface
	virtual void PostLoad() override;
	// ~End UObject interface

	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Metadata; }
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	//~End UPCGSettings interface

	virtual FPCGAttributePropertySelector GetInputSource(uint32 Index) const { return FPCGAttributePropertySelector(); };

	virtual FName GetInputPinLabel(uint32 Index) const { return PCGPinConstants::DefaultInputLabel; }
	virtual uint32 GetInputPinNum() const { return 1; };

	virtual FName GetOutputPinLabel(uint32 Index) const { return PCGPinConstants::DefaultOutputLabel; }
	virtual uint32 GetOutputPinNum() const { return 1; }

	virtual bool IsSupportedInputType(uint16 TypeId, uint32 InputIndex, bool& bHasSpecialRequirement) const { return false; };
	virtual uint16 GetOutputType(uint16 InputTypeId) const { return InputTypeId; };
	virtual FName GetOutputAttributeName(FName BaseName, uint32 Index) const { return BaseName; }

	virtual bool HasDifferentOutputTypes() const { return false; }
	virtual TArray<uint16> GetAllOutputTypes() const { return TArray<uint16>(); };

	bool IsMoreComplexType(uint16 FirstType, uint16 SecondType) const;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Output")
	FPCGAttributePropertySelector OutputTarget;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FName OutputAttributeName_DEPRECATED = NAME_None;

	UPROPERTY()
	EPCGMetadataSettingsBaseMode Mode_DEPRECATED = EPCGMetadataSettingsBaseMode::Inferred;
#endif // WITH_EDITORONLY_DATA

	static constexpr uint32 MaxNumberOfOutputs = 4;

#if WITH_EDITORONLY_DATA
	// Useful for unit tests. Allow to force a connection to allow the node to do its operation, even if nothing is connected to it.
	TStaticArray<bool, MaxNumberOfOutputs> ForceOutputConnections{};
#endif // WITH_EDITORONLY_DATA
};


class FPCGMetadataElementBase : public FSimplePCGElement
{
public:
	struct FOperationData
	{
		int32 NumberOfElementsToProcess = -1;
		uint16 MostComplexInputType;
		uint16 OutputType;
		const UPCGMetadataSettingsBase* Settings = nullptr;

		TArray<FPCGPropertyAttributeIterator> InputIterators;
		TArray<FPCGPropertyAttributeIterator> OutputIterators;
	};

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;

	virtual bool DoOperation(FOperationData& OperationData) const = 0;

	/* All operations can have a fixed number of inputs and a variable number of outputs.
	* Each output need to have its own callback, all taking the exact number of "const InType&" as input
	* and each can return a different output type.
	*/
	template <typename InType, typename... Callbacks>
	bool DoUnaryOp(FOperationData& OperationData, Callbacks&& ...InCallbacks) const;

	template <typename InType1, typename InType2, typename... Callbacks>
	bool DoBinaryOp(FOperationData& OperationData, Callbacks&& ...InCallbacks) const;

	template <typename InType1, typename InType2, typename InType3, typename... Callbacks>
	bool DoTernaryOp(FOperationData& OperationData, Callbacks&& ...InCallbacks) const;

	template <typename InType1, typename InType2, typename InType3, typename InType4, typename... Callbacks>
	bool DoQuaternaryOp(FOperationData& OperationData, Callbacks&& ...InCallbacks) const;
};

template <typename InType, typename... Callbacks>
inline bool FPCGMetadataElementBase::DoUnaryOp(FOperationData& OperationData, Callbacks&& ...InCallbacks) const
{
	check(OperationData.InputIterators[0].IsValid());

	const uint32 NumOutputs = OperationData.OutputIterators.Num();
	constexpr uint32 NumCallbacks = (uint32)sizeof...(InCallbacks);
	check(NumOutputs == NumCallbacks);

	InType DefaultValue{};
	if (OperationData.InputIterators[0].GetWrapper().Get<InType>(PCGInvalidEntryKey, DefaultValue))
	{
		// Fold expressions. It will un-roll the packed argument "InCallbacks", and we can use "InCallbacks" as the current unrolled argument.
		// In this case, we have multiple callbacks that all take "const InType&" as input, but can return different outputs.
		// We use this return value to know in which type we need to cast our attribute.
		uint32 j = 0;
		([&]
		{
			check(j < NumOutputs);
			auto OutValue = InCallbacks(DefaultValue);
			typedef decltype(OutValue) OutType;

			OperationData.OutputIterators[j++].GetWrapper().Set<OutType>(PCGInvalidEntryKey, OutValue);
		} (), ...);
	}

	InType Value{};

	for (int32 i = 0; i < OperationData.NumberOfElementsToProcess; ++i)
	{
		if (OperationData.InputIterators[0].GetAndAdvance<InType>(Value))
		{
			uint32 j = 0;
			([&]
			{
				check(j < NumOutputs);
				auto OutValue = InCallbacks(Value);
				typedef decltype(OutValue) OutType;

				OperationData.OutputIterators[j++].SetAndAdvance<OutType>(OutValue);
			} (), ...);
		}
	}

	return true;
}

template <typename InType1, typename InType2, typename... Callbacks>
inline bool FPCGMetadataElementBase::DoBinaryOp(FOperationData& OperationData, Callbacks&& ...InCallbacks) const
{
	check(OperationData.InputIterators[0].IsValid());
	check(OperationData.InputIterators[1].IsValid());

	const uint32 NumOutputs = OperationData.OutputIterators.Num();
	constexpr uint32 NumCallbacks = (uint32)sizeof...(InCallbacks);
	check(NumOutputs == NumCallbacks);

	InType1 DefaultValue1{};
	InType2 DefaultValue2{};

	if (OperationData.InputIterators[0].GetWrapper().Get<InType1>(PCGInvalidEntryKey, DefaultValue1) &&
		OperationData.InputIterators[1].GetWrapper().Get<InType2>(PCGInvalidEntryKey, DefaultValue2))
	{
		// Fold expression, cf. Unary op
		uint32 j = 0;
		([&]
		{
			check(j < NumOutputs);
			auto OutValue = InCallbacks(DefaultValue1, DefaultValue2);
			typedef decltype(OutValue) OutType;

			OperationData.OutputIterators[j++].GetWrapper().Set<OutType>(PCGInvalidEntryKey, OutValue);
		} (), ...);
	}

	InType1 Value1{};
	InType2 Value2{};

	for (int32 i = 0; i < OperationData.NumberOfElementsToProcess; ++i)
	{
		if (OperationData.InputIterators[0].GetAndAdvance<InType1>(Value1) &&
			OperationData.InputIterators[1].GetAndAdvance<InType2>(Value2))
		{
			uint32 j = 0;
			([&]
			{
				check(j < NumOutputs);
				auto OutValue = InCallbacks(Value1, Value2);
				typedef decltype(OutValue) OutType;

				OperationData.OutputIterators[j++].SetAndAdvance<OutType>(OutValue);
			} (), ...);
		}
	}

	return true;
}

template <typename InType1, typename InType2, typename InType3, typename... Callbacks>
inline bool FPCGMetadataElementBase::DoTernaryOp(FOperationData& OperationData, Callbacks&& ...InCallbacks) const
{
	check(OperationData.InputIterators[0].IsValid());
	check(OperationData.InputIterators[1].IsValid());
	check(OperationData.InputIterators[2].IsValid());

	const uint32 NumOutputs = OperationData.OutputIterators.Num();
	constexpr uint32 NumCallbacks = (uint32)sizeof...(InCallbacks);
	check(NumOutputs == NumCallbacks);

	InType1 DefaultValue1{};
	InType2 DefaultValue2{};
	InType3 DefaultValue3{};

	if (OperationData.InputIterators[0].GetWrapper().Get<InType1>(PCGInvalidEntryKey, DefaultValue1) &&
		OperationData.InputIterators[1].GetWrapper().Get<InType2>(PCGInvalidEntryKey, DefaultValue2) &&
		OperationData.InputIterators[2].GetWrapper().Get<InType3>(PCGInvalidEntryKey, DefaultValue3))
	{
		// Fold expression, cf. Unary op
		uint32 j = 0;
		([&]
		{
			check(j < NumOutputs);
			auto OutValue = InCallbacks(DefaultValue1, DefaultValue2, DefaultValue3);
			typedef decltype(OutValue) OutType;

			OperationData.OutputIterators[j++].GetWrapper().Set<OutType>(PCGInvalidEntryKey, OutValue);
		} (), ...);
	}

	InType1 Value1{};
	InType2 Value2{};
	InType3 Value3{};

	for (int32 i = 0; i < OperationData.NumberOfElementsToProcess; ++i)
	{
		if (OperationData.InputIterators[0].GetAndAdvance<InType1>(Value1) &&
			OperationData.InputIterators[1].GetAndAdvance<InType2>(Value2) &&
			OperationData.InputIterators[2].GetAndAdvance<InType3>(Value3))
		{
			uint32 j = 0;
			([&]
			{
				check(j < NumOutputs);
				auto OutValue = InCallbacks(Value1, Value2, Value3);
				typedef decltype(OutValue) OutType;

				OperationData.OutputIterators[j++].SetAndAdvance<OutType>(OutValue);
			} (), ...);
		}
	}

	return true;
}

template <typename InType1, typename InType2, typename InType3, typename InType4, typename... Callbacks>
inline bool FPCGMetadataElementBase::DoQuaternaryOp(FOperationData& OperationData, Callbacks&& ...InCallbacks) const
{
	check(OperationData.InputIterators[0].IsValid());
	check(OperationData.InputIterators[1].IsValid());
	check(OperationData.InputIterators[2].IsValid());
	check(OperationData.InputIterators[3].IsValid());

	const uint32 NumOutputs = OperationData.OutputIterators.Num();
	constexpr uint32 NumCallbacks = (uint32)sizeof...(InCallbacks);
	check(NumOutputs == NumCallbacks);

	InType1 DefaultValue1{};
	InType2 DefaultValue2{};
	InType3 DefaultValue3{};
	InType4 DefaultValue4{};
	
	if (OperationData.InputIterators[0].GetWrapper().Get<InType1>(PCGInvalidEntryKey, DefaultValue1) &&
		OperationData.InputIterators[1].GetWrapper().Get<InType2>(PCGInvalidEntryKey, DefaultValue2) &&
		OperationData.InputIterators[2].GetWrapper().Get<InType3>(PCGInvalidEntryKey, DefaultValue3) &&
		OperationData.InputIterators[3].GetWrapper().Get<InType4>(PCGInvalidEntryKey, DefaultValue4))
	{
		// Fold expression, cf. Unary op
		uint32 j = 0;
		([&]
		{
			check(j < NumOutputs);
			auto OutValue = InCallbacks(DefaultValue1, DefaultValue2, DefaultValue3, DefaultValue4);
			typedef decltype(OutValue) OutType;

			OperationData.OutputIterators[j++].GetWrapper().Set<OutType>(PCGInvalidEntryKey, OutValue);
		} (), ...);
	}

	InType1 Value1{};
	InType2 Value2{};
	InType3 Value3{};
	InType4 Value4{};

	for (int32 i = 0; i < OperationData.NumberOfElementsToProcess; ++i)
	{
		if (OperationData.InputIterators[0].GetAndAdvance<InType1>(Value1) &&
			OperationData.InputIterators[1].GetAndAdvance<InType2>(Value2) &&
			OperationData.InputIterators[2].GetAndAdvance<InType3>(Value3) &&
			OperationData.InputIterators[3].GetAndAdvance<InType4>(Value4))
		{
			uint32 j = 0;
			([&]
			{
				check(j < NumOutputs);
				auto OutValue = InCallbacks(Value1, Value2, Value3, Value4);
				typedef decltype(OutValue) OutType;

				OperationData.OutputIterators[j++].SetAndAdvance<OutType>(OutValue);
			} (), ...);
		}
	}

	return true;
}