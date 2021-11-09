// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavigationProcessor.h"

#include "MassCommonUtils.h"
#include "MassCommandBuffer.h"
#include "MassCommonFragments.h"
#include "MassAIMovementFragments.h"
#include "MassMovementTypes.h"
#include "MassSignals/Public/MassSignalSubsystem.h"
#include "Math/UnrealMathUtility.h"
#include "VisualLogger/VisualLogger.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/Timespan.h"
#include "MassLODTypes.h"
#include "MassSimulationLOD.h"
#include "MassAvoidanceSettings.h"

#define UNSAFE_FOR_MT 0
#define MOVEMENT_DEBUGDRAW 0	// Set to 1 to see heading debugdraw

namespace UE::MassMovement
{
	int32 bFreezeMovement = 0;
	FAutoConsoleVariableRef CVarFreezeMovement(TEXT("ai.debug.mass.FreezeMovement"), bFreezeMovement, TEXT("Freeze any movement by the UMassApplyVelocityMoveTargetProcessor"));

	static float Damp(const float X, const float Goal, const float HalfLife, const float DeltaTime)
	{
		return FMath::Lerp(X, Goal, 1.f - FMath::Pow(2.f, -DeltaTime / (HalfLife + KINDA_SMALL_NUMBER)));
	}

	// Calculates yaw angle from direction vector.
	static float GetYawFromDirection(const FVector Direction)
	{
		return FMath::Atan2(Direction.Y, Direction.X);
	}

	// Wraps and angle to range -PI..PI. Angle in radians.
	static float WrapAngle(const float Angle)
	{
		float WrappedAngle = FMath::Fmod(Angle, PI*2.0f);

		if (WrappedAngle > PI)
		{
			WrappedAngle -= PI*2.0f;
		}

		if (WrappedAngle < -PI)
		{
			WrappedAngle += PI*2.0f;
		}

		return WrappedAngle;
	}

	// Linearly interpolates between two angles (in Radians).
	static float LerpAngle(const float AngleA, const float AngleB, const float T)
	{
		const float DeltaAngle = WrapAngle(AngleB - AngleA);
		return AngleA + DeltaAngle * T;
	}

	// Exponential smooth from current angle to target angle. Angles in radians.
	static float ExponentialSmoothingAngle(const float Angle, const float TargetAngle, const float DeltaTime, const float SmoothingTime)
	{
		// Note: based on FMath::ExponentialSmoothingApprox().
		if (SmoothingTime < KINDA_SMALL_NUMBER)
		{
			return TargetAngle;
		}
		const float A = DeltaTime / SmoothingTime;
		const float Exp = FMath::InvExpApprox(A);
		return TargetAngle + WrapAngle(Angle - TargetAngle) * Exp;
	}

} // UE::MassMovement

//----------------------------------------------------------------------//
//  UMassApplyVelocityMoveTargetProcessor
//----------------------------------------------------------------------//
UMassApplyVelocityMoveTargetProcessor::UMassApplyVelocityMoveTargetProcessor()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Avoidance;
	ExecutionOrder.ExecuteAfter.Add(TEXT("MassAvoidanceProcessor"));
}

void UMassApplyVelocityMoveTargetProcessor::ConfigureQueries()
{
	HighResEntityQuery.AddRequirement<FMassMoveTargetFragment>(EMassFragmentAccess::ReadOnly);
	HighResEntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	HighResEntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadWrite);
	HighResEntityQuery.AddRequirement<FMassSteeringFragment>(EMassFragmentAccess::ReadWrite);
	HighResEntityQuery.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::None);

	LowResEntityQuery_Conditional.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadWrite);
	LowResEntityQuery_Conditional.AddRequirement<FMassMoveTargetFragment>(EMassFragmentAccess::ReadOnly);
	LowResEntityQuery_Conditional.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::All);
	LowResEntityQuery_Conditional.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	LowResEntityQuery_Conditional.SetChunkFilter(&FMassSimulationVariableTickChunkFragment::ShouldTickChunkThisFrame);
}

