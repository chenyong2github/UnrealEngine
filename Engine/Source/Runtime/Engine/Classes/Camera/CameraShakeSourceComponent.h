// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "UObject/ObjectMacros.h"
#include "CameraShakeSourceComponent.generated.h"

class UCameraShake;

UENUM(BlueprintType)
enum class ECameraShakeAttenuation : uint8
{
	Linear,
	Quadratic
};

UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent))
class ENGINE_API UCameraShakeSourceComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UCameraShakeSourceComponent(const FObjectInitializer& ObjectInitializer);

	/** The attenuation profile for how camera shakes' intensity falls off with distance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CameraShake)
	ECameraShakeAttenuation Attenuation;

	/** Under this distance from the source, the camera shakes are at full intensity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CameraShake)
	float InnerAttenuationRadius;

	/** Outside of this distance from the source, the camera shakes don't apply at all */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CameraShake)
	float OuterAttenuationRadius;

	/** Starts a new camera shake originating from this source, and apply it on all player controllers */
	UFUNCTION(BlueprintCallable, Category = CameraShake)
	void PlayCameraShake(TSubclassOf<UCameraShake> CameraShake);

	/** Stops all currently active camera shakes that are originating from this source from all player controllers */
	UFUNCTION(BlueprintCallable, Category = CameraShake)
	void StopAllCameraShakes(bool bImmediately = true);

	/** Computes an attenuation factor from this source */
	UFUNCTION(BlueprintCallable, Category = CameraShake)
	float GetAttenuationFactor(const FVector& Location) const;
};
