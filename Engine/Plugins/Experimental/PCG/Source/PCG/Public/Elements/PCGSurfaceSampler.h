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
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ClampMin="0"))
	float PointsPerSquaredMeter = 0.1f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ClampMin="0"))
	float PointRadius = 100.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ClampMin="0"))
	float Looseness = 1.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bApplyDensityToPoints = true;

#if WITH_EDITOR
	//~Begin UPCGSettings interface
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("SurfaceSamplerNode")); }
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	// ~End UPCGSettings interface
};

class FPCGSurfaceSamplerElement : public FSimpleTypedPCGElement<UPCGSurfaceSamplerSettings>
{
public:
	virtual bool Execute(FPCGContextPtr Context) const override;
};