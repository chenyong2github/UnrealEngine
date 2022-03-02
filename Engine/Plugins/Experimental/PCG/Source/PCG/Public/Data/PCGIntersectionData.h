// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGSpatialData.h"

#include "PCGIntersectionData.generated.h"

UENUM()
enum class EPCGIntersectionDensityFunction : uint8
{
	Multiply,
	Minimum
};

/**
* Generic intersection class that delays operations as long as possible.
*/
UCLASS(BlueprintType, ClassGroup=(Procedural))
class UPCGIntersectionData : public UPCGSpatialDataWithPointCache
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	void Initialize(const UPCGSpatialData* InA, const UPCGSpatialData* InB);

	//~Begin UPCGSpatialData interface
	virtual int GetDimension() const override;
	virtual FBox GetBounds() const override;
	virtual FBox GetStrictBounds() const override;
	virtual float GetDensityAtPosition(const FVector& InPosition) const override;
	virtual FVector TransformPosition(const FVector& InPosition) const override;
	virtual FPCGPoint TransformPoint(const FPCGPoint& InPoint) const override;
	virtual bool HasNonTrivialTransform() const override;
	//~End UPCGSpatialData interface

	//~Begin UPCGSpatialDataWithPointCache interface
	virtual const UPCGPointData* CreatePointData(FPCGContextPtr Context) const override;
	//~End UPCGSpatialDataWithPointCache interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGIntersectionDensityFunction DensityFunction = EPCGIntersectionDensityFunction::Multiply;

protected:
	UPCGPointData* CreateAndFilterPointData(FPCGContextPtr Context, const UPCGSpatialData* X, const UPCGSpatialData* Y) const;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = SpatialData)
	TObjectPtr<const UPCGSpatialData> A = nullptr;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = SpatialData)
	TObjectPtr<const UPCGSpatialData> B = nullptr;

	UPROPERTY()
	FBox CachedBounds = FBox(EForceInit::ForceInit);

	UPROPERTY()
	FBox CachedStrictBounds = FBox(EForceInit::ForceInit);
};