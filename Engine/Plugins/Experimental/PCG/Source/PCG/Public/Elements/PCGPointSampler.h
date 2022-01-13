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

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ClampMin="0", ClampMax="1"))
	float Ratio = 0.1f;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class FPCGPointSamplerElement : public FSimpleTypedPCGElement<UPCGPointSamplerSettings>
{
public:
	virtual bool Execute(FPCGContextPtr Context) const override;
};