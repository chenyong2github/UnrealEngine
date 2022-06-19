// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSurfaceData.h"
#include "Elements/PCGSurfaceSampler.h"

#include "PCGLandscapeData.generated.h"

class ALandscapeProxy;
class UPCGPolyLineData;

USTRUCT()
struct PCG_API FPCGLandscapeDataPoint
{
	GENERATED_BODY()

	FPCGLandscapeDataPoint() = default;
	FPCGLandscapeDataPoint(int InX, int InY, float InHeight);

	UPROPERTY()
	int X = 0;

	UPROPERTY()
	int Y = 0;

	UPROPERTY()
	float Height = 0;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGLandscapeData : public UPCGSurfaceData
{
	GENERATED_BODY()

public:
	void Initialize(ALandscapeProxy* InLandscape, const FBox& InBounds);

	// ~Begin UPCGData interface
	virtual EPCGDataType GetDataType() const override { return EPCGDataType::Landscape | Super::GetDataType(); }
	// ~End UPCGData interface

	// ~Begin UPGCSpatialData interface
	virtual FBox GetBounds() const override;
	virtual FBox GetStrictBounds() const override;
	virtual bool SamplePoint(const FTransform& Transform, const FBox& Bounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const override;
	virtual bool HasNonTrivialTransform() const override { return true; }
	// ~End UPGCConcreteData interface

	// ~Begin UPCGSpatialDataWithPointCache interface
	virtual bool SupportsBoundedPointData() const { return true; }
	virtual const UPCGPointData* CreatePointData(FPCGContext* Context) const override { return CreatePointData(Context, FBox(EForceInit::ForceInit)); }
	virtual const UPCGPointData* CreatePointData(FPCGContext* Context, const FBox& InBounds) const override;
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