void UMassApplyVelocityMoveTargetProcessor::Execute(UMassEntitySubsystem& EntitySubsystem,
													FMassExecutionContext& Context)
{
	// Clamp max delta time to avoid force explosion on large time steps (i.e. during initialization).
	const float TimeDelta = FMath::Min(0.1f, Context.GetDeltaTimeSeconds());

	const UMassAvoidanceSettings* Settings = UMassAvoidanceSettings::Get();
	check(Settings);

	{
		QUICK_SCOPE_CYCLE_COUNTER(HighRes);

		HighResEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this, Settings, TimeDelta](FMassExecutionContext& Context)
		{
			const int32 NumEntities = Context.GetNumEntities();
			constexpr float ZDamperHalfLife = 0.2f;

			const float OrientationEndOfPathHeadingAnticipation = FMath::Max(KINDA_SMALL_NUMBER, Settings->OrientationEndOfPathHeadingAnticipation);
			const float OrientationSmoothingTime = Settings->OrientationSmoothingTime;
			constexpr float OrientationBlendWhileMoving = 0.4f;
			constexpr float OrientationBlendWhileStanding = 0.95f;

			const TConstArrayView<FMassMoveTargetFragment> MoveTargetList = Context.GetFragmentView<FMassMoveTargetFragment>();
			const TArrayView<FDataFragment_Transform> LocationList = Context.GetMutableFragmentView<FDataFragment_Transform>();
			const TArrayView<FMassSteeringFragment> SteeringList = Context.GetMutableFragmentView<FMassSteeringFragment>();
			const TArrayView<FMassVelocityFragment> VelocityList = Context.GetMutableFragmentView<FMassVelocityFragment>();

			for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
			{
				const FMassMoveTargetFragment& MoveTarget = MoveTargetList[EntityIndex];
				FMassSteeringFragment& Steering = SteeringList[EntityIndex];
				FMassVelocityFragment& Velocity = VelocityList[EntityIndex];

				// Do not touch transform at all when animating
				if (MoveTarget.GetCurrentAction() == EMassMovementAction::Animate)
				{
					Velocity.Value = FVector::ZeroVector;
					Steering.Reset();
					continue;
				}

				// Update velocity from steering forces.
				Velocity.Value += Steering.SteeringForce * TimeDelta;
				Velocity.Value.Z = 0.0f;

				constexpr float LowSpeedThreshold = 3.0f;

				if (MoveTargetList[EntityIndex].GetCurrentAction() == EMassMovementAction::Stand)
				{
					// Clamp small velocities in stand to zero to avoid tiny drifting.
					if (Velocity.Value.SquaredLength() < FMath::Square(LowSpeedThreshold))
					{
						Velocity.Value = FVector::ZeroVector;
					}
				}

				FVector DeltaLoc = Velocity.Value * TimeDelta;

#if WITH_MASSGAMEPLAY_DEBUG
				if (UE::MassMovement::bFreezeMovement)
				{
					DeltaLoc.X = 0;
					DeltaLoc.Y = 0;
					Velocity.Value = FVector::ZeroVector;
				}
#endif // WITH_MASSGAMEPLAY_DEBUG

				// Apply DeltaLoc on X,Y and set the Z of the current lane location.
				FTransform& CurrentTransform = LocationList[EntityIndex].GetMutableTransform();

				const float Z = CurrentTransform.GetLocation().Z;
				const float TargetZ = MoveTarget.Center.Z;
				const float NewZ = UE::MassMovement::Damp(Z, TargetZ, ZDamperHalfLife, TimeDelta);
				DeltaLoc.Z = NewZ - Z;

				CurrentTransform.AddToTranslation(DeltaLoc);

				// Direction
				const FVector CurrentForward = CurrentTransform.GetRotation().GetForwardVector();
				const float CurrentHeading = UE::MassMovement::GetYawFromDirection(CurrentForward);

				float Blend = 0.0f;
				if (MoveTarget.GetCurrentAction() == EMassMovementAction::Move)
				{
					if (MoveTarget.IntentAtGoal == EMassMovementAction::Stand && MoveTarget.DistanceToGoal < OrientationEndOfPathHeadingAnticipation)
					{
						// Fade towards the movement target direction at the end of the path.
						const float Strength = 1.0f - FMath::Square(FMath::Clamp(MoveTarget.DistanceToGoal / OrientationEndOfPathHeadingAnticipation, 0.0f, 1.0));
						Blend = FMath::Lerp(Blend, OrientationBlendWhileStanding, Strength);
					}
					else
					{
						Blend = OrientationBlendWhileMoving;
					}
				}
				else // Stand
				{
					Blend = OrientationBlendWhileStanding;
				}

				const float VelocityHeading = UE::MassMovement::GetYawFromDirection(Velocity.Value);
				const float MovementHeading = UE::MassMovement::GetYawFromDirection(MoveTarget.Forward);
				const float DesiredHeading = UE::MassMovement::LerpAngle(VelocityHeading, MovementHeading, Blend);
				const float NewHeading = UE::MassMovement::ExponentialSmoothingAngle(CurrentHeading, DesiredHeading, TimeDelta, OrientationSmoothingTime);

				FQuat Rotation(FVector::UpVector, NewHeading);
				CurrentTransform.SetRotation(Rotation);
			}
		});
	}

	{
		QUICK_SCOPE_CYCLE_COUNTER(LowRes);

		LowResEntityQuery_Conditional.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
			{
#if WITH_MASSGAMEPLAY_DEBUG
				if (UE::MassMovement::bFreezeMovement)
				{
					return;
				}
#endif // WITH_MASSGAMEPLAY_DEBUG
				const int32 NumEntities = Context.GetNumEntities();

				const TArrayView<FDataFragment_Transform> LocationList = Context.GetMutableFragmentView<FDataFragment_Transform>();
				const TConstArrayView<FMassMoveTargetFragment> MoveTargetList = Context.GetFragmentView<FMassMoveTargetFragment>();

				for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
				{
					FTransform& CurrentTransform = LocationList[EntityIndex].GetMutableTransform();
					const FMassMoveTargetFragment& MoveTarget = MoveTargetList[EntityIndex];

					// Snap position to move target directly
					CurrentTransform.SetLocation(MoveTarget.Center);
					CurrentTransform.SetRotation(FQuat::FindBetweenNormals(FVector::ForwardVector, MoveTarget.Forward));
				}
			});
	}
}

