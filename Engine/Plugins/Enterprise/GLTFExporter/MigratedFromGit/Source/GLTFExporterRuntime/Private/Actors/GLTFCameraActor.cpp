// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/GLTFCameraActor.h"
#include "Components/InputComponent.h"

namespace
{
	// Scales to convert from the export-friendly sensitivity-values stored in our properties
	// to values that we can use when processing axis-values (to get similar results as in the viewer).
	const float RotationSensitivityScale = 16.667f;
	const float DollySensitivityScale = 0.1f;
}

AGLTFCameraActor::AGLTFCameraActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Mode(EGLTFCameraMode::FirstPerson)
	, Focus(nullptr)
	, PitchAngleMin(-90.0f)
	, PitchAngleMax(90.0f)
	, DistanceMin(100.0f)
	, DistanceMax(1000.0f)
	, DollyDuration(0.2f)
	, DollySensitivity(0.5f)
	, RotationInertia(0.1f)
	, RotationSensitivity(0.3f)
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

#if WITH_EDITOR
void AGLTFCameraActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FProperty* PropertyThatChanged = PropertyChangedEvent.Property;

	if (PropertyThatChanged)
	{
		const FName PropertyName = PropertyThatChanged->GetFName();

		if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, Focus))
		{
			if (Focus == this)
			{
				Focus = nullptr;
				// TODO: don't use LogTemp
				UE_LOG(LogTemp, Warning, TEXT("The camera focus must not be the camera's own actor"));
			}
		}
	}
}
#endif // WITH_EDITOR

void AGLTFCameraActor::BeginPlay()
{
	Super::BeginPlay();

	if (Mode == EGLTFCameraMode::FirstPerson)
	{
		// TODO: implement
	}
	else if (Mode == EGLTFCameraMode::ThirdPerson)
	{
		const FVector FocusPosition = GetFocusPosition();

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
	}
	else
	{
		checkNoEntry();
	}

	if (InputComponent)
	{
		InputComponent->BindAxisKey(EKeys::MouseX, this, &AGLTFCameraActor::OnMouseX);
		InputComponent->BindAxisKey(EKeys::MouseY, this, &AGLTFCameraActor::OnMouseY);
		InputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &AGLTFCameraActor::OnMouseWheelAxis);
	}
}

void AGLTFCameraActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Mode == EGLTFCameraMode::FirstPerson)
	{
		// TODO: implement
	}
	else if (Mode == EGLTFCameraMode::ThirdPerson)
	{
		if (DollyTime != 0.0f)
		{
			DollyTime = FMath::Max(DollyTime - DeltaSeconds, 0.0f);
			Distance = FMath::InterpEaseInOut(DollyStartDistance, TargetDistance, (DollyDuration - DollyTime) / DollyDuration, 1.2f);
		}

		const float Alpha = (RotationInertia == 0.0f) ? 1.0f : FMath::Min(DeltaSeconds / RotationInertia, 1.0f);
		Yaw = FMath::Lerp(Yaw, TargetYaw, Alpha);
		Pitch = FMath::Lerp(Pitch, TargetPitch, Alpha);

		const FTransform FocusTransform = FTransform(GetFocusPosition());
		const FTransform DollyTransform = FTransform(-FVector::ForwardVector * Distance);
		const FTransform RotationTransform = FTransform(FQuat::MakeFromEuler(FVector(0.0f, Pitch, Yaw)));
		const FTransform ResultTransform = DollyTransform * RotationTransform * FocusTransform;

		SetActorTransform(ResultTransform);
	}
}

void AGLTFCameraActor::PreInitializeComponents()
{
	AutoReceiveInput = static_cast<EAutoReceiveInput::Type>(GetAutoActivatePlayerIndex() + 1);

	Super::PreInitializeComponents();
}

void AGLTFCameraActor::PostActorCreated()
{
	SetAutoActivateForPlayer(EAutoReceiveInput::Player0);
}

void AGLTFCameraActor::OnMouseX(float AxisValue)
{
	TargetYaw += AxisValue * RotationSensitivity * RotationSensitivityScale;
}

void AGLTFCameraActor::OnMouseY(float AxisValue)
{
	TargetPitch = ClampPitch(TargetPitch + AxisValue * RotationSensitivity * RotationSensitivityScale);
}

void AGLTFCameraActor::OnMouseWheelAxis(float AxisValue)
{
	if (Mode == EGLTFCameraMode::ThirdPerson)
	{
		if (!FMath::IsNearlyZero(AxisValue))
		{
			const float DeltaDistance = -AxisValue * (TargetDistance * DollySensitivity * DollySensitivityScale);

			DollyTime = DollyDuration;
			TargetDistance = ClampDistance(TargetDistance + DeltaDistance);
			DollyStartDistance = Distance;
		}
	}
}

float AGLTFCameraActor::ClampDistance(float Value) const
{
	return FMath::Clamp(Value, DistanceMin, DistanceMax);
}

float AGLTFCameraActor::ClampPitch(float Value) const
{
	return FMath::Clamp(Value, PitchAngleMin, PitchAngleMax);
}

float AGLTFCameraActor::ClampYaw(float Value) const
{
	// TODO: implement

	return Value;
}

void AGLTFCameraActor::RemoveInertia()
{
	Yaw = TargetYaw;
	Pitch = TargetPitch;
	Distance = TargetDistance;
}

FRotator AGLTFCameraActor::GetLookAtRotation(const FVector TargetPosition) const
{
	const FVector EyePosition = GetActorLocation();

	return FRotationMatrix::MakeFromXZ(TargetPosition - EyePosition, FVector::UpVector).Rotator();
}

FVector AGLTFCameraActor::GetFocusPosition() const
{
	return this->Focus != nullptr ? this->Focus->GetActorLocation() : FVector::ZeroVector;
}

bool AGLTFCameraActor::SetAutoActivateForPlayer(const EAutoReceiveInput::Type Player)
{
	// TODO: remove hack by adding proper API access to ACameraActor (or by other means)
	FProperty* Property = GetClass()->FindPropertyByName(TEXT("AutoActivateForPlayer"));
	if (Property == nullptr)
	{
		return false;
	}

	TEnumAsByte<EAutoReceiveInput::Type>* ValuePtr = Property->ContainerPtrToValuePtr<TEnumAsByte<EAutoReceiveInput::Type>>(this);
	if (ValuePtr == nullptr)
	{
		return false;
	}

	*ValuePtr = Player;
	return true;
}
