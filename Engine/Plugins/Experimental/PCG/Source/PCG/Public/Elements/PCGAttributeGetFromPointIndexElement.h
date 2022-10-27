// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGElement.h"
#include "PCGSettings.h"

#include "PCGAttributeGetFromPointIndexElement.generated.h"

namespace PCGAttributeGetFromPointIndexConstants
{
	const FName OutputAttributeLabel = TEXT("Attribute");
	const FName OutputPointLabel = TEXT("Point");
}

/**
* Get the attribute of a point given its index. The result will be in a ParamData.
* There is also a second output that will output the selected point. This point will be output
* even if the attribute doesn't exist.
* 
* The Index can be overridden by a second Params input.
*/
UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGAttributeGetFromPointIndexSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Metadata; }
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	//~End UPCGSettings interface

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName InputAttributeName = NAME_None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	int32 Index = 0;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};


class FPCGAttributeGetFromPointIndexElement : public FSimplePCGElement
{
public:
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
