// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "PCGElement.h"

#include "PCGSurfaceSampler.generated.h"

class FPCGSurfaceSamplerElement;
class UPCGPointData;
class UPCGSpatialData;

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGSurfaceSamplerSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPCGSurfaceSamplerSettings();

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ClampMin="0"))
	float PointsPerSquaredMeter = 0.1f;

	UPROPERTY()
	float PointRadius_DEPRECATED = 0.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FVector PointExtents = FVector(100.0f);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ClampMin="0"))
	float Looseness = 1.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Points")
	bool bApplyDensityToPoints = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Points", meta=(ClampMin="0", ClampMax="1"))
	float PointSteepness = 0.5f;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient, BlueprintReadWrite, EditAnywhere, Category = "Debug")
	bool bKeepZeroDensityPoints = false;
#endif

	//~Begin UObject interface
	virtual void PostLoad() override;
	//~End UObject interface

#if WITH_EDITOR
	//~Begin UPCGSettings interface
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("SurfaceSamplerNode")); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Sampler; }
#endif

	virtual TArray<FPCGPinProperties> OutputPinProperties() const override { return Super::DefaultPointOutputPinProperties(); }

protected:
	virtual FPCGElementPtr CreateElement() const override;
	// ~End UPCGSettings interface
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
		void ComputeCellIndices(int32 Index, int32& CellX, int32& CellY) const;

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
	};

	UPCGPointData* SampleSurface(FPCGContext* Context, const UPCGSpatialData* SpatialInput, const FSurfaceSamplerSettings& LoopData);
	void SampleSurface(FPCGContext* Context, const UPCGSpatialData* SpatialInput, const FSurfaceSamplerSettings& LoopData, UPCGPointData* SampledData);
}