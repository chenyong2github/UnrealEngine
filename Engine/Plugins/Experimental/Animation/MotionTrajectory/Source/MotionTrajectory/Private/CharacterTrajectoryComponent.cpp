// Copyright Epic Games, Inc. All Rights Reserved.

#include "CharacterTrajectoryComponent.h"

#include "CharacterMovementTrajectoryLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "MotionTrajectory.h"

#if ENABLE_ANIM_DEBUG
TAutoConsoleVariable<int32> CVarCharacterTrajectoryDebug(TEXT("a.CharacterTrajectory.Debug"), 0, TEXT("Turn on debug drawing for Character trajectory"));
#endif // ENABLE_ANIM_DEBUG

UCharacterTrajectoryComponent::UCharacterTrajectoryComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	bWantsInitializeComponent = true;
}

void UCharacterTrajectoryComponent::InitializeComponent()
{
	Super::InitializeComponent();

	if (bAutoUpdateTrajectory)
	{
		if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			Character->OnCharacterMovementUpdated.AddDynamic(this, &UCharacterTrajectoryComponent::OnMovementUpdated);
		}
		else
		{
			UE_LOG(LogMotionTrajectory, Error, TEXT("UCharacterTrajectoryComponent requires its owner to be ACharacter"));
		}
	}
}

void UCharacterTrajectoryComponent::UninitializeComponent()
{
	if (bAutoUpdateTrajectory)
	{
		if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			Character->OnCharacterMovementUpdated.RemoveDynamic(this, &UCharacterTrajectoryComponent::OnMovementUpdated);
		}
		else
		{
			UE_LOG(LogMotionTrajectory, Error, TEXT("UCharacterTrajectoryComponent requires its owner to be ACharacter"));
		}
	}

	Super::UninitializeComponent();
}

void UCharacterTrajectoryComponent::BeginPlay()
{
	Super::BeginPlay();

	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!ensureMsgf(Character, TEXT("UCharacterTrajectoryComponent requires valid ACharacter owner.")))
	{
		return;
	}

	SkelMeshComponent = Character->GetMesh();
	if (!ensureMsgf(SkelMeshComponent, TEXT("UCharacterTrajectoryComponent must be run on an ACharacter with a valid USkeletalMeshComponent.")))
	{
		return;
	}

	CharacterMovementComponent = Character->GetCharacterMovement();
	if (!ensureMsgf(CharacterMovementComponent, TEXT("UCharacterTrajectoryComponent must be run on an ACharacter with a valid UCharacterMovementComponent.")))
	{
		return;
	}

	SkelMeshComponentTransformWS = SkelMeshComponent->GetComponentTransform();

	// Default forward in the engine is the X axis, but data often diverges from this (e.g. it's common for skeletal meshes to be Y forward).
	// We determine the forward direction in the space of the skeletal mesh component based on the offset from the actor.
	ForwardFacingCS = SkelMeshComponent->GetRelativeRotation().Quaternion().Inverse();

	// The UI clamps these to be non-zero.
	check(HistorySamplesPerSecond);
	check(PredictionSamplesPerSecond);

	NumHistorySamples = FMath::CeilToInt32(HistoryLengthSeconds * HistorySamplesPerSecond);
	SecondsPerHistorySample = 1.f / HistorySamplesPerSecond;

	const int32 NumPredictionSamples = FMath::CeilToInt32(PredictionLengthSeconds * PredictionSamplesPerSecond);
	SecondsPerPredictionSample = 1.f / PredictionSamplesPerSecond;

	FPoseSearchQueryTrajectorySample DefaultSample;
	DefaultSample.Facing = ForwardFacingCS;
	DefaultSample.Position = FVector::ZeroVector;
	DefaultSample.AccumulatedSeconds = 0.f;

	// History + current sample + prediction
	Trajectory.Samples.Init(DefaultSample, NumHistorySamples + 1 + NumPredictionSamples);

	// initializing history samples AccumulatedSeconds
	for (int32 i = 0; i < NumHistorySamples; ++i)
	{
		Trajectory.Samples[i].AccumulatedSeconds = SecondsPerHistorySample * (i - NumHistorySamples);
	}

	// initializing history samples AccumulatedSeconds
	for (int32 i = NumHistorySamples + 1; i < Trajectory.Samples.Num(); ++i)
	{
		Trajectory.Samples[i].AccumulatedSeconds = SecondsPerPredictionSample * (i - NumHistorySamples);
	}
}

