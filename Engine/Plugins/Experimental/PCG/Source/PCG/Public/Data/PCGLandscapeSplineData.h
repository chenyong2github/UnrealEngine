// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGPolyLineData.h"

#include "PCGLandscapeSplineData.generated.h"

class ULandscapeSplinesComponent;

UCLASS(BlueprintType, ClassGroup=(Procedural))
class UPCGLandscapeSplineData : public UPCGPolyLineData
{
	GENERATED_BODY()
public:
	void Initialize(ULandscapeSplinesComponent* InSplineComponent);

	//~Begin UPCGPolyLineData interface
	virtual int GetNumSegments() const override;
	virtual float GetSegmentLength(int SegmentIndex) const override;
	virtual FVector GetLocationAtDistance(int SegmentIndex, float Distance) const override;
	//~End UPCGPolyLineData interface

	//~Begin UPCGSpatialDataWithPointCache interface
	virtual const UPCGPointData* CreatePointData(FPCGContextPtr Context) const override;
	//~End UPCGSpatialDataWithPointCache interface

	//~Begin UPCGSpatialData interface
	virtual FBox GetBounds() const override;
	virtual float GetDensityAtPosition(const FVector& InPosition) const override;
	//~End

protected:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = SourceData)
	TSoftObjectPtr<ULandscapeSplinesComponent> Spline;
};
