// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGSettings.h"
#include "PCGElement.h"

#include "PCGSplineSampler.generated.h"

class UPCGPolyLineData;
class UPCGSpatialData;
class UPCGPointData;

UENUM()
enum class EPCGSplineSamplingMode : uint8
{
	Subdivision = 0,
	Distance
};

UENUM()
enum class EPCGSplineSamplingDimension : uint8
{
	OnSpline = 0,
	OnHorizontal,
	OnVertical,
	OnVolume
};

UENUM()
enum class EPCGSplineSamplingFill : uint8
{
	Fill = 0,
	EdgesOnly = 1
};

USTRUCT(BlueprintType)
struct PCG_API FPCGSplineSamplerParams
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGSplineSamplingMode Mode = EPCGSplineSamplingMode::Subdivision;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGSplineSamplingDimension Dimension = EPCGSplineSamplingDimension::OnSpline;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Dimension!=EPCGSplineSamplingDimension::OnSpline"))
	EPCGSplineSamplingFill Fill = EPCGSplineSamplingFill::Fill;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ClampMin = "0", EditCondition = "Mode==EPCGSplineSamplingMode::Subdivision"))
	int32 SubdivisionsPerSegment = 1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ClampMin = "0.1", EditCondition = "Mode==EPCGSplineSamplingMode::Distance"))
	float DistanceIncrement = 100.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ClampMin = "0", EditCondition = "Dimension==EPCGSplineSamplingDimension::OnHorizontal||Dimension==EPCGSplineSamplingDimension::OnVolume"))
	int32 NumPlanarSubdivisions = 8;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ClampMin = "0", EditCondition = "Dimension==EPCGSplineSamplingDimension::OnVertical||Dimension==EPCGSplineSamplingDimension::OnVolume"))
	int32 NumHeightSubdivisions = 8;
};

namespace PCGSplineSampler
{
	void SampleLineData(const UPCGPolyLineData* LineData, const UPCGSpatialData* SpatialData, const FPCGSplineSamplerParams& Params, UPCGPointData* OutPointData);
	const UPCGPolyLineData* GetPolyLineData(const UPCGSpatialData* InSpatialData);
}

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGSplineSamplerSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	// ~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("SplineSamplerNode")); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Sampler; }
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override { return Super::DefaultPointOutputPinProperties(); }

protected:
	virtual FPCGElementPtr CreateElement() const override;
	// ~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ShowOnlyInnerProperties))
	FPCGSplineSamplerParams Params;
};

class FPCGSplineSamplerElement : public FSimplePCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};