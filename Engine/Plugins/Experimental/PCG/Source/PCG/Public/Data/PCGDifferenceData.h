// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSpatialData.h"

#include "PCGDifferenceData.generated.h"

class UPCGUnionData;

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGDifferenceData : public UPCGSpatialDataWithPointCache
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	void Initialize(const UPCGSpatialData* InData);

	UFUNCTION(BlueprintCallable, Category = SpatialData)
	void AddDifference(const UPCGSpatialData* InDifference);

	//~Begin UPCGSpatialData interface
	virtual int GetDimension() const override;
	virtual FBox GetBounds() const override;
	virtual FBox GetStrictBounds() const override;
	virtual float GetDensityAtPosition(const FVector& InPosition) const override;
	//~End UPCGSpatialData interface

	//~Begin UPCGSpatialDataWithPointCache interface
	virtual const UPCGPointData* CreatePointData() const override;
	//~End UPCGSpatialDataWithPointCache interface
protected:
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = SpatialData)
	TObjectPtr<const UPCGSpatialData> Source = nullptr;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = SpatialData)
	TObjectPtr<const UPCGSpatialData> Difference = nullptr;

	UPROPERTY()
	TObjectPtr<UPCGUnionData> DifferencesUnion = nullptr;
};