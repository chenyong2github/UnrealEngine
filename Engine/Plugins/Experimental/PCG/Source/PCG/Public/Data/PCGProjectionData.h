// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGSpatialData.h"
#include "PCGPointData.h"

#include "PCGProjectionData.generated.h"

/**
* Generic projection class (A projected onto B) that intercepts spatial queries
*/
UCLASS(BlueprintType, ClassGroup=(Procedural))
class UPCGProjectionData : public UPCGSpatialDataWithPointCache
{
	GENERATED_BODY()
public:
	void Initialize(const UPCGSpatialData* InSource, const UPCGSpatialData* InTarget);

	//~Begin UPCGSpatialData interface
	virtual int GetDimension() const override;
	virtual FBox GetBounds() const override;
	virtual FBox GetStrictBounds() const override;
	virtual float GetDensityAtPosition(const FVector& InPosition) const override;
	virtual FVector GetNormal() const override;
	//~End UPCGSpatialData interface

	//~Begin UPCGSpatialDataWithPointCache interface
	virtual const UPCGPointData* CreatePointData() const override;
	//~End UPCGSpatialDataWithPointCache interface

protected:
	virtual FVector ProjectPosition(const FVector& InPosition) const;
	virtual FPCGPoint ProjectPoint(const FPCGPoint& InPoint) const;

	FBox ProjectBounds(const FBox& InBounds) const;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = SpatialData)
	TObjectPtr<const UPCGSpatialData> Source = nullptr;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = SpatialData)
	TObjectPtr<const UPCGSpatialData> Target = nullptr;

	FBox CachedBounds = FBox(EForceInit::ForceInit);
	FBox CachedStrictBounds = FBox(EForceInit::ForceInit);
};

