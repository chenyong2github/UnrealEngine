// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Camera/CameraActor.h"
#include "GLTFCameraActor.generated.h"

/**
 * GLTF-compatible camera that will carry over settings and simulate the behavior in the resulting viewer.
 * Focuses one actor in the scene and orbits it through mouse control.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "GLTF Camera")
class GLTFEXPORTERRUNTIME_API AGLTFCameraActor : public ACameraActor
{
	GENERATED_BODY()
	//~ Begin UObject Interface
public:
	AGLTFCameraActor(const FObjectInitializer& ObjectInitializer);
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface

	//~ Begin AActor Interface
protected:
	virtual void BeginPlay() override;
public:
	virtual void Tick(float DeltaSeconds) override;
	virtual void PreInitializeComponents() override;
	virtual void PostActorCreated() override;
	//~ End AActor Interface

private:
	UFUNCTION()
	void OnMouseX(float AxisValue);

	UFUNCTION()
	void OnMouseY(float AxisValue);

	UFUNCTION()
	void OnMouseWheelAxis(float AxisValue);

	float ClampDistance(float Value) const;

	float ClampPitch(float Value) const;

	float ClampYaw(float Value) const;

	void RemoveInertia();

	FRotator GetLookAtRotation(const FVector TargetPosition) const;

	FVector GetFocusPosition() const;

	bool SetAutoActivateForPlayer(const EAutoReceiveInput::Type Player);

public:
	/* Actor which the camera will focus on and subsequently orbit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Orbit Camera Actor")
	AActor* Focus;

	/* Minimum pitch angle (in degrees) for the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Camera Actor")
	float PitchAngleMin;

	/* Maximum pitch angle (in degrees) for the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Camera Actor")
	float PitchAngleMax;

	/* Closest distance the camera can approach the focused actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Camera Actor")
	float DistanceMin;

	/* Farthest distance the camera can recede from the focused actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Camera Actor")
	float DistanceMax;

	/* Duration (in seconds) that it takes the camera to complete a change in distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GLTF Camera Actor")
	float DollyDuration;

	/* Size of the dolly movement relative to user input. The higher the value, the faster it moves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GLTF Camera Actor")
	float DollySensitivity;

	/* Deceleration that occurs after rotational movement. The higher the value, the longer it takes to settle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GLTF Camera Actor")
	float RotationInertia;

	/* Size of the rotational movement relative to user input. The higher the value, the faster it moves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GLTF Camera Actor")
	float RotationSensitivity;

private:
	float Distance;
	float Pitch;
	float Yaw;
	float TargetDistance;
	float TargetPitch;
	float TargetYaw;
	float DollyTime;
	float DollyStartDistance;
};
