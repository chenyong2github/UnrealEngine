// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotionTrajectoryCharacterMovement.h"

#include "Animation/AnimInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "KismetAnimationLibrary.h"

// ----------- BEGIN Derived from FCharacterMovementComponentAsyncInput::CalcVelocity() ----------- //

static void StepPrediction(float IntegrationDelta
	, const UCharacterMovementComponent* MovementComponent
	, FTrajectorySample& Sample)
{
	const FTransform InitialSampleTransform = Sample.Transform;

	if (MovementComponent->GetCurrentAcceleration().IsZero())
	{
		float ActualBrakingFriction = (MovementComponent->bUseSeparateBrakingFriction ? MovementComponent->BrakingFriction : MovementComponent->GroundFriction);

		if (!Sample.LinearVelocity.IsZero())
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

			const FVector PrevLinearVelocity = Sample.LinearVelocity;
			const FVector RevAccel = (bZeroBraking ? FVector::ZeroVector : (-BrakingDeceleration * Sample.LinearVelocity.GetSafeNormal()));

			while (RemainingTime >= UCharacterMovementComponent::MIN_TICK_TIME)
			{
				const float dt = ((RemainingTime > MaxTimeStep && !bZeroFriction) ? FMath::Min(MaxTimeStep, RemainingTime * 0.5f) : RemainingTime);
				RemainingTime -= dt;

				Sample.LinearVelocity = Sample.LinearVelocity + ((-ActualBrakingFriction) * Sample.LinearVelocity + RevAccel) * dt;

				if ((Sample.LinearVelocity | PrevLinearVelocity) <= 0.f)
				{
					Sample.LinearVelocity = FVector::ZeroVector;
					return;
				}
			}

			// Clamp to zero if nearly zero, or if below min threshold and braking
			const float VSizeSq = Sample.LinearVelocity.SizeSquared();
			if (VSizeSq <= KINDA_SMALL_NUMBER || (!bZeroBraking && VSizeSq <= FMath::Square(UCharacterMovementComponent::BRAKE_TO_STOP_VELOCITY)))
			{
				Sample.LinearVelocity = FVector::ZeroVector;
			}
		}
	}
	else
	{
		const FVector AccelDir = Sample.LinearAcceleration.GetSafeNormal();
		const float VelSize = Sample.LinearVelocity.Size();
		Sample.LinearVelocity = Sample.LinearVelocity - (Sample.LinearVelocity - AccelDir * VelSize) * FMath::Min(IntegrationDelta * MovementComponent->GroundFriction, 1.f);

		const float MaxInputSpeed = FMath::Max(MovementComponent->GetMaxSpeed() * MovementComponent->GetAnalogInputModifier(), MovementComponent->GetMinAnalogSpeed());
		Sample.LinearVelocity += Sample.LinearAcceleration * IntegrationDelta;
		Sample.LinearVelocity = Sample.LinearVelocity.GetClampedToMaxSize(MaxInputSpeed);
	}

	const FTransform InitialTransformWS = InitialSampleTransform * MovementComponent->GetOwner()->GetActorTransform();

	if (MovementComponent->bOrientRotationToMovement)
	{
		FRotator CurrentRotation = InitialTransformWS.GetRotation().Rotator();
		CurrentRotation.DiagnosticCheckNaN(TEXT("CharacterMovementComponent::PhysicsRotation(): CurrentRotation"));

		FRotator DeltaRot = MovementComponent->GetDeltaRotation(IntegrationDelta);
		DeltaRot.DiagnosticCheckNaN(TEXT("CharacterMovementComponent::PhysicsRotation(): GetDeltaRotation"));

		FRotator DesiredRotation = 
			MovementComponent->ComputeOrientToMovementRotation(CurrentRotation, IntegrationDelta, DeltaRot);

		// TODO: leaving this code commented here as a reference for later combining both modes so we have one 
		// component of rotation targeting a fixed direction (above) and the below being extrapolated
		//FRotator DesiredRotation = CurrentRotation;
		//if (MovementComponent->bOrientRotationToMovement)
		//{
		//	DesiredRotation = MovementComponent->ComputeOrientToMovementRotation(CurrentRotation, IntegrationDelta, DeltaRot);
		//}
		//else if (MovementComponent->GetCharacterOwner()->Controller && MovementComponent->bUseControllerDesiredRotation)
		//{
		//	DesiredRotation = MovementComponent->GetCharacterOwner()->Controller->GetDesiredRotation();
		//}

		if (MovementComponent->ShouldRemainVertical())
		{
			DesiredRotation.Pitch = 0.f;
			DesiredRotation.Yaw = FRotator::NormalizeAxis(DesiredRotation.Yaw);
			DesiredRotation.Roll = 0.f;
		}
		else
		{
			DesiredRotation.Normalize();
		}

		// Accumulate a desired new rotation.
		const float AngleTolerance = 1e-3f;

		if (!CurrentRotation.Equals(DesiredRotation, AngleTolerance))
		{
			// PITCH
			if (!FMath::IsNearlyEqual(CurrentRotation.Pitch, DesiredRotation.Pitch, AngleTolerance))
			{
				DesiredRotation.Pitch = FMath::FixedTurn(CurrentRotation.Pitch, DesiredRotation.Pitch, DeltaRot.Pitch);
			}

			// YAW
			if (!FMath::IsNearlyEqual(CurrentRotation.Yaw, DesiredRotation.Yaw, AngleTolerance))
			{
				DesiredRotation.Yaw = FMath::FixedTurn(CurrentRotation.Yaw, DesiredRotation.Yaw, DeltaRot.Yaw);
			}

			// ROLL
			if (!FMath::IsNearlyEqual(CurrentRotation.Roll, DesiredRotation.Roll, AngleTolerance))
			{
				DesiredRotation.Roll = FMath::FixedTurn(CurrentRotation.Roll, DesiredRotation.Roll, DeltaRot.Roll);
			}

			// Set the new rotation.
			DesiredRotation.DiagnosticCheckNaN(TEXT("CharacterMovementComponent::PhysicsRotation(): DesiredRotation"));
			const FQuat DesiredRotationQuat = DesiredRotation.Quaternion();
			const FQuat DesiredRotationQuatAS = MovementComponent->GetOwner()->GetActorQuat().Inverse() * DesiredRotationQuat;
			Sample.Transform.SetRotation(DesiredRotationQuatAS);
		}

		const FQuat DeltaRotation = InitialSampleTransform.GetRotation().Inverse() * Sample.Transform.GetRotation();

		// since we're not extrapolating the rotation on this mode, the linear component's don't rotate
		//Sample.LinearVelocity = DeltaRotation.RotateVector(Sample.LinearVelocity);
		//Sample.LinearAcceleration = DeltaRotation.UnrotateVector(Sample.LinearAcceleration);
	}
	else
	{
		const FQuat DeltaRotation = FQuat(Sample.AngularVelocityAxis, Sample.AngularSpeed * IntegrationDelta);
		const FQuat SampleRotation = Sample.Transform.GetRotation() * DeltaRotation;
		Sample.Transform.SetRotation(SampleRotation);

		Sample.LinearVelocity = DeltaRotation.RotateVector(Sample.LinearVelocity);
		Sample.LinearAcceleration = DeltaRotation.RotateVector(Sample.LinearAcceleration);
	}

	
	const FVector Translation = Sample.LinearVelocity * IntegrationDelta;
	Sample.Transform.AddToTranslation(Translation);
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

			AccumulatedDistance += 
				FVector::Distance(PreviousSample.Transform.GetLocation(), Sample.Transform.GetLocation());
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

FTrajectorySample UCharacterMovementTrajectoryComponent::CalcWorldSpacePresentTrajectorySample(float DeltaTime) const
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
		FTransform ComponentWorldTransform = Pawn->GetActorTransform();
		FVector MovementComponentPosition = MovementComponent->GetLastUpdateLocation();

		ReturnValue.Transform = ComponentWorldTransform;
		ReturnValue.LinearVelocity = MovementComponent->Velocity;
		ReturnValue.LinearAcceleration = MovementComponent->GetCurrentAcceleration();

		if (DeltaTime > SMALL_NUMBER)
		{
			const FQuat DeltaRotation = 
				PresentTrajectorySampleWS.Transform.GetRotation().Inverse() * ComponentWorldTransform.GetRotation();
			FVector DeltaRotationAxis;
			float DeltaRotationAngle;
			DeltaRotation.ToAxisAndAngle(DeltaRotationAxis, DeltaRotationAngle);
			const float AngularSpeed = DeltaRotationAngle / DeltaTime;

			ReturnValue.AngularVelocityAxis = DeltaRotationAxis;
			ReturnValue.AngularSpeed = AngularSpeed;
		}
		else
		{
			ReturnValue.AngularVelocityAxis = PresentTrajectorySampleWS.AngularVelocityAxis;
			ReturnValue.AngularSpeed = PresentTrajectorySampleWS.AngularSpeed;
		}
	}

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

	if (ACharacter* Character = CharacterMovementComponent->GetCharacterOwner())
	{
		Character->OnCharacterMovementUpdated.AddDynamic(
			this, 
			&UCharacterMovementTrajectoryComponent::OnMovementUpdated);
	}

	Super::InitializeComponent();
}

void UCharacterMovementTrajectoryComponent::UninitializeComponent()
{
	UCharacterMovementComponent* CharacterMovementComponent = GetOwner()->FindComponentByClass<UCharacterMovementComponent>();
	RemoveTickPrerequisiteComponent(CharacterMovementComponent);

	Super::UninitializeComponent();
}

void UCharacterMovementTrajectoryComponent::OnMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
	TickTrajectory(DeltaSeconds);
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
		PredictTrajectory(MovementComponent, SampleRate, MaxSamples, Settings, PresentTrajectorySampleLS, Prediction);

		// Combine past, present, and future into a uniformly sampled complete trajectory
		return CombineHistoryPresentPrediction(bIncludeHistory, Prediction);
	}
	else
	{
		return FTrajectorySampleRange(SampleRate);
	}
}