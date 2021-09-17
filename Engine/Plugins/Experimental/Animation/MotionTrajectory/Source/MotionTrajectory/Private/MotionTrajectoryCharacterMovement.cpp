// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotionTrajectoryCharacterMovement.h"

#include "Animation/AnimInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "KismetAnimationLibrary.h"

// ----------- BEGIN Derived from FCharacterMovementComponentAsyncInput::CalcVelocity() ----------- //

static void StepPrediction(float IntegrationDelta
	, const UCharacterMovementComponent* MovementComponent
	, FTrajectorySample& Sample)
{
	if (MovementComponent->GetCurrentAcceleration().IsZero())
	{
		float ActualBrakingFriction = (MovementComponent->bUseSeparateBrakingFriction ? MovementComponent->BrakingFriction : MovementComponent->GroundFriction);

		if (!Sample.LocalLinearVelocity.IsZero())
		{
			const float FrictionFactor = FMath::Max(0.f, MovementComponent->BrakingFrictionFactor);
			ActualBrakingFriction = FMath::Max(0.f, ActualBrakingFriction * FrictionFactor);
				
			const float BrakingDeceleration = FMath::Max(0.f, MovementComponent->GetMaxBrakingDeceleration());
			const bool bZeroFriction = (ActualBrakingFriction == 0.f);
			const bool bZeroBraking = (BrakingDeceleration == 0.f);

			if (bZeroFriction && bZeroBraking)
			{
				return;
			}

			float RemainingTime = IntegrationDelta;
			const float MaxTimeStep = FMath::Clamp(MovementComponent->BrakingSubStepTime, 1.f / 75.f, 1.f / 20.f);

			const FVector PrevLinearVelocity = Sample.LocalLinearVelocity;
			const FVector RevAccel = (bZeroBraking ? FVector::ZeroVector : (-BrakingDeceleration * Sample.LocalLinearVelocity.GetSafeNormal()));

			while (RemainingTime >= UCharacterMovementComponent::MIN_TICK_TIME)
			{
				const float dt = ((RemainingTime > MaxTimeStep && !bZeroFriction) ? FMath::Min(MaxTimeStep, RemainingTime * 0.5f) : RemainingTime);
				RemainingTime -= dt;

				Sample.LocalLinearVelocity = Sample.LocalLinearVelocity + ((-ActualBrakingFriction) * Sample.LocalLinearVelocity + RevAccel) * dt;

				if ((Sample.LocalLinearVelocity | PrevLinearVelocity) <= 0.f)
				{
					Sample.LocalLinearVelocity = FVector::ZeroVector;
					return;
				}
			}

			// Clamp to zero if nearly zero, or if below min threshold and braking
			const float VSizeSq = Sample.LocalLinearVelocity.SizeSquared();
			if (VSizeSq <= KINDA_SMALL_NUMBER || (!bZeroBraking && VSizeSq <= FMath::Square(UCharacterMovementComponent::BRAKE_TO_STOP_VELOCITY)))
			{
				Sample.LocalLinearVelocity = FVector::ZeroVector;
			}
		}
	}
	else
	{
		const FVector AccelDir = Sample.LocalLinearAcceleration.GetSafeNormal();
		const float VelSize = Sample.LocalLinearVelocity.Size();
		Sample.LocalLinearVelocity = Sample.LocalLinearVelocity - (Sample.LocalLinearVelocity - AccelDir * VelSize) * FMath::Min(IntegrationDelta * MovementComponent->GroundFriction, 1.f);

		const float MaxInputSpeed = FMath::Max(MovementComponent->GetMaxSpeed() * MovementComponent->GetAnalogInputModifier(), MovementComponent->GetMinAnalogSpeed());
		Sample.LocalLinearVelocity += Sample.LocalLinearAcceleration * IntegrationDelta;
		Sample.LocalLinearVelocity = Sample.LocalLinearVelocity.GetClampedToMaxSize(MaxInputSpeed);
	}

	Sample.Position += Sample.LocalLinearVelocity * IntegrationDelta;
}

