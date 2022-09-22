// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGElement.h"
#include "PCGSettings.h"
#include "Elements/Metadata/PCGMetadataOpElementBase.h"

#include "PCGMetadataBitwiseOpElement.generated.h"

UENUM()
enum class EPCGMedadataBitwiseOperation : uint16
{
	BitwiseAnd,
	BitwiseNot,
	BitwiseOr,
	BitwiseXor
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGMetadataBitwiseSettings : public UPCGMetadataSettingsBase
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override;
#endif
	//~End UPCGSettings interface

	//~Begin UPCGMetadataSettingsBase interface
	virtual FName GetInputAttributeNameWithOverride(uint32 Index, UPCGParamData* Params) const override;

	virtual FName GetInputPinLabel(uint32 Index) const override;
	virtual uint32 GetInputPinNum() const override;

	virtual bool IsSupportedInputType(uint16 TypeId, uint32 InputIndex, bool& bHasSpecialRequirement) const override;
	virtual uint16 GetOutputType(uint16 InputTypeId) const override;
	//~End UPCGMetadataSettingsBase interface

protected:
	//~Begin UPCGSettings interface
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGMedadataBitwiseOperation Operation = EPCGMedadataBitwiseOperation::BitwiseAnd;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Input)
	FName Input1AttributeName = NAME_None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Input, meta = (EditCondition = "Operation != EPCGMedadataBitwiseOperation::BitwiseNot", EditConditionHides))
	FName Input2AttributeName = NAME_None;
};

class FPCGMetadataBitwiseElement : public FPCGMetadataElementBase
{
protected:
	virtual bool DoOperation(FOperationData& OperationData) const override;
};
