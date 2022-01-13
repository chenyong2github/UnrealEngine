// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSpatialData.h"

#include "PCGUnionData.generated.h"

UENUM()
enum class EPCGUnionType : uint8
{
	LeftToRightPriority,
	RightToLeftPriority,
	KeepAB,
	ABMinusIntersection
};

UCLASS(BlueprintType, ClassGroup=(Procedural))
class UPCGUnionData : public UPCGSpatialDataWithPointCache
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	void Initialize(const UPCGSpatialData* InA, const UPCGSpatialData* InB);

	UFUNCTION(BlueprintCallable, Category = SpatialData)
	void AddData(const UPCGSpatialData* InC);

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
	TObjectPtr<const UPCGSpatialData> A = nullptr;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = SpatialData)
	TObjectPtr<const UPCGSpatialData> B = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGUnionType UnionType = EPCGUnionType::LeftToRightPriority;

	UPROPERTY()
	FBox CachedBounds = FBox(EForceInit::ForceInit);

	UPROPERTY()
	FBox CachedStrictBounds = FBox(EForceInit::ForceInit);
};