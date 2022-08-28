// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/GLTFOrbitCameraActor.h"

namespace
{
	// Uniquely identifies an angle by how many times it has crossed the 0-360 degree range.
	// Positive angles are indexed from 0 and negative angles from -1.
	int32 AngleCycleIndex(float Angle)
	{
		// TODO: is this similar to viewer camera?
		return (static_cast<int32>(Angle) / 360) + ((Angle < 0.0f) ? -1 : 0);
	}

	// Scales to convert from the export-friendly sensitivity-values stored in our properties
	// to values that we can use when processing axis-values (to get similar results as in the viewer).
	const float OrbitSensitivityScale = 16.667f;
	const float DistanceSensitivityScale = 0.1f;
}

AGLTFOrbitCameraActor::AGLTFOrbitCameraActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Focus(nullptr)
	, DistanceMin(100.0f)
	, DistanceMax(1000.0f)
	, PitchAngleMin(-90.0f)
	, PitchAngleMax(90.0f)
	, DollyDuration(0.2f)
	, OrbitInertia(0.1f)
	, OrbitSensitivity(0.3f)
	, DistanceSensitivity(0.5f)
	, FocusPosition(0.0f, 0.0f, 0.0f)
	, Distance(0.0f)
	, Pitch(0.0f)
	, Yaw(0.0f)
	, TargetDistance(0.0f)
	, TargetPitch(0.0f)
	, TargetYaw(0.0f)
	, DollyTime(0.0f)
	, DollyStartDistance(0.0f)
{
	PrimaryActorTick.bCanEverTick = true;
}

void AGLTFOrbitCameraActor::BeginPlay()
{
	Super::BeginPlay();

	if (this->Focus != nullptr && this->Focus != this)
	{
		const FBox BoundingBox = this->Focus->GetComponentsBoundingBox(true, true);
		FocusPosition = BoundingBox.IsValid ? BoundingBox.GetCenter() : this->Focus->GetActorLocation();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("The camera focus must not be null, and must not be the camera's own actor"));

		// TODO: use the scene center (similar to viewer camera) instead of using the zero-vector.
		// It may however prove difficult, since we would need to exclude sky-spheres, backdrops etc when calculating the center.
	}

	// Ensure that the camera is initially aimed at the focus-position
	SetActorRotation(GetLookAtRotation(FocusPosition));

	const FVector Position = GetActorLocation();
	const FRotator Rotation = GetActorRotation();

	// Calculate values based on the current location and orientation
	Distance = ClampDistance((FocusPosition - Position).Size());
	Pitch = ClampPitch(Rotation.Pitch);
	Yaw = ClampYaw(Rotation.Yaw);
	TargetDistance = Distance;
	TargetPitch = Pitch;
	TargetYaw = Yaw;

	if (InputComponent)
	{
		InputComponent->BindAxisKey(EKeys::MouseX, this, &AGLTFOrbitCameraActor::OnMouseX);
		InputComponent->BindAxisKey(EKeys::MouseY, this, &AGLTFOrbitCameraActor::OnMouseY);
		InputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &AGLTFOrbitCameraActor::OnMouseWheelAxis);
	}
}

void AGLTFOrbitCameraActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (DollyTime != 0.0f)
	{
		DollyTime = FMath::Max(DollyTime - DeltaSeconds, 0.0f);
		Distance = FMath::InterpEaseInOut(DollyStartDistance, TargetDistance, (DollyDuration - DollyTime) / DollyDuration, 1.2f);
	}

	const float Alpha = (OrbitInertia == 0.0f) ? 1.0f : FMath::Min(DeltaSeconds / OrbitInertia, 1.0f);
	Yaw = FMath::Lerp(Yaw, TargetYaw, Alpha);
	Pitch = FMath::Lerp(Pitch, TargetPitch, Alpha);

	const int32 YawCycleIndex = AngleCycleIndex(Yaw);
	const int32 TargetYawCycleIndex = AngleCycleIndex(TargetYaw);

	// Clamp the angles to a positive 360 degrees while they are within the same cycle.
	// It is important that this doesn't happen during a transition as it will otherwise be skewed.
	if (YawCycleIndex == TargetYawCycleIndex && YawCycleIndex != 0)
	{
		Yaw = ClampYaw(Yaw);
		TargetYaw = ClampYaw(TargetYaw);
	}

	const FTransform FocusTransform = FTransform(FocusPosition);
	const FTransform DollyTransform = FTransform(-FVector::ForwardVector * Distance);
	const FTransform RotationTransform = FTransform(FQuat::MakeFromEuler(FVector(0.0f, Pitch, Yaw)));
	const FTransform ResultTransform = DollyTransform * RotationTransform * FocusTransform;

	SetActorTransform(ResultTransform);
}

void AGLTFOrbitCameraActor::PreInitializeComponents()
{
	AutoReceiveInput = static_cast<EAutoReceiveInput::Type>(GetAutoActivatePlayerIndex() + 1);

	Super::PreInitializeComponents();
}

void AGLTFOrbitCameraActor::OnMouseX(float AxisValue)
{
	TargetYaw += AxisValue * OrbitSensitivity * OrbitSensitivityScale;
}

void AGLTFOrbitCameraActor::OnMouseY(float AxisValue)
{
	TargetPitch = ClampPitch(TargetPitch + AxisValue * OrbitSensitivity * OrbitSensitivityScale);
}

void AGLTFOrbitCameraActor::OnMouseWheelAxis(float AxisValue)
{
	if (!FMath::IsNearlyZero(AxisValue))
	{
		const float DeltaDistance = -AxisValue * (TargetDistance * DistanceSensitivity * DistanceSensitivityScale);

		DollyTime = DollyDuration;
		TargetDistance = ClampDistance(TargetDistance + DeltaDistance);
		DollyStartDistance = Distance;
	}
}

float AGLTFOrbitCameraActor::ClampDistance(float Value) const
{
	return FMath::Clamp(Value, DistanceMin, DistanceMax);
}

float AGLTFOrbitCameraActor::ClampPitch(float Value) const
{
	return FMath::Clamp(Value, PitchAngleMin, PitchAngleMax);
}

float AGLTFOrbitCameraActor::ClampYaw(float Value) const
{
	const int32 CycleIndex = AngleCycleIndex(Value);
	const float Offset = 360.0f * static_cast<float>(FMath::Abs(FMath::Min(CycleIndex, 0)));

	return FMath::Fmod(Value + Offset, 360.0f);
}

void AGLTFOrbitCameraActor::RemoveInertia()
{
	Yaw = TargetYaw;
	Pitch = TargetPitch;
	Distance = TargetDistance;
}

FRotator AGLTFOrbitCameraActor::GetLookAtRotation(const FVector TargetPosition) const
{
	const FVector EyePosition = GetActorLocation();

	return FRotationMatrix::MakeFromXZ(TargetPosition - EyePosition, FVector::UpVector).Rotator();
}
