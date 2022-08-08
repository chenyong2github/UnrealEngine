// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSurfaceData.h"

#include "PCGLandscapeData.generated.h"

class ALandscapeProxy;
class ULandscapeInfo;
struct FPCGLandscapeCache;

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGLandscapeData : public UPCGSurfaceData
{
	GENERATED_BODY()

public:
	void Initialize(ALandscapeProxy* InLandscape, const FBox& InBounds, bool bInHeightOnly, bool bInUseMetadata);

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

	bool IsUsingMetadata() const { return bUseMetadata; }

protected:
	UPROPERTY()
	FBox Bounds = FBox(EForceInit::ForceInit);

	UPROPERTY()
	bool bHeightOnly = false;

	UPROPERTY()
	bool bUseMetadata = true;

private:
	// Transient data
	ULandscapeInfo* LandscapeInfo = nullptr;
	FPCGLandscapeCache* LandscapeCache = nullptr;
};
