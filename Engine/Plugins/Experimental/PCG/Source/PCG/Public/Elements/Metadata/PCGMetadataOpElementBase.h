// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGElement.h"
#include "PCGSettings.h"

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

	virtual FName GetInputPinLabel(uint32 Index) const { return NAME_None; }
	virtual uint32 GetInputPinNum() const { return 0; };

	virtual bool IsSupportedInputType(uint16 TypeId, uint32 InputIndex, bool& bHasSpecialRequirement) const { return false; };
	virtual uint16 GetOutputType(uint16 InputTypeId) const { return InputTypeId; };

	bool IsMoreComplexType(uint16 FirstType, uint16 SecondType) const;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Output)
	FName OutputAttributeName = NAME_None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGMetadataSettingsBaseMode Mode = EPCGMetadataSettingsBaseMode::Inferred;
};


class FPCGMetadataElementBase : public FSimplePCGElement
{
public:
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;

	struct FOperationData
	{
		TArray<const FPCGMetadataAttributeBase*> SourceAttributes;
		int32 NumberOfElementsToProcess = -1;
		uint16 OutputType;
		FPCGMetadataAttributeBase* OutputAttribute = nullptr;
		const UPCGMetadataSettingsBase* Settings = nullptr;
		TArray<TUniquePtr<IPCGMetadataEntryIterator>> Iterators;
	};

	virtual bool DoOperation(FOperationData& OperationData) const { return true; };
};