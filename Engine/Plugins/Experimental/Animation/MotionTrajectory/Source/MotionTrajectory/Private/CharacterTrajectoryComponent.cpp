// Copyright Epic Games, Inc. All Rights Reserved.

#include "CharacterTrajectoryComponent.h"

#include "CharacterMovementTrajectoryLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "MotionTrajectory.h"

#include "MotionTrajectoryCharacterMovement.h"
#include "MotionTrajectoryLibrary.h"

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

	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->OnCharacterMovementUpdated.AddDynamic(this, &UCharacterTrajectoryComponent::OnMovementUpdated);
	}
	else
	{
		UE_LOG(LogMotionTrajectory, Error, TEXT("UCharacterTrajectoryComponent requires its owner to be ACharacter"));
	}

	NumHistorySamples = FMath::CeilToInt32(HistoryLengthSeconds * HistorySamplesPerSecond);
	SecondsPerHistorySample = 1.f / HistorySamplesPerSecond;

	const int32 NumPredictionSamples = FMath::CeilToInt32(PredictionLengthSeconds * PredictionSamplesPerSecond);
	SecondsPerPredictionSample = 1.f / PredictionSamplesPerSecond;

	// History + current sample + prediction
	Trajectory.Samples.Init(FPoseSearchQueryTrajectorySample(), NumHistorySamples + 1 + NumPredictionSamples);
}

void UCharacterTrajectoryComponent::UninitializeComponent()
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->OnCharacterMovementUpdated.RemoveDynamic(this, &UCharacterTrajectoryComponent::OnMovementUpdated);
	}
	else
	{
		UE_LOG(LogMotionTrajectory, Error, TEXT("UCharacterTrajectoryComponent requires its owner to be ACharacter"));
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
}

void UCharacterTrajectoryComponent::OnMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
	if (FMath::IsNearlyZero(DeltaSeconds))
	{
		return;
	}

	if (!ensure(CharacterMovementComponent != nullptr && SkelMeshComponent != nullptr))
	{
		return;
	}

	const FTransform PreviousSkelMeshComponentTransformWS = SkelMeshComponentTransformWS;
	SkelMeshComponentTransformWS = SkelMeshComponent->GetComponentTransform();

	const FTransform SkelMeshTransformDelta = SkelMeshComponentTransformWS.GetRelativeTransform(PreviousSkelMeshComponentTransformWS);
	UpdateHistory(DeltaSeconds, SkelMeshTransformDelta);

	const FVector VelocityCS = SkelMeshComponentTransformWS.InverseTransformVectorNoScale(CharacterMovementComponent->Velocity);
	const FVector AccelerationCS = SkelMeshComponentTransformWS.InverseTransformVectorNoScale(CharacterMovementComponent->GetCurrentAcceleration());
	UpdatePrediction(VelocityCS , AccelerationCS);

#if ENABLE_ANIM_DEBUG
	if (CVarCharacterTrajectoryDebug.GetValueOnAnyThread() == 1)
	{
		Trajectory.DebugDrawTrajectory(GetWorld(), SkelMeshComponent->GetComponentTransform());
	}
#endif // ENABLE_ANIM_DEBUG

	// Convert to FTrajectorySampleRange for testing with the Motion Matching node.
	// This is temporary to support compatibility with existing data until we switch Motion Matching to use FPoseSearchTrajectory.
	Temp_TrajectorySampleRange.Samples.Reset(Trajectory.Samples.Num());
	for (const FPoseSearchQueryTrajectorySample& Sample : Trajectory.Samples)
	{
		FTrajectorySample TempSample;
		TempSample.Transform.SetLocation(Sample.Position);
		TempSample.AccumulatedSeconds = Sample.AccumulatedSeconds;

		Temp_TrajectorySampleRange.Samples.Add(TempSample);
	}
}

void UpdateHistorySample(FPoseSearchQueryTrajectorySample& Sample, float DeltaSeconds, const FTransform& DeltaTransformCS)
{
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
	if (NumHistorySamples > 0 &&
		(FMath::Abs(Trajectory.Samples[NumHistorySamples - 1].AccumulatedSeconds) >= SecondsPerHistorySample))
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

void UCharacterTrajectoryComponent::UpdatePrediction(const FVector& VelocityCS, const FVector& AccelerationCS)
{
	check(CharacterMovementComponent);

	FVector CurrentPositionCS = FVector::ZeroVector;
	FVector CurrentVelocityCS = VelocityCS;
	float AccumulatedSeconds = 0.f;

	for (int32 Index = NumHistorySamples + 1; Index < Trajectory.Samples.Num(); ++Index)
	{
		CurrentPositionCS += CurrentVelocityCS * SecondsPerPredictionSample;
		AccumulatedSeconds += SecondsPerPredictionSample;

		Trajectory.Samples[Index].Position = CurrentPositionCS;
		Trajectory.Samples[Index].AccumulatedSeconds = AccumulatedSeconds;

		FVector NewVelocityCS = FVector::ZeroVector;
		UCharacterMovementTrajectoryLibrary::StepCharacterMovementGroundPrediction(SecondsPerPredictionSample, CurrentVelocityCS, AccelerationCS, CharacterMovementComponent, NewVelocityCS);
		CurrentVelocityCS = NewVelocityCS;
	}
}