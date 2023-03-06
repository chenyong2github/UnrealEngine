// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGSpatialData.h"

#include "PCGDifferenceData.generated.h"

struct FPropertyChangedEvent;

class UPCGUnionData;

UENUM()
enum class EPCGDifferenceDensityFunction : uint8
{
	Minimum,
	ClampedSubstraction,
	Binary
};

UENUM()
enum class EPCGDifferenceMode : uint8
{
	Inferred,
	Continuous,
	Discrete
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGDifferenceData : public UPCGSpatialDataWithPointCache
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	void Initialize(const UPCGSpatialData* InData);

	UFUNCTION(BlueprintCallable, Category = SpatialData)
	void AddDifference(const UPCGSpatialData* InDifference);

	UFUNCTION(BlueprintCallable, Category = Settings)
	void SetDensityFunction(EPCGDifferenceDensityFunction InDensityFunction);

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = SpatialData)
	bool bDiffMetadata = true;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ~Begin UPCGData interface
	virtual EPCGDataType GetDataType() const override { return EPCGDataType::Spatial; }
	virtual void VisitDataNetwork(TFunctionRef<void(const UPCGData*)> Action) const override;

protected:
	virtual FPCGCrc ComputeCrc() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar) const override;
	// ~End UPCGData interface

public:
	//~Begin UPCGSpatialData interface
	virtual int GetDimension() const override;
	virtual FBox GetBounds() const override;
	virtual FBox GetStrictBounds() const override;
	virtual bool SamplePoint(const FTransform& Transform, const FBox& Bounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const override;
	virtual bool HasNonTrivialTransform() const override;
	virtual const UPCGSpatialData* FindShapeFromNetwork(const int InDimension) const override { return Source ? Source->FindShapeFromNetwork(InDimension) : nullptr; }
	virtual const UPCGSpatialData* FindFirstConcreteShapeFromNetwork() const override { return Source ? Source->FindFirstConcreteShapeFromNetwork() : nullptr; }
protected:
	virtual UPCGSpatialData* CopyInternal() const override;
	//~End UPCGSpatialData interface

public:
	//~Begin UPCGSpatialDataWithPointCache interface
	virtual const UPCGPointData* CreatePointData(FPCGContext* Context) const override;
	//~End UPCGSpatialDataWithPointCache interface
protected:
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = SpatialData)
	TObjectPtr<const UPCGSpatialData> Source = nullptr;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = SpatialData)
	TObjectPtr<const UPCGSpatialData> Difference = nullptr;

	UPROPERTY()
	TObjectPtr<UPCGUnionData> DifferencesUnion = nullptr;

	UPROPERTY(BlueprintSetter = SetDensityFunction, EditAnywhere, Category = Settings)
	EPCGDifferenceDensityFunction DensityFunction = EPCGDifferenceDensityFunction::Minimum;
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif
