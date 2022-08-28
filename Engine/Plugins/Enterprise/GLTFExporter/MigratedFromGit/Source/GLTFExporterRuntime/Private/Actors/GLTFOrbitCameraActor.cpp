// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/GLTFOrbitCameraActor.h"
//#include "Kismet/GameplayStatics.h"

namespace
{
	const float DollyDuration = 0.2f;

	float EaseInQuad(float T, float B, float C, float D)
	{
		return C * (T /= D) * T + B;
	}
} // Anonymous namespace

#define DEBUGGLTFORBITCAMERA 0

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

	if (PropertyThatChanged)
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
	//Distance = FMath::Lerp(Distance, TargetDistance, Alpha);

	const FVector FocusPosition = (this->Focus != nullptr) ? this->Focus->GetActorLocation() : FVector::ZeroVector;
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

	const float Delta = (TargetYaw + AxisValue * OrbitSensitivity) - Yaw;
	const float Remainder = FMath::Fmod(Delta, 360.0f);

	if (Remainder > 180.0f)
	{
		TargetYaw = Yaw - (360.0f - Remainder);
}
	else if (Remainder < -180.0f)
	{
		TargetYaw = Yaw + (360.0f + Remainder);
	}
	else
	{
		TargetYaw = Yaw + Remainder;
	}

#if DEBUGGLTFORBITCAMERA
	UE_LOG(LogEditorGLTFOrbitCamera, Warning, TEXT("AGLTFOrbitCameraActor::OnMouseX(), %f"), AxisValue);
#endif
}

void AGLTFOrbitCameraActor::OnMouseY(float AxisValue)
{
	if (AxisValue == 0.0f)
	{
		return;
	}

	TargetPitch = ClampPitch(TargetPitch + AxisValue * OrbitSensitivity);

#if DEBUGGLTFORBITCAMERA
	UE_LOG(LogEditorGLTFOrbitCamera, Warning, TEXT("AGLTFOrbitCameraActor::OnMouseY(), %f"), AxisValue);
#endif
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

#if DEBUGGLTFORBITCAMERA
	UE_LOG(LogEditorGLTFOrbitCamera, Warning, TEXT("AGLTFOrbitCameraActor::OnMouseWheelAxis(), %f"), AxisValue);
#endif
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
	return FMath::Fmod(Value + 360.0f, 360.0f);
}

void AGLTFOrbitCameraActor::RemoveInertia()
{
	Yaw = TargetYaw;
	Pitch = TargetPitch;
	Distance = TargetDistance;
}
