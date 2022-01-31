// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGElement.h"
#include "PCGNode.h"

#include "PCGPointSampler.generated.h"

class FPCGPointSamplerElement;

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGPointSamplerSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("PointSamplerNode")); }
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ClampMin="0", ClampMax="1"))
	float Ratio = 0.1f;
};

class FPCGPointSamplerElement : public FSimpleTypedPCGElement<UPCGPointSamplerSettings>
{
protected:
	virtual bool ExecuteInternal(FPCGContextPtr Context) const override;
};