void UCharacterTrajectoryComponent::UpdateTrajectory(float DeltaSeconds)
{
	if (!ensure(CharacterMovementComponent != nullptr && SkelMeshComponent != nullptr))
	{
		return;
	}

	if (!ensure(DeltaSeconds > 0.f))
	{
		return;
	}

	const FTransform PreviousSkelMeshComponentTransformWS = SkelMeshComponentTransformWS;
	SkelMeshComponentTransformWS = SkelMeshComponent->GetComponentTransform();

	const FTransform SkelMeshTransformDelta = SkelMeshComponentTransformWS.GetRelativeTransform(PreviousSkelMeshComponentTransformWS);
	UpdateHistory(DeltaSeconds, SkelMeshTransformDelta);

	const FVector VelocityCS = SkelMeshComponentTransformWS.InverseTransformVectorNoScale(CharacterMovementComponent->Velocity);
	const FVector AccelerationCS = SkelMeshComponentTransformWS.InverseTransformVectorNoScale(CharacterMovementComponent->GetCurrentAcceleration());
	const FRotator ControllerRotationRate = CalculateControllerRotationRate(DeltaSeconds, CharacterMovementComponent->ShouldRemainVertical());
	UpdatePrediction(VelocityCS, AccelerationCS, ControllerRotationRate);

#if ENABLE_ANIM_DEBUG
	if (CVarCharacterTrajectoryDebug.GetValueOnAnyThread())
	{
		Trajectory.DebugDrawTrajectory(GetWorld(), SkelMeshComponentTransformWS);
	}
#endif // ENABLE_ANIM_DEBUG
}

void UCharacterTrajectoryComponent::OnMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
	UpdateTrajectory(DeltaSeconds);
}

void UpdateHistorySample(FPoseSearchQueryTrajectorySample& Sample, float DeltaSeconds, const FTransform& DeltaTransformCS)
{
	Sample.Facing = DeltaTransformCS.InverseTransformRotation(Sample.Facing);
	Sample.Position = DeltaTransformCS.InverseTransformPosition(Sample.Position);
	Sample.AccumulatedSeconds -= DeltaSeconds;
}

// This function moves each history sample by the inverse of the character's current motion (i.e. if the character is moving forward, the history
// samples move backward). It also shifts the range of history samples whenever a new history sample should be recorded.
// This allows us to keep a single sample array in component space that can be read directly by the Motion Matching node, rather than storing world
// transforms in a separate list and converting them to component space each update. 
// This also allows us to create "faked" trajectories that match animation data rather than the simulation (e.g. if our animation data only has coverage
// for one speed, we can adjust the history by a single speed to produce trajectories that best match the data).
void UCharacterTrajectoryComponent::UpdateHistory(float DeltaSeconds, const FTransform& DeltaTransformCS)
{
	check(NumHistorySamples <= Trajectory.Samples.Num());

	// Shift history Samples when it's time to record a new one.
	if (NumHistorySamples > 0 && FMath::Abs(Trajectory.Samples[NumHistorySamples - 1].AccumulatedSeconds) >= SecondsPerHistorySample)
	{
		for (int32 Index = 0; Index < NumHistorySamples; ++Index)
		{
			Trajectory.Samples[Index] = Trajectory.Samples[Index + 1];

			UpdateHistorySample(Trajectory.Samples[Index], DeltaSeconds, DeltaTransformCS);
		}
	}
	else
	{
		for (int32 Index = 0; Index < NumHistorySamples; ++Index)
		{
			UpdateHistorySample(Trajectory.Samples[Index], DeltaSeconds, DeltaTransformCS);
		}
	}
}

void UCharacterTrajectoryComponent::UpdatePrediction(const FVector& VelocityCS, const FVector& AccelerationCS, const FRotator& ControllerRotationRate)
{
	check(CharacterMovementComponent);

	FVector CurrentPositionCS = FVector::ZeroVector;
	FVector CurrentVelocityCS = VelocityCS;
	FVector CurrentAccelerationCS = AccelerationCS;
	FQuat CurrentFacingCS = ForwardFacingCS;
	float AccumulatedSeconds = 0.f;

	FQuat ControllerRotationPerStep = (ControllerRotationRate * SecondsPerPredictionSample).Quaternion();

	for (int32 Index = NumHistorySamples + 1; Index < Trajectory.Samples.Num(); ++Index)
	{
		CurrentPositionCS += CurrentVelocityCS * SecondsPerPredictionSample;
		AccumulatedSeconds += SecondsPerPredictionSample;

		// Account for the controller (e.g. the camera) rotating.
		CurrentFacingCS = ControllerRotationPerStep * CurrentFacingCS;
		CurrentAccelerationCS = ControllerRotationPerStep * CurrentAccelerationCS;

		Trajectory.Samples[Index].Position = CurrentPositionCS;
		Trajectory.Samples[Index].Facing = CurrentFacingCS;
		Trajectory.Samples[Index].AccumulatedSeconds = AccumulatedSeconds;

		FVector NewVelocityCS = FVector::ZeroVector;
		UCharacterMovementTrajectoryLibrary::StepCharacterMovementGroundPrediction(
			SecondsPerPredictionSample, CurrentVelocityCS, CurrentAccelerationCS, CharacterMovementComponent,
			NewVelocityCS);
		CurrentVelocityCS = NewVelocityCS;

		if (CharacterMovementComponent->bOrientRotationToMovement && !CurrentAccelerationCS.IsNearlyZero())
		{
			// Rotate towards acceleration.
			CurrentFacingCS = FMath::QInterpConstantTo(CurrentFacingCS, CurrentAccelerationCS.ToOrientationQuat(), SecondsPerPredictionSample, RotateTowardsMovementSpeed);
		}
	}
}

// Calculate how much the character is rotating each update due to the controller (e.g. the camera) rotating.
// E.g. If the user is moving forward but rotating the camera, the character (and thus future accelerations, facing directions, etc) will rotate.
FRotator UCharacterTrajectoryComponent::CalculateControllerRotationRate(float DeltaSeconds, bool bShouldRemainVertical)
{
	check(GetOwner());

	// UpdateTrajectory handles DeltaSeconds == 0.f, so we should never hit this.
	check(DeltaSeconds > 0.f);

	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	if (CharacterOwner == nullptr || CharacterOwner->Controller == nullptr)
	{
		// @todo: Simulated proxies don't have controllers, so they'll need some other mechanism to account for controller rotation rate.
		return FRotator::ZeroRotator;
	}

	FRotator DesiredControllerRotation = CharacterOwner->Controller->GetDesiredRotation();
	if (bShouldRemainVertical)
	{
		DesiredControllerRotation.Yaw = FRotator::NormalizeAxis(DesiredControllerRotation.Yaw);
		DesiredControllerRotation.Pitch = 0.f;
		DesiredControllerRotation.Roll = 0.f;
	}

	const FRotator DesiredRotationDelta = DesiredControllerRotation - DesiredControllerRotationLastUpdate;
	DesiredControllerRotationLastUpdate = DesiredControllerRotation;

	return DesiredRotationDelta.GetNormalized() * (1.f / DeltaSeconds);
}