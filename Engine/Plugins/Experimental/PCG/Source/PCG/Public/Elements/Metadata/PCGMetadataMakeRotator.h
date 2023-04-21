// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Metadata/PCGMetadataOpElementBase.h"

#include "PCGMetadataMakeRotator.generated.h"

namespace PCGMetadataMakeRotatorConstants
{
	const FName XLabel = TEXT("X");
	const FName YLabel = TEXT("Y");
	const FName ZLabel = TEXT("Z");
	const FName ForwardLabel = TEXT("Forward");
	const FName RightLabel = TEXT("Right");
	const FName UpLabel = TEXT("Up");
}

UENUM()
enum class EPCGMetadataMakeRotatorOp : uint8
{
	MakeRotFromX,
	MakeRotFromY,
	MakeRotFromZ,
	MakeRotFromXY,
	MakeRotFromYX,
	MakeRotFromXZ,
	MakeRotFromZX,
	MakeRotFromYZ,
	MakeRotFromZY,
	MakeRotFromAxis
};

/* Create a Rotator from 1, 2 or 3 axis. */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGMetadataMakeRotatorSettings : public UPCGMetadataSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	//~Begin UPCGSettings interface
	virtual FName GetDefaultNodeName() const override;
	virtual FText GetDefaultNodeTitle() const override;
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
#endif
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
	//~End UPCGSettings interface

	FPCGAttributePropertySelector GetInputSource(uint32 Index) const override;

	virtual FName GetInputPinLabel(uint32 Index) const override;
	virtual uint32 GetInputPinNum() const override;

	virtual bool IsSupportedInputType(uint16 TypeId, uint32 InputIndex, bool& bHasSpecialRequirement) const override;
	virtual uint16 GetOutputType(uint16 InputTypeId) const override;

protected:
	//~Begin UPCGSettings interface
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Input)
	FPCGAttributePropertySelector InputSource1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Input, meta = (EditCondition = "Operation != EPCGMetadataMakeRotatorOp::MakeRotFromX && Operation != EPCGMetadataMakeRotatorOp::MakeRotFromY && Operation != EPCGMetadataMakeRotatorOp::MakeRotFromZ", EditConditionHides))
	FPCGAttributePropertySelector InputSource2;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Input, meta = (EditCondition = "Operation == EPCGMetadataMakeRotatorOp::MakeRotFromAxis", EditConditionHides))
	FPCGAttributePropertySelector InputSource3;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGMetadataMakeRotatorOp Operation = EPCGMetadataMakeRotatorOp::MakeRotFromAxis;
};

class FPCGMetadataMakeRotatorElement : public FPCGMetadataElementBase
{
protected:
	virtual bool DoOperation(FOperationData& OperationData) const override;
};
