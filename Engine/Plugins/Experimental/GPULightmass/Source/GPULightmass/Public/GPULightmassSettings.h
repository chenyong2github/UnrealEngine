// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Info.h"
#include "Subsystems/WorldSubsystem.h"
#include "GPULightmassSettings.generated.h"

UENUM()
enum class EGPULightmassMode : uint8
{
	FullBake,
	BakeWhatYouSee,
	// BakeSelected  UMETA(DisplayName = "Bake Selected (Not Implemented)")
};

UENUM()
enum class EGPULightmassDenoisingOptions : uint8
{
	None,
	OnCompletion,
	DuringInteractivePreview
};

UCLASS(BlueprintType)
class GPULIGHTMASS_API UGPULightmassSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = General)
	bool bShowProgressBars = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = General)
	EGPULightmassMode Mode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = GlobalIllumination, DisplayName = "GI Samples", meta = (ClampMin = "32", ClampMax = "65536", UIMax = "8192"))
	int32 GISamples = 512;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = GlobalIllumination, meta = (ClampMin = "32", ClampMax = "65536", UIMax = "8192"))
	int32 StationaryLightShadowSamples = 512;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = GlobalIllumination)
	bool bUseIrradianceCaching = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = GlobalIllumination, meta = (EditCondition = "bUseIrradianceCaching"))
	bool bUseFirstBounceRayGuiding = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Denoising, DisplayName = "Denoise")
	EGPULightmassDenoisingOptions DenoisingOptions = EGPULightmassDenoisingOptions::OnCompletion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IrradianceCaching, DisplayName = "Quality", meta = (EditCondition = "bUseIrradianceCaching", ClampMin = "4", ClampMax = "65536", UIMax = "8192"))
	int32 IrradianceCacheQuality = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = IrradianceCaching, DisplayName = "Size", meta = (EditCondition = "bUseIrradianceCaching", ClampMin = "4", ClampMax = "1024"))
	int32 IrradianceCacheSpacing = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = IrradianceCaching, DisplayName = "Corner Rejection", meta = (EditCondition = "bUseIrradianceCaching", ClampMin = "0.0", ClampMax = "8.0"))
	float IrradianceCacheCornerRejection = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = IrradianceCaching, DisplayName = "Debug: Visualize", meta = (EditCondition = "bUseIrradianceCaching"))
	bool bVisualizeIrradianceCache = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = FirstBounceRayGuiding, DisplayName = "Trial Samples", meta = (EditCondition = "bUseFirstBounceRayGuiding"))
	int32 FirstBounceRayGuidingTrialSamples = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = System, DisplayName = "Slow Mode Speed", meta = (ClampMin = "1", ClampMax = "64"))
	int32 TilePassesInSlowMode = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = System, DisplayName = "Full Speed", meta = (ClampMin = "1", ClampMax = "64"))
	int32 TilePassesInFullSpeedMode = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = System, meta = (ClampMin = "16", ClampMax = "128"))
	int32 LightmapTilePoolSize = 40;

public:
	void GatherSettingsFromCVars();
	void ApplyImmediateSettingsToRunningInstances();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif // WITH_EDITOR
};

UCLASS()
class GPULIGHTMASS_API AGPULightmassSettingsActor : public AInfo
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	UGPULightmassSettings* Settings;
};

UCLASS()
class GPULIGHTMASS_API UGPULightmassSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = GPULightmass)
	UGPULightmassSettings* GetSettings();

	UFUNCTION(BlueprintCallable, Category = GPULightmass)
	void Launch();

	UFUNCTION(BlueprintCallable, Category = GPULightmass)
	void Stop();

	UFUNCTION(BlueprintCallable, Category = GPULightmass)
	bool IsRunning();

private:
	AGPULightmassSettingsActor* GetSettingsActor();
};
