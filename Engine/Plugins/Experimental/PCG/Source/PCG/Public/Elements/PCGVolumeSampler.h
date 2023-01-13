// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Box.h"
#include "PCGSettings.h"

#include "PCGPin.h"
#include "PCGVolumeSampler.generated.h"

class UPCGPointData;
class UPCGSpatialData;

namespace PCGVolumeSampler
{
	struct FVolumeSamplerSettings
	{
		FVector VoxelSize;
	};

	UPCGPointData* SampleVolume(FPCGContext* Context, const UPCGSpatialData* SpatialData, const FVolumeSamplerSettings& SamplerSettings);
	void SampleVolume(FPCGContext* Context, const UPCGSpatialData* SpatialData, const FVolumeSamplerSettings& SamplerSettings, UPCGPointData* OutputData, const FBox& Bounds);
}

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGVolumeSamplerSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	FVector VoxelSize = FVector(100.0, 100.0, 100.0);

	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("VolumeSampler")); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Sampler; }
#endif

	virtual TArray<FPCGPinProperties> OutputPinProperties() const override { return Super::DefaultPointOutputPinProperties(); }
	
protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface
};

class FPCGVolumeSamplerElement : public FSimplePCGElement
{
protected:
	bool ExecuteInternal(FPCGContext* Context) const override;
};
