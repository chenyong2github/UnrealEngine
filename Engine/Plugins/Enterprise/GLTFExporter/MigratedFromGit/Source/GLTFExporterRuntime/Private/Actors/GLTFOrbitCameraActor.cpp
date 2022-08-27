// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/GLTFOrbitCameraActor.h"

namespace
{
	const float DollyDuration = 0.2f;

	float EaseInQuad(float T, float B, float C, float D)
	{
		return C * (T /= D) * T + B;
	}

	// Positive angles are indexed from 0 and negative angles from -1
	int32 AngleCycleIndex(float Angle)
	{
		return (static_cast<int32>(Angle) / 360) + ((Angle < 0.0f) ? -1 : 0);
	}
} // Anonymous namespace

DEFINE_LOG_CATEGORY_STATIC(LogEditorGLTFOrbitCamera, Log, All);

AGLTFOrbitCameraActor::AGLTFOrbitCameraActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GLTF Orbit Camera Actor"),
	Focus(nullptr),
	DistanceMin(100.0f),
	DistanceMax(1000.0f),
	PitchAngleMin(-90.0f),
	PitchAngleMax(90.0f),
	OrbitInertia(0.07f),
	OrbitSensitivity(30.0f),
	DistanceSensitivity(50.0f),
	Distance(0.0f),
	Pitch(0.0f),
	Yaw(0.0f),
	TargetDistance(0.0f),
	TargetPitch(0.0f),
	TargetYaw(0.0f),
	DollyTime(0.0f),
	DollyStartDistance(0.0f)
{
	PrimaryActorTick.bCanEverTick = true;
}

#if WITH_EDITOR
void AGLTFOrbitCameraActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FProperty* PropertyThatChanged = PropertyChangedEvent.Property;

	if (PropertyThatChanged != nullptr)
	{
		const FString PropertyName = PropertyThatChanged->GetName();

		if (PropertyName == TEXT("Focus"))
		{
			if (Focus == this)
			{
				UE_LOG(LogEditorGLTFOrbitCamera, Warning, TEXT("The camera cannot focus itself."));
			}
		}
	}
}
#endif // WITH_EDITOR

void AGLTFOrbitCameraActor::BeginPlay()
{
	Super::BeginPlay();

	if (this->Focus == nullptr || this->Focus == this)
	{
		UE_LOG(LogEditorGLTFOrbitCamera, Warning, TEXT("The camera focus must not be null or the camera itself."));
	}
	
	Distance = ClampDistance(Distance);
	Pitch = ClampPitch(Pitch);
	Yaw = ClampYaw(Yaw);
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
		Distance = EaseInQuad(DollyDuration - DollyTime, DollyStartDistance, TargetDistance - DollyStartDistance, DollyDuration);
		DollyTime = FMath::Max(DollyTime - DeltaSeconds, 0.0f);
	}

	const float Alpha = (OrbitInertia == 0.0f) ? 1.0f : FMath::Min(DeltaSeconds / OrbitInertia, 1.0f);
	Yaw = FMath::Lerp(Yaw, TargetYaw, Alpha);
	Pitch = FMath::Lerp(Pitch, TargetPitch, Alpha);

	const int32 YawCycleIndex = AngleCycleIndex(Yaw);
	const int32 TargetYawCycleIndex = AngleCycleIndex(TargetYaw);

	// Clamp the angles to a positive 360 degrees while they are within the same cycle
	if (YawCycleIndex == TargetYawCycleIndex && YawCycleIndex != 0)
	{
		Yaw = ClampYaw(Yaw);
		TargetYaw = ClampYaw(TargetYaw);
	}

	const FVector FocusPosition = (this->Focus != nullptr && this->Focus != this) ? this->Focus->GetActorLocation() : FVector::ZeroVector;
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
	if (AxisValue == 0.0f)
	{
		return;
	}

	TargetYaw += AxisValue * OrbitSensitivity;
}

void AGLTFOrbitCameraActor::OnMouseY(float AxisValue)
{
	if (AxisValue == 0.0f)
	{
		return;
	}

	TargetPitch = ClampPitch(TargetPitch + AxisValue * OrbitSensitivity);
}

void AGLTFOrbitCameraActor::OnMouseWheelAxis(float AxisValue)
{
	if (AxisValue == 0.0f)
	{
		return;
	}

	DollyTime = DollyDuration;
	TargetDistance = ClampDistance(TargetDistance + -AxisValue * DistanceSensitivity);
	DollyStartDistance = Distance;
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
