// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "WaterRuntimeSettings.generated.h"

class UMaterialParameterCollection;

/**
 * Implements the runtime settings for the Water plugin.
 */
UCLASS(config = Engine, defaultconfig, meta=(DisplayName="Water"))
class WATER_API UWaterRuntimeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UWaterRuntimeSettings();

	virtual FName GetCategoryName() const;

	FName GetDefaultWaterCollisionProfileName() const { return DefaultWaterCollisionProfileName; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

	/** Collision channel to use for tracing and blocking water bodies */
	UPROPERTY(EditAnywhere, config, Category = Collision)
	TEnumAsByte<ECollisionChannel> CollisionChannelForWaterTraces;

	/** Material Parameter Collection for everything water-related */
	UPROPERTY(EditAnywhere, config, Category = Rendering)
	TSoftObjectPtr<UMaterialParameterCollection> MaterialParameterCollection;

	/** Size of the water body icon in world-space. */
	UPROPERTY(EditAnywhere, config, Category = Rendering)
	float WaterBodyIconWorldSize = 1000.0f;

	/** Offset in Z for the water body icon in world-space. */
	UPROPERTY(EditAnywhere, config, Category = Rendering)
	float WaterBodyIconWorldZOffset = 250.0f;

#if WITH_EDITORONLY_DATA
	// Delegate called whenever the curve data is updated
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnUpdateSettings, const UWaterRuntimeSettings* /*Settings*/, EPropertyChangeType::Type /*ChangeType*/);
	static FOnUpdateSettings OnSettingsChange;
#endif

private:
	/** Default collision profile name of water bodies */
	UPROPERTY(VisibleAnywhere, config, Category = Collision)
	FName DefaultWaterCollisionProfileName;
};