//----------------------------------------------------------------------//
//  UMassDynamicObstacleProcessor
//----------------------------------------------------------------------//
UMassDynamicObstacleProcessor::UMassDynamicObstacleProcessor()
{
	bAutoRegisterWithProcessingPhases = false;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::SyncWorldToMass);
}

void UMassDynamicObstacleProcessor::ConfigureQueries()
{
	EntityQuery_Conditional.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FDataFragment_AgentRadius>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassDynamicObstacleFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery_Conditional.SetChunkFilter(&FMassSimulationVariableTickChunkFragment::ShouldTickChunkThisFrame);
}

void UMassDynamicObstacleProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery_Conditional.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
		{
			const int32 NumEntities = Context.GetNumEntities();

			const TConstArrayView<FDataFragment_Transform> LocationList = Context.GetFragmentView<FDataFragment_Transform>();
			const TConstArrayView<FDataFragment_AgentRadius> RadiusList = Context.GetFragmentView<FDataFragment_AgentRadius>();
			const TArrayView<FMassDynamicObstacleFragment> ObstacleDataList = Context.GetMutableFragmentView<FMassDynamicObstacleFragment>();

			const FDateTime Now = FDateTime::UtcNow();

			for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
			{
				// @todo: limit update frequency, this does not need to occur every frame
				const FVector Position = LocationList[EntityIndex].GetTransform().GetLocation();
				const float Radius = RadiusList[EntityIndex].Radius;
				FMassDynamicObstacleFragment& Obstacle = ObstacleDataList[EntityIndex];

				UE_VLOG_LOCATION(this, LogMassDynamicObstacle, Display, Position, Radius, Obstacle.bHasStopped ? FColor::Red : FColor::Green, TEXT(""));

				if ((Position - Obstacle.LastPosition).SquaredLength() < FMath::Square(DistanceBuffer))
				{
					const FTimespan TimeElapsed = Now - Obstacle.LastMovedTimeStamp;
					if (TimeElapsed.GetSeconds() > DelayBeforeStopNotification && !Obstacle.bHasStopped)
					{
						// The obstacle hasn't moved for a while.
						Obstacle.bHasStopped = true;
						OnStop(Obstacle, Radius);
					}
				}
				else
				{
					// Update position and time stamp
					Obstacle.LastPosition = Position;
					Obstacle.LastMovedTimeStamp = Now;

					// If the obstacle had stopped, signal the move.
					if (Obstacle.bHasStopped)
					{
						Obstacle.bHasStopped = false;
						OnMove(Obstacle);
					}
				}
			}
		});
}

#undef UNSAFE_FOR_MT