static void PredictTrajectory(const UCharacterMovementComponent* MovementComponent
	, int32 SampleRate
	, int32 MaxSamples
	, const FMotionTrajectorySettings& Settings
	, const FTrajectorySample& PresentTrajectory
	, FTrajectorySampleRange& ReturnValue)
{
	ReturnValue.SampleRate = SampleRate;
	const float IntegrationDelta = 1.f / static_cast<float>(SampleRate);

	if (!!Settings.Domain)
	{
		FTrajectorySample Sample = PresentTrajectory;
		FTrajectorySample PreviousSample = PresentTrajectory;
		float AccumulatedDistance = 0.f;
		float AccumulatedSeconds = 0.f;

		constexpr int32 DistanceDomainMask = static_cast<int32>(ETrajectorySampleDomain::Distance);
		constexpr int32 TimeDomainMask = static_cast<int32>(ETrajectorySampleDomain::Time);

		for (int32 Step = 0; Step < MaxSamples; ++Step)
		{
			PreviousSample = Sample;
			StepPrediction(IntegrationDelta, MovementComponent, Sample);

			AccumulatedDistance += FVector::Distance(PreviousSample.Position, Sample.Position);
			Sample.AccumulatedDistance = AccumulatedDistance;
			AccumulatedSeconds += IntegrationDelta;
			Sample.AccumulatedSeconds = AccumulatedSeconds;

			if (FMath::IsNearlyEqual(FMath::Abs(Sample.AccumulatedDistance - PreviousSample.AccumulatedDistance), SMALL_NUMBER))
			{
				break;
			}

			ReturnValue.Samples.Add(Sample);

			if (((Settings.Domain & DistanceDomainMask) == DistanceDomainMask)
				&& (Settings.Distance > 0.f)
				&& (Sample.AccumulatedDistance < Settings.Distance))
			{
				continue;
			}

			if (((Settings.Domain & TimeDomainMask) == TimeDomainMask)
				&& (Settings.Seconds > 0.f)
				&& (Step * IntegrationDelta < Settings.Seconds)
				&& (Sample.AccumulatedDistance > PreviousSample.AccumulatedDistance))
			{
				continue;
			}

			break;
		}
	}
}

// ------------ END Derived from FCharacterMovementComponentAsyncInput::CalcVelocity() ------------ //

FTrajectorySample UCharacterMovementTrajectoryComponent::GetPresentTrajectory() const
{
	FTrajectorySample ReturnValue;
	
	const APawn* Pawn = TryGetOwnerPawn();
	if (!Pawn)
	{
		return ReturnValue;
	}

	const UCharacterMovementComponent* MovementComponent = Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent());
	if (ensure(MovementComponent) && MovementComponent->MovementMode == EMovementMode::MOVE_Walking)
	{
		const USkeletalMeshComponent* SkeletalMeshComponent = Pawn->FindComponentByClass<USkeletalMeshComponent>();
		FTransform ComponentWorldTransform = SkeletalMeshComponent ? SkeletalMeshComponent->GetComponentTransform() : FTransform();

		ReturnValue.LocalLinearVelocity = ComponentWorldTransform.InverseTransformVectorNoScale(MovementComponent->Velocity);
		ReturnValue.LocalLinearAcceleration = ComponentWorldTransform.InverseTransformVectorNoScale(MovementComponent->GetCurrentAcceleration());
	}
	return ReturnValue;
}

FTransform UCharacterMovementTrajectoryComponent::GetPresentWorldTransform() const
{
	FTransform ReturnValue = FTransform::Identity;

	const APawn* Pawn = TryGetOwnerPawn();
	if (!Pawn)
	{
		return ReturnValue;
	}

	const USkeletalMeshComponent* SkeletalMeshComponent = Pawn->FindComponentByClass<USkeletalMeshComponent>();
	ReturnValue = SkeletalMeshComponent ? SkeletalMeshComponent->GetComponentTransform() : Pawn->GetActorTransform();

	return ReturnValue;
}

UCharacterMovementTrajectoryComponent::UCharacterMovementTrajectoryComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	bWantsInitializeComponent = true;
}

void UCharacterMovementTrajectoryComponent::InitializeComponent()
{
	// Add a tick dependency on the character movement component since this component implementation is reliant on its internal state
	UCharacterMovementComponent* CharacterMovementComponent = GetOwner()->FindComponentByClass<UCharacterMovementComponent>();
	AddTickPrerequisiteComponent(CharacterMovementComponent);

	Super::InitializeComponent();
}

void UCharacterMovementTrajectoryComponent::UninitializeComponent()
{
	UCharacterMovementComponent* CharacterMovementComponent = GetOwner()->FindComponentByClass<UCharacterMovementComponent>();
	RemoveTickPrerequisiteComponent(CharacterMovementComponent);

	Super::UninitializeComponent();
}

FTrajectorySampleRange UCharacterMovementTrajectoryComponent::GetTrajectory() const
{
	return GetTrajectoryWithSettings(PredictionSettings, bPredictionIncludesHistory);
}

FTrajectorySampleRange UCharacterMovementTrajectoryComponent::GetTrajectoryWithSettings(const FMotionTrajectorySettings& Settings
	, bool bIncludeHistory) const
{
	const APawn* Pawn = TryGetOwnerPawn();
	if (!Pawn)
	{
		return FTrajectorySampleRange(SampleRate);
	}

	const UCharacterMovementComponent* MovementComponent = Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent());

	// Currently the trajectory prediction only supports the walking movement mode of the character movement component
	if (ensure(MovementComponent) && MovementComponent->MovementMode == EMovementMode::MOVE_Walking)
	{
		// Step the prediction iteratively towards the specified domain horizon(s)
		FTrajectorySampleRange Prediction(SampleRate);
		PredictTrajectory(MovementComponent, SampleRate, MaxSamples, Settings, PresentTrajectory, Prediction);

		// Combine past, present, and future into a uniformly sampled complete trajectory
		return CombineHistoryPresentPrediction(bIncludeHistory, Prediction);
	}
	else
	{
		return FTrajectorySampleRange(SampleRate);
	}
}