// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSurfaceData.h"
#include "Elements/PCGSurfaceSampler.h"

#include "PCGLandscapeData.generated.h"

class ALandscapeProxy;
class UPCGPolyLineData;

USTRUCT()
struct FPCGLandscapeDataPoint
{
	GENERATED_BODY()

	FPCGLandscapeDataPoint() = default;
	FPCGLandscapeDataPoint(int InX, int InY, float InHeight);

	UPROPERTY()
	int X;

	UPROPERTY()
	int Y;

	UPROPERTY()
	float Height;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGLandscapeData : public UPCGSurfaceData
{
	GENERATED_BODY()

public:
	void Initialize(ALandscapeProxy* InLandscape, const FBox& InBounds);

	// ~Begin UPGCSpatialData interface
	virtual FBox GetBounds() const override;
	virtual FBox GetStrictBounds() const override;
	virtual float GetDensityAtPosition(const FVector& InPosition) const override;
	virtual FVector TransformPosition(const FVector& InPosition) const override;
	virtual FPCGPoint TransformPoint(const FPCGPoint& InPoint) const override;
	virtual bool HasNonTrivialTransform() const override { return true; }
	// ~End UPGCConcreteData interface

	// ~Begin UPCGSpatialDataWithPointCache interface
	virtual const UPCGPointData* CreatePointData() const override;
	// ~End UPCGConcreteDataWithPointCache interface

	// TODO: add on property changed to clear cached data
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = SourceData)
	TSoftObjectPtr<ALandscapeProxy> Landscape;

	// TODO: add on property changed to clear cached data
	//UPROPERTY(BlueprintReadWrite, EditAnywhere)
	//TObjectPtr<UPCGSurfaceSamplerSettings> ImplicitSamplerSettings;
protected:
	UPROPERTY()
	TArray<FPCGLandscapeDataPoint> LandscapePoints;

	UPROPERTY()
	FBox Bounds = FBox(EForceInit::ForceInit);
};