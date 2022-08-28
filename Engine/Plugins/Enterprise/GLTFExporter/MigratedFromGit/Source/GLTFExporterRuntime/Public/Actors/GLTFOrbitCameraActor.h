// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Camera/CameraActor.h"
#include "GLTFOrbitCameraActor.generated.h"

/**
 * GLTF-compatible camera that will carry over settings and simulate the behavior in the resulting viewer.
 * Focuses one actor in the scene and orbits it through mouse control.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "GLTF Orbit Camera Actor")
class GLTFEXPORTERRUNTIME_API AGLTFOrbitCameraActor : public ACameraActor
{
	GENERATED_BODY()
	//~ Begin UObject Interface
public:
	AGLTFOrbitCameraActor(const FObjectInitializer& ObjectInitializer);
	//~ End UObject Interface

	//~ Begin AActor Interface
protected:
	virtual void BeginPlay() override;
public:
	virtual void Tick(float DeltaSeconds) override;
	virtual void PreInitializeComponents() override;
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

public:
	/* Actor which the camera will focus on and subsequently orbit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Orbit Camera Actor")
	AActor* Focus;

	/* Closest distance the camera can approach the focused actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Orbit Camera Actor")
	float DistanceMin;

	/* Farthest distance the camera can recede from the focused actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Orbit Camera Actor")
	float DistanceMax;

	/* Minimum angle (in degrees) that the camera can pitch relative to the focused actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Orbit Camera Actor")
	float PitchAngleMin;

	/* Maximum angle (in degrees) that the camera can pitch relative to the focused actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Orbit Camera Actor")
	float PitchAngleMax;

	/* Duration (in seconds) that it takes the camera to complete a change in distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GLTF Orbit Camera Actor")
	float DollyDuration;

	/* Deceleration that occurs after orbital movement. The higher the value, the longer it takes to settle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GLTF Orbit Camera Actor")
	float OrbitInertia;

	/* Size of the orbital movement relative to user input. The higher the value, the faster it moves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GLTF Orbit Camera Actor")
	float OrbitSensitivity;

	/* Size of the dolly movement relative to user input. The higher the value, the faster it moves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GLTF Orbit Camera Actor")
	float DistanceSensitivity;

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
