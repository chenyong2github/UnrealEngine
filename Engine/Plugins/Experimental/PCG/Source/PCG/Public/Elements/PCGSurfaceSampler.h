// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "PCGElement.h"

#include "PCGSurfaceSampler.generated.h"

class FPCGSurfaceSamplerElement;

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGSurfaceSamplerSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	// TODO

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class FPCGSurfaceSamplerElement : public FSimpleTypedPCGElement<UPCGSurfaceSamplerSettings>
{
public:
	virtual bool Execute(FPCGContextPtr Context) const override;
};