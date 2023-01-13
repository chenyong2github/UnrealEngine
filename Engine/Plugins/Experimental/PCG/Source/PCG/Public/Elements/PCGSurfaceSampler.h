// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Math/Box.h"
#include "PCGSettings.h"

#include "PCGSurfaceSampler.generated.h"

class UPCGNode;
class UPCGPointData;
class UPCGSpatialData;

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGSurfaceSamplerSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPCGSurfaceSamplerSettings();

	//~Begin UObject interface
	virtual void PostLoad() override;
	//~End UObject interface

	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("SurfaceSampler")); }
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Sampler; }
	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

#if WITH_EDITOR
	virtual void ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void ApplyDeprecation(UPCGNode* InOutNode) override;
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	// ~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ClampMin="0"))
	float PointsPerSquaredMeter = 0.1f;

	UPROPERTY()
	float PointRadius_DEPRECATED = 0.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FVector PointExtents = FVector(100.0f);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ClampMin="0"))
	float Looseness = 1.0f;

	/** If no Bounding Shape input is provided the actor bounds are used to limit the sample generation area.
	* This option allows ignoring the actor bounds and generating over the entire surface. Use with caution as this
	* may generate a lot of points.
	*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bUnbounded = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Points")
	bool bApplyDensityToPoints = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Points", meta=(ClampMin="0", ClampMax="1"))
	float PointSteepness = 0.5f;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient, BlueprintReadWrite, EditAnywhere, Category = "Settings|Debug")
	bool bKeepZeroDensityPoints = false;
#endif
};

class FPCGSurfaceSamplerElement : public FSimplePCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

namespace PCGSurfaceSampler
{
	struct FSurfaceSamplerSettings
	{
		const UPCGSurfaceSamplerSettings* Settings = nullptr;

		float PointsPerSquaredMeter = 1.0f;
		FVector PointExtents = FVector::One() * 0.5f;
		float Looseness = 0.0f;
		bool bApplyDensityToPoints = false;
		float PointSteepness = 0.0f;
#if WITH_EDITORONLY_DATA
		bool bKeepZeroDensityPoints = false;
#endif

		bool Initialize(const UPCGSurfaceSamplerSettings* InSettings, FPCGContext* Context, const FBox& InputBounds);
		FIntVector2 ComputeCellIndices(int32 Index) const;

		/** Computed values **/
		FVector InterstitialDistance;
		FVector InnerCellSize;
		FVector CellSize;

		int32 CellMinX;
		int32 CellMaxX;
		int32 CellMinY;
		int32 CellMaxY;
		int32 CellCount;
		int64 TargetPointCount;
		float Ratio;
		int Seed;

		FVector::FReal InputBoundsMaxZ;
	};

	UPCGPointData* SampleSurface(FPCGContext* Context, const UPCGSpatialData* InSurface, const UPCGSpatialData* InBoundingShape, const FSurfaceSamplerSettings& LoopData);
	void SampleSurface(FPCGContext* Context, const UPCGSpatialData* InSurface, const UPCGSpatialData* InBoundingShape, const FSurfaceSamplerSettings& LoopData, UPCGPointData* SampledData);
}

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif
