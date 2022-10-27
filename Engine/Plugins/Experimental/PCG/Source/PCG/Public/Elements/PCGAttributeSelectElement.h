// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGElement.h"
#include "PCGSettings.h"

#include "PCGAttributeSelectElement.generated.h"

namespace PCGAttributeSelectConstants
{
	const FName OutputAttributeLabel = TEXT("Attribute");
	const FName OutputPointLabel = TEXT("Point");
}

UENUM()
enum class EPCGAttributeSelectOperation
{
	Min,
	Max,
	Median
};

UENUM()
enum class EPCGAttributeSelectAxis
{
	X,
	Y,
	Z,
	W,
	CustomAxis
};

/**
* Take all the entries/points from the input and perform a select operation on the given attribute on the given axis 
* (if the attribute is a vector) and output the result into a ParamData.
* It will also output the selected point if the input is a PointData.
* 
* Only support vector attributes and scalar attributes.
* 
* CustomAxis is overridable.
* 
* In case of the median operation, and the number of elements is even, we arbitrarily chose a point (Index = Num / 2)
*
* If the OutputAttributeName is None, we will use InputAttributeName.
*/
UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGAttributeSelectSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Metadata; }
#endif

	virtual FName AdditionalTaskName() const override;

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	//~End UPCGSettings interface

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName InputAttributeName = NAME_None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName OutputAttributeName = NAME_None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGAttributeSelectOperation Operation = EPCGAttributeSelectOperation::Min;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGAttributeSelectAxis Axis = EPCGAttributeSelectAxis::X;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Axis == EPCGAttributeSelectAxis::CustomAxis", EditConditionHides))
	FVector4 CustomAxis = FVector4::Zero();

protected:
	virtual FPCGElementPtr CreateElement() const override;
};


class FPCGAttributeSelectElement : public FSimplePCGElement
{
public:
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};