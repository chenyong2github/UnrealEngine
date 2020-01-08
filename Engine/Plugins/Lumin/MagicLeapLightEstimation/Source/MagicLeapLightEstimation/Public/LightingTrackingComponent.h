// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "Engine/Engine.h"
#include "LightingTrackingComponent.generated.h"

/**
The LightingTrackingComponent wraps the Magic Leap lighting tracking API.
This api provides lumosity data from the camera that can be used to shade objects in a more realistic
manner (via the post processor).
*/
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, EditInlineNew, meta = (BlueprintSpawnableComponent))
class MAGICLEAPLIGHTESTIMATION_API UMagicLeapLightingTrackingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMagicLeapLightingTrackingComponent();

	/** Intializes the lighting tracking api. If a post processing component cannot be found in the scene, one will be created. */
	void BeginPlay() override;
	/** Cleans up the lighting tracking api. */
	void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** Polls for data from the camera array and processes it based on the active modes (UseGlobalAmbience, UseColorTemp, ...). */
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	/** Set to true if you want the global ambience value from the cameras to be used in post processing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LightingTracking|MagicLeap")
	bool UseGlobalAmbience;
	/** Set to true if you want the color temperature value from the cameras to be used in post processing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LightingTracking|MagicLeap")
	bool UseColorTemp;
	/** Set to true if you want the ambient cube map to be dynamically updated from the cameras' data. */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LightingTracking|MagicLeap")
	//bool UseDynamicAmbientCubeMap;

private:
	class FMagicLeapLightingTrackingImpl* Impl;
};
