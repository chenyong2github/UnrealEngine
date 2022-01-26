// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassZoneGraphMovementProcessors.h"
#include "MassZoneGraphMovementFragments.h"
#include "ZoneGraphSubsystem.h"
#include "MassAIMovementFragments.h"
#include "MassMovementSettings.h"
#include "MassSignalSubsystem.h"
#include "MassCommonFragments.h"
#include "ZoneGraphQuery.h"
#include "VisualLogger/VisualLogger.h"
#include "MassSimulationLOD.h"
#include "Engine/World.h"

#define UNSAFE_FOR_MT 1

#if WITH_MASSGAMEPLAY_DEBUG

namespace UE::MassMovement::Debug
{
	FColor MixColors(const FColor ColorA, const FColor ColorB)
	{
		const int32 R = ((int32)ColorA.R + (int32)ColorB.R) / 2;
		const int32 G = ((int32)ColorA.G + (int32)ColorB.G) / 2;
		const int32 B = ((int32)ColorA.B + (int32)ColorB.B) / 2;
		const int32 A = ((int32)ColorA.A + (int32)ColorB.A) / 2;
		return FColor((uint8)R, (uint8)G, (uint8)B, (uint8)A);
	}
}

#endif // WITH_MASSGAMEPLAY_DEBUG

namespace UE::MassMovement
{

	/*
	 * Calculates speed scale based on agent's forward direction and desired steering direction.
	 */
	static float CalcDirectionalSpeedScale(const FVector ForwardDirection, const FVector SteerDirection)
	{
		// @todo: make these configurable
		constexpr float ForwardSpeedScale = 1.0f;
		constexpr float BackwardSpeedScale = 0.25f;
		constexpr float SideSpeedScale = 0.5f;

		const FVector LeftDirection = FVector::CrossProduct(ForwardDirection, FVector::UpVector);
		const float DirX = FVector::DotProduct(LeftDirection, SteerDirection);
		const float DirY = FVector::DotProduct(ForwardDirection, SteerDirection);

		// Calculate intersection between a direction vector and ellipse, where A & B are the size of the ellipse.
		// The direction vector is starting from the center of the ellipse.
		constexpr float SideA = SideSpeedScale;
		const float SideB = DirY > 0.0f ? ForwardSpeedScale : BackwardSpeedScale;
		const float Disc = FMath::Square(SideA) * FMath::Square(DirY) + FMath::Square(SideB) * FMath::Square(DirX);
		const float Speed = (Disc > SMALL_NUMBER) ? (SideA * SideB / FMath::Sqrt(Disc)) : 0.0f;;

		return Speed;
	}

	/** Speed envelope when approaching a point. NormalizedDistance in range [0..1] */
	static float ArrivalSpeedEnvelope(const float NormalizedDistance)
	{
		return FMath::Sqrt(NormalizedDistance);
	}

} // UE::MassMovement


//----------------------------------------------------------------------//
//  UMassZoneGraphLocationInitializer
//----------------------------------------------------------------------//
UMassZoneGraphLocationInitializer::UMassZoneGraphLocationInitializer()
{
	FragmentType = FMassZoneGraphLaneLocationFragment::StaticStruct();
	ExecutionFlags = (int32)(EProcessorExecutionFlags::All);
}

void UMassZoneGraphLocationInitializer::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassMovementConfigFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite); // Make optional?
}

void UMassZoneGraphLocationInitializer::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(Owner.GetWorld());
	SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Owner.GetWorld());
}

void UMassZoneGraphLocationInitializer::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	const UMassMovementSettings* Settings = GetDefault<UMassMovementSettings>();
	if (!ZoneGraphSubsystem || !Settings || !SignalSubsystem)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this, Settings](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();
		const TArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationList = Context.GetMutableFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TArrayView<FMassMoveTargetFragment> MoveTargetList = Context.GetMutableFragmentView<FMassMoveTargetFragment>();
		const TConstArrayView<FMassMovementConfigFragment> MovementConfigList = Context.GetFragmentView<FMassMovementConfigFragment>();
		const TConstArrayView<FDataFragment_Transform> TransformList = Context.GetFragmentView<FDataFragment_Transform>();

		// Get the default movement config.
		FMassMovementConfigHandle CurrentConfigHandle;
		const FMassMovementConfig* CurrentMovementConfig = nullptr;

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			const FDataFragment_Transform& Transform = TransformList[EntityIndex];
			const FVector& AgentLocation = Transform.GetTransform().GetLocation();
			const FMassMovementConfigFragment& MovementConfig = MovementConfigList[EntityIndex];
			FMassMoveTargetFragment& MoveTarget = MoveTargetList[EntityIndex];
			FMassZoneGraphLaneLocationFragment& LaneLocation = LaneLocationList[EntityIndex];

			if (MovementConfig.ConfigHandle != CurrentConfigHandle)
			{
				CurrentMovementConfig = Settings->GetMovementConfigByHandle(MovementConfig.ConfigHandle);
				CurrentConfigHandle = MovementConfig.ConfigHandle;
			}
			if (!CurrentMovementConfig)
			{
#if WITH_MASSGAMEPLAY_DEBUG && UNSAFE_FOR_MT
				const FMassEntityHandle Entity = Context.GetEntity(EntityIndex);
				UE_VLOG(this, LogMassNavigation, Warning, TEXT("Entity [%s] Invalid movement config."), *Entity.DebugGetDescription());
#endif
				continue;
			}

			const FVector QuerySize(CurrentMovementConfig->QueryRadius);
			const FBox QueryBounds(AgentLocation - QuerySize, AgentLocation + QuerySize);
			
			FZoneGraphLaneLocation NearestLane;
			float NearestLaneDistSqr = 0;
			
			if (ZoneGraphSubsystem->FindNearestLane(QueryBounds, CurrentMovementConfig->LaneFilter, NearestLane, NearestLaneDistSqr))
			{
				const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem->GetZoneGraphStorage(NearestLane.LaneHandle.DataHandle);
				check(ZoneGraphStorage); // Assume valid storage since we just got result.

				LaneLocation.LaneHandle = NearestLane.LaneHandle;
				LaneLocation.DistanceAlongLane = NearestLane.DistanceAlongLane;
				UE::ZoneGraph::Query::GetLaneLength(*ZoneGraphStorage, LaneLocation.LaneHandle, LaneLocation.LaneLength);
				
				MoveTarget.Center = AgentLocation;
				MoveTarget.Forward = NearestLane.Tangent;
				MoveTarget.DistanceToGoal = 0.0f;
				MoveTarget.SlackRadius = 0.0f;
			}
			else
			{
				LaneLocation.LaneHandle.Reset();
				LaneLocation.DistanceAlongLane = 0.0f;
				LaneLocation.LaneLength = 0.0f;

				MoveTarget.Center = AgentLocation;
				MoveTarget.Forward = FVector::ForwardVector;
				MoveTarget.DistanceToGoal = 0.0f;
				MoveTarget.SlackRadius = 0.0f;
			}
		}
	});
}


//----------------------------------------------------------------------//
//  UMassZoneGraphPathFollowProcessor
//----------------------------------------------------------------------//
UMassZoneGraphPathFollowProcessor::UMassZoneGraphPathFollowProcessor()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Tasks;
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Avoidance);
}

void UMassZoneGraphPathFollowProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(Owner.GetWorld());
	SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Owner.GetWorld());
}

void UMassZoneGraphPathFollowProcessor::ConfigureQueries()
{
	EntityQuery_Conditional.AddRequirement<FMassZoneGraphShortPathFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.AddRequirement<FMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.AddRequirement<FMassSimulationLODFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery_Conditional.AddRequirement<FMassSimulationVariableTickFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);

	EntityQuery_Conditional.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery_Conditional.SetChunkFilter(&FMassSimulationVariableTickChunkFragment::ShouldTickChunkThisFrame);
}

void UMassZoneGraphPathFollowProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	if (!SignalSubsystem || !ZoneGraphSubsystem)
	{
		return;
	}
	
	TArray<FMassEntityHandle> EntitiesToSignalPathDone;
	TArray<FMassEntityHandle> EntitiesToSignalLaneChanged;

	EntityQuery_Conditional.ForEachEntityChunk(EntitySubsystem, Context, [this, &EntitiesToSignalPathDone, &EntitiesToSignalLaneChanged](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();
		const TArrayView<FMassZoneGraphShortPathFragment> ShortPathList = Context.GetMutableFragmentView<FMassZoneGraphShortPathFragment>();
		const TArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationList = Context.GetMutableFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TArrayView<FMassMoveTargetFragment> MoveTargetList = Context.GetMutableFragmentView<FMassMoveTargetFragment>();
		const TConstArrayView<FMassSimulationLODFragment> SimLODList = Context.GetFragmentView<FMassSimulationLODFragment>();
		const bool bHasLOD = (SimLODList.Num() > 0);
		const TConstArrayView<FMassSimulationVariableTickFragment> SimVariableTickList = Context.GetFragmentView<FMassSimulationVariableTickFragment>();
		const bool bHasVariableTick = (SimVariableTickList.Num() > 0);
		const float WorldDeltaTime = Context.GetDeltaTimeSeconds();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			FMassZoneGraphShortPathFragment& ShortPath = ShortPathList[EntityIndex];
			FMassZoneGraphLaneLocationFragment& LaneLocation = LaneLocationList[EntityIndex];
			FMassMoveTargetFragment& MoveTarget = MoveTargetList[EntityIndex];
			const FMassEntityHandle Entity = Context.GetEntity(EntityIndex);
			const float DeltaTime = bHasVariableTick ? SimVariableTickList[EntityIndex].DeltaTime : WorldDeltaTime;

			bool bDisplayDebug = false;
#if WITH_MASSGAMEPLAY_DEBUG && UNSAFE_FOR_MT // this will result in bDisplayDebug == false and disabling of all the vlogs below
			bDisplayDebug = UE::Mass::Debug::IsDebuggingEntity(Entity);
			if (bDisplayDebug)
			{
				UE_VLOG(this, LogMassNavigation, Log, TEXT("Entity [%s] Updating path following"), *Entity.DebugGetDescription());
			}
#endif // WITH_MASSGAMEPLAY_DEBUG

			// Must have at least two points to interpolate.
			if (MoveTarget.GetCurrentAction() == EMassMovementAction::Move && ShortPath.NumPoints >= 2)
			{
				const bool bWasDone = ShortPath.IsDone();

				// Note: this should be in sync with the logic in apply velocity.
				const bool bHasSteering = (bHasLOD == false) || (SimLODList[EntityIndex].LOD != EMassLOD::Off);

				if (!bHasSteering || !MoveTarget.bSteeringFallingBehind)
				{
					// Update progress
					ShortPath.ProgressDistance += MoveTarget.DesiredSpeed.Get() * DeltaTime;
				}

				// @todo MassMovement: Ideally we would carry over any left over distance to the next path, especially when dealing with larger timesteps.
				// @todo MassMovement: Feedback current movement progress back to ShortPath.DesiredSpeed.

				if (!bWasDone)
				{
					const uint8 LastPointIndex = ShortPath.NumPoints - 1;
#if WITH_MASSGAMEPLAY_DEBUG
					ensureMsgf(LaneLocation.LaneHandle == ShortPath.DebugLaneHandle, TEXT("Short path lane should match current lane location."));
#endif // WITH_MASSGAMEPLAY_DEBUG

					if (ShortPath.ProgressDistance <= 0.0f)
					{
						// Requested time before the start of the path
						LaneLocation.DistanceAlongLane = ShortPath.Points[0].DistanceAlongLane.Get();
						
						MoveTarget.Center = ShortPath.Points[0].Position;
						MoveTarget.Forward = ShortPath.Points[0].Tangent.GetVector();
						MoveTarget.DistanceToGoal = ShortPath.Points[LastPointIndex].Distance.Get();
						MoveTarget.bOffBoundaries = ShortPath.Points[0].bOffLane;

						UE_CVLOG(bDisplayDebug,this, LogMassNavigation, Verbose, TEXT("Entity [%s] before start of lane %s at distance %.1f. Distance to goal: %.1f. Off Boundaries: %s"),
							*Entity.DebugGetDescription(),
							*LaneLocation.LaneHandle.ToString(),
							LaneLocation.DistanceAlongLane,
							MoveTarget.DistanceToGoal,
							*LexToString((bool)MoveTarget.bOffBoundaries));
					}
					else if (ShortPath.ProgressDistance <= ShortPath.Points[LastPointIndex].Distance.Get())
					{
						// Requested time along the path, interpolate.
						uint8 PointIndex = 0;
						while (PointIndex < (ShortPath.NumPoints - 2))
						{
							const FMassZoneGraphPathPoint& NextPoint = ShortPath.Points[PointIndex + 1];
							if (ShortPath.ProgressDistance <= NextPoint.Distance.Get())
							{
								break;
							}
							PointIndex++;
						}

						const FMassZoneGraphPathPoint& CurrPoint = ShortPath.Points[PointIndex];
						const FMassZoneGraphPathPoint& NextPoint = ShortPath.Points[PointIndex + 1];
						const float T = (ShortPath.ProgressDistance - CurrPoint.Distance.Get()) / (NextPoint.Distance.Get() - CurrPoint.Distance.Get());
						
						LaneLocation.DistanceAlongLane = FMath::Min(FMath::Lerp(CurrPoint.DistanceAlongLane.Get(), NextPoint.DistanceAlongLane.Get(), T), LaneLocation.LaneLength);

						MoveTarget.Center = FMath::Lerp(CurrPoint.Position, NextPoint.Position, T);
						MoveTarget.Forward = FMath::Lerp(CurrPoint.Tangent.GetVector(), NextPoint.Tangent.GetVector(), T).GetSafeNormal();
						MoveTarget.DistanceToGoal = ShortPath.Points[LastPointIndex].Distance.Get() - FMath::Lerp(CurrPoint.Distance.Get(), NextPoint.Distance.Get(), T);
						MoveTarget.bOffBoundaries = CurrPoint.bOffLane || NextPoint.bOffLane;

						UE_CVLOG(bDisplayDebug, this, LogMassNavigation, Verbose, TEXT("Entity [%s] along lane %s at distance %.1f. Distance to goal: %.1f. Off Boundaries: %s"),
							*Entity.DebugGetDescription(),
							*LaneLocation.LaneHandle.ToString(),
							LaneLocation.DistanceAlongLane,
							MoveTarget.DistanceToGoal,
							*LexToString((bool)MoveTarget.bOffBoundaries));
					}
					else
					{
						// Requested time after the end of the path, clamp to lane length in case quantization overshoots.
						LaneLocation.DistanceAlongLane = FMath::Min(ShortPath.Points[LastPointIndex].DistanceAlongLane.Get(), LaneLocation.LaneLength);

						MoveTarget.Center = ShortPath.Points[LastPointIndex].Position;
						MoveTarget.Forward = ShortPath.Points[LastPointIndex].Tangent.GetVector();
						MoveTarget.DistanceToGoal = 0.0f;
						MoveTarget.bOffBoundaries = ShortPath.Points[LastPointIndex].bOffLane;

						UE_CVLOG(bDisplayDebug, this, LogMassNavigation, Log, TEXT("Entity [%s] Finished path follow on lane %s at distance %f. Off Boundaries: %s"),
							*Entity.DebugGetDescription(), *LaneLocation.LaneHandle.ToString(), LaneLocation.DistanceAlongLane, *LexToString((bool)MoveTarget.bOffBoundaries));

						if (bDisplayDebug)
						{
							UE_VLOG(this, LogMassNavigation, Log, TEXT("Entity [%s] End of path."), *Entity.DebugGetDescription());
						}

						// Check to see if need advance to next lane.
						if (ShortPath.NextLaneHandle.IsValid())
						{
							const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem->GetZoneGraphStorage(LaneLocation.LaneHandle.DataHandle);
							if (ZoneGraphStorage != nullptr)
							{
								if (ShortPath.NextExitLinkType == EZoneLaneLinkType::Outgoing)
								{
									float NewLaneLength = 0.f;
									UE::ZoneGraph::Query::GetLaneLength(*ZoneGraphStorage, ShortPath.NextLaneHandle, NewLaneLength);

									UE_CVLOG(bDisplayDebug, this, LogMassNavigation, Log, TEXT("Entity [%s] Switching to OUTGOING lane %s -> %s, new distance %f."),
										*Entity.DebugGetDescription(), *LaneLocation.LaneHandle.ToString(), *ShortPath.NextLaneHandle.ToString(), 0.f);

									// update lane location
									LaneLocation.LaneHandle = ShortPath.NextLaneHandle;
									LaneLocation.LaneLength = NewLaneLength;
									LaneLocation.DistanceAlongLane = 0.0f;
								}
								else if (ShortPath.NextExitLinkType == EZoneLaneLinkType::Incoming)
								{
									float NewLaneLength = 0.f;
									UE::ZoneGraph::Query::GetLaneLength(*ZoneGraphStorage, ShortPath.NextLaneHandle, NewLaneLength);

									UE_CVLOG(bDisplayDebug, this, LogMassNavigation, Log, TEXT("Entity [%s] Switching to INCOMING lane %s -> %s, new distance %f."),
										*Entity.DebugGetDescription(), *LaneLocation.LaneHandle.ToString(), *ShortPath.NextLaneHandle.ToString(), NewLaneLength);

									// update lane location
									LaneLocation.LaneHandle = ShortPath.NextLaneHandle;
									LaneLocation.LaneLength = NewLaneLength;
									LaneLocation.DistanceAlongLane = NewLaneLength;
								}
								else if (ShortPath.NextExitLinkType == EZoneLaneLinkType::Adjacent)
								{
									FZoneGraphLaneLocation NewLocation;
									float DistanceSqr;
									if (UE::ZoneGraph::Query::FindNearestLocationOnLane(*ZoneGraphStorage, ShortPath.NextLaneHandle, MoveTarget.Center, MAX_flt, NewLocation, DistanceSqr))
									{
										float NewLaneLength = 0.f;
										UE::ZoneGraph::Query::GetLaneLength(*ZoneGraphStorage, ShortPath.NextLaneHandle, NewLaneLength);

										UE_CVLOG(bDisplayDebug, this, LogMassNavigation, Log, TEXT("Entity [%s] Switching to ADJACENT lane %s -> %s, new distance %f."),
											*Entity.DebugGetDescription(), *LaneLocation.LaneHandle.ToString(), *ShortPath.NextLaneHandle.ToString(), NewLocation.DistanceAlongLane);

										// update lane location
										LaneLocation.LaneHandle = ShortPath.NextLaneHandle;
										LaneLocation.LaneLength = NewLaneLength;
										LaneLocation.DistanceAlongLane = NewLocation.DistanceAlongLane;

										MoveTarget.Forward = NewLocation.Tangent;
									}
									else
									{
										UE_CVLOG(bDisplayDebug, this, LogMassNavigation, Error, TEXT("Entity [%s] Failed to switch to ADJACENT lane %s -> %s."),
											*Entity.DebugGetDescription(), *LaneLocation.LaneHandle.ToString(), *ShortPath.NextLaneHandle.ToString());
									}
								}
								else
								{
									ensureMsgf(false, TEXT("Unhandle NextExitLinkType type %s"), *UEnum::GetValueAsString(ShortPath.NextExitLinkType));
								}

								// Signal lane changed.
								EntitiesToSignalLaneChanged.Add(Entity);
							}
							else
							{
								UE_CVLOG(bDisplayDebug, this, LogMassNavigation, Error, TEXT("Entity [%s] Could not find ZoneGraph storage for lane %s."),
									*Entity.DebugGetDescription(), *LaneLocation.LaneHandle.ToString());
							}
						}
						else
						{
							UE_CVLOG(bDisplayDebug, this, LogMassNavigation, Log, TEXT("Entity [%s] Next lane not defined."), *Entity.DebugGetDescription());
						}

						ShortPath.bDone = true;
					}
				}

				const bool bIsDone = ShortPath.IsDone();

				// Signal path done.
				if (!bWasDone && bIsDone)
				{
					EntitiesToSignalPathDone.Add(Entity);
				}

#if WITH_MASSGAMEPLAY_DEBUG && UNSAFE_FOR_MT
				if (bDisplayDebug)
				{
					const FColor EntityColor = UE::Mass::Debug::GetEntityDebugColor(Entity);

					const FVector ZOffset(0,0,25);
					const FColor LightEntityColor = UE::MassMovement::Debug::MixColors(EntityColor, FColor::White);
					
					for (uint8 PointIndex = 0; PointIndex < ShortPath.NumPoints - 1; PointIndex++)
					{
						const FMassZoneGraphPathPoint& CurrPoint = ShortPath.Points[PointIndex];
						const FMassZoneGraphPathPoint& NextPoint = ShortPath.Points[PointIndex + 1];

						// Path
						UE_VLOG_SEGMENT_THICK(this, LogMassNavigation, Display, CurrPoint.Position + ZOffset, NextPoint.Position + ZOffset, EntityColor, /*Thickness*/3, TEXT(""));
					}
					
					for (uint8 PointIndex = 0; PointIndex < ShortPath.NumPoints; PointIndex++)
					{
						const FMassZoneGraphPathPoint& CurrPoint = ShortPath.Points[PointIndex];
						const FVector CurrBase = CurrPoint.Position + ZOffset;
						// Lane tangents
						UE_VLOG_SEGMENT_THICK(this, LogMassNavigation, Display, CurrBase, CurrBase + CurrPoint.Tangent.GetVector() * 100.0f, LightEntityColor, /*Thickness*/1, TEXT(""));
					}

					if (ShortPath.NumPoints > 0 && ShortPath.NextLaneHandle.IsValid())
					{
						const FMassZoneGraphPathPoint& LastPoint = ShortPath.Points[ShortPath.NumPoints - 1];
						const FVector CurrBase = LastPoint.Position + ZOffset;
						UE_VLOG_SEGMENT_THICK(this, LogMassNavigation, Display, CurrBase, CurrBase + FVector(0,0,100), FColor::Red, /*Thickness*/3, TEXT("Next: %s"), *ShortPath.NextLaneHandle.ToString());
					}
				}
#endif // WITH_MASSGAMEPLAY_DEBUG
			}
		}
	});

	if (EntitiesToSignalPathDone.Num())
	{
		SignalSubsystem->SignalEntities(UE::Mass::Signals::FollowPointPathDone, EntitiesToSignalPathDone);
	}
	if (EntitiesToSignalLaneChanged.Num())
	{
		SignalSubsystem->SignalEntities(UE::Mass::Signals::CurrentLaneChanged, EntitiesToSignalLaneChanged);
	}
}


//----------------------------------------------------------------------//
//  UMassZoneGraphSteeringProcessor
//----------------------------------------------------------------------//
UMassZoneGraphSteeringProcessor::UMassZoneGraphSteeringProcessor()
{
	ExecutionFlags = int32(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Tasks);
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Avoidance);
}

void UMassZoneGraphSteeringProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Owner.GetWorld());
}

void UMassZoneGraphSteeringProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassMovementConfigFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassSteeringFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassSteeringGhostFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);

	// No need for Off LOD to do steering, applying move target directly
	EntityQuery.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::None);
}

void UMassZoneGraphSteeringProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	const UMassMovementSettings* Settings = GetDefault<UMassMovementSettings>();
	check(Settings);

	if (!SignalSubsystem)
	{
		return;
	}
	
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this, Settings](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();
		const TArrayView<FMassMoveTargetFragment> MoveTargetList = Context.GetMutableFragmentView<FMassMoveTargetFragment>();
		const TConstArrayView<FMassMovementConfigFragment> MovementConfigList = Context.GetFragmentView<FMassMovementConfigFragment>();
		const TConstArrayView<FMassVelocityFragment> VelocityList = Context.GetFragmentView<FMassVelocityFragment>();
		const TArrayView<FDataFragment_Transform> TransformList = Context.GetMutableFragmentView<FDataFragment_Transform>();
		const TArrayView<FMassSteeringFragment> SteeringList = Context.GetMutableFragmentView<FMassSteeringFragment>();
		const TArrayView<FMassSteeringGhostFragment> GhostList = Context.GetMutableFragmentView<FMassSteeringGhostFragment>();

		// @todo: make configurable
		constexpr float StandDeadZoneRadius = 5.0f;
		constexpr float SteeringReactionTime = 0.2f;
		constexpr float SteerK = 1.f / SteeringReactionTime;
		const float DeltaTime = Context.GetDeltaTimeSeconds();

		// Get the default movement config.
		FMassMovementConfigHandle CurrentConfigHandle;
		const FMassMovementConfig* CurrentMovementConfig = nullptr;

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			FDataFragment_Transform& TransformFragment = TransformList[EntityIndex];
			FMassSteeringFragment& Steering = SteeringList[EntityIndex];
			FMassSteeringGhostFragment& Ghost = GhostList[EntityIndex];
			FMassMoveTargetFragment& MoveTarget = MoveTargetList[EntityIndex];
			const FMassMovementConfigFragment& MovementConfig = MovementConfigList[EntityIndex];
			const FMassVelocityFragment& Velocity = VelocityList[EntityIndex];
			const FMassEntityHandle Entity = Context.GetEntity(EntityIndex);

			if (MovementConfig.ConfigHandle != CurrentConfigHandle)
			{
				CurrentMovementConfig = Settings->GetMovementConfigByHandle(MovementConfig.ConfigHandle);
				CurrentConfigHandle = MovementConfig.ConfigHandle;
			}
			if (!CurrentMovementConfig)
			{
#if WITH_MASSGAMEPLAY_DEBUG && UNSAFE_FOR_MT
				UE_VLOG(this, LogMassNavigation, Warning, TEXT("Entity [%s] Invalid movement config."), *Entity.DebugGetDescription());
#endif
				continue;
			}

			FTransform& Transform = TransformFragment.GetMutableTransform();;

			// Calculate velocity for steering.
			const FVector CurrentLocation = Transform.GetLocation();
			const FVector CurrentForward = Transform.GetRotation().GetForwardVector();

			const float LookAheadDistance = FMath::Max(KINDA_SMALL_NUMBER, CurrentMovementConfig->Steering.LookAheadDistance);

			if (MoveTarget.GetCurrentAction() == EMassMovementAction::Move)
			{
				// Tune down avoidance and speed when arriving at goal.
				float ArrivalFade = 1.0f;
				if (MoveTarget.IntentAtGoal == EMassMovementAction::Stand)
				{
					ArrivalFade = FMath::Clamp(MoveTarget.DistanceToGoal / LookAheadDistance, 0.0f, 1.0f);
				}
				const float SteeringPredictionDistance = LookAheadDistance * ArrivalFade;

				// Steer towards and along the move target.
				const FVector TargetSide = FVector::CrossProduct(MoveTarget.Forward, FVector::UpVector);
				const FVector Delta = CurrentLocation - MoveTarget.Center;

				const float ForwardOffset = FVector::DotProduct(MoveTarget.Forward, Delta);

				// Calculate steering direction. When far away from the line defined by TargetPosition and TargetTangent,
				// the steering direction is towards the line, the close we get, the more it aligns with the line.
				const float SidewaysOffset = FVector::DotProduct(TargetSide, Delta);
				const float SteerForward = FMath::Sqrt(FMath::Max(0.0f, FMath::Square(SteeringPredictionDistance) - FMath::Square(SidewaysOffset)));

				// The Max() here makes the steering directions behind the TargetPosition to steer towards it directly.
				FVector SteerTarget = MoveTarget.Center + MoveTarget.Forward * FMath::Clamp(ForwardOffset + SteerForward, 0.0f, SteeringPredictionDistance);

				FVector SteerDirection = SteerTarget - CurrentLocation;
				SteerDirection.Z = 0.0f;
				const float DistanceToSteerTarget = SteerDirection.Length();
				if (DistanceToSteerTarget > KINDA_SMALL_NUMBER)
				{
					SteerDirection *= 1.0f / DistanceToSteerTarget;
				}
				
				const float DirSpeedScale = UE::MassMovement::CalcDirectionalSpeedScale(CurrentForward, SteerDirection);
				float DesiredSpeed = MoveTarget.DesiredSpeed.Get() * DirSpeedScale;

				// Control speed based relation to the forward axis of the move target.
				float CatchupDesiredSpeed = DesiredSpeed;
				if (ForwardOffset < 0.0f)
				{
					// Falling behind, catch up
					const float T = FMath::Min(-ForwardOffset / LookAheadDistance, 1.0f);
					CatchupDesiredSpeed = FMath::Lerp(DesiredSpeed, CurrentMovementConfig->MaximumSpeed, T);
				}
				else if (ForwardOffset > 0.0f)
				{
					// Ahead, slow down.
					const float T = FMath::Min(ForwardOffset / LookAheadDistance, 1.0f);
					CatchupDesiredSpeed = FMath::Lerp(DesiredSpeed, DesiredSpeed * 0.0f, 1.0f - FMath::Square(1.0f - T));
				}

				// Control speed based on distance to move target. This allows to catch up even if speed above reaches zero.
				const float DeviantSpeed = FMath::Min(FMath::Abs(SidewaysOffset) / LookAheadDistance, 1.0f) * DesiredSpeed;

				DesiredSpeed = FMath::Max(CatchupDesiredSpeed, DeviantSpeed);

				// Slow down towards the end of path.
				if (MoveTarget.IntentAtGoal == EMassMovementAction::Stand)
				{
					const float NormalizedDistanceToSteerTarget = FMath::Clamp(DistanceToSteerTarget / LookAheadDistance, 0.0f, 1.0f);
					DesiredSpeed *= UE::MassMovement::ArrivalSpeedEnvelope(FMath::Max(ArrivalFade, NormalizedDistanceToSteerTarget));
				}

				// @todo: This current completely overrides steering, we probably should have one processor that resets the steering at the beginning of the frame.
				Steering.DesiredVelocity = SteerDirection * DesiredSpeed;
				Steering.SteeringForce = SteerK * (Steering.DesiredVelocity - Velocity.Value); // Goal force

				MoveTarget.bSteeringFallingBehind = ForwardOffset < -LookAheadDistance * 0.8f;
			}
			else if (MoveTarget.GetCurrentAction() == EMassMovementAction::Stand)
			{
				constexpr float TargetMoveThresholdBase = 15.0f; // How much the target should deviate from the ghost location before update.
				constexpr float TargetSpeedHysteresisScale = 0.85f; // How much the max speed can drop before we stop tracking it.
				constexpr float TargetSelectionCooldown = 2.0f;	// Time between updates, varied randomly.

				// "Randomize" target move threshold so that different agents react a bit differently.
				const float PerEntityScale = (float)(Entity.Index & 8) / 7.0f;
				const float TargetMoveThreshold = TargetMoveThresholdBase * (0.9f + PerEntityScale * 0.2f);
				
				if (Ghost.LastSeenActionID != MoveTarget.GetCurrentActionID())
				{
					// Reset when action changes. @todo: should reset only when move->stand?
					Ghost.Location = MoveTarget.Center;
					Ghost.Velocity = FVector::ZeroVector;
					Ghost.LastSeenActionID = MoveTarget.GetCurrentActionID();

					Ghost.SteerTarget = MoveTarget.Center;
					Ghost.TargetMaxSpeed = 0.0f;
					Ghost.bUpdatingTarget = false;
					Ghost.TargetCooldown = FMath::RandRange(TargetSelectionCooldown * 0.25f, TargetSelectionCooldown);
					Ghost.bEnteredFromMoveAction = MoveTarget.GetPreviousAction() == EMassMovementAction::Move;
				}

				Ghost.TargetCooldown = FMath::Max(0.0f, Ghost.TargetCooldown - DeltaTime);

				if (!Ghost.bUpdatingTarget)
				{
					// Update the move target if enough time has passed and the target has moved. 
					if (Ghost.TargetCooldown <= 0.0f && FVector::DistSquared(Ghost.SteerTarget, Ghost.Location) > FMath::Square(TargetMoveThreshold))
					{
						Ghost.SteerTarget = Ghost.Location;
						Ghost.TargetMaxSpeed = 0.0f;
						Ghost.bUpdatingTarget = true;
						Ghost.bEnteredFromMoveAction = false;
					}
				}
				else
				{
					// Updating target
					Ghost.SteerTarget = Ghost.Location;
					const float GhostSpeed = Ghost.Velocity.Length();
					if (GhostSpeed > (Ghost.TargetMaxSpeed * TargetSpeedHysteresisScale))
					{
						Ghost.TargetMaxSpeed = FMath::Max(Ghost.TargetMaxSpeed, GhostSpeed);
					}
					else
					{
						// Speed is dropping, we have found the peak change, stop updating the target and start cooldown.
						Ghost.TargetCooldown = FMath::RandRange(TargetSelectionCooldown * 0.5f, TargetSelectionCooldown);
						Ghost.bUpdatingTarget = false;
					}
				}
				
				// Move directly towards the move target when standing.
				FVector SteerDirection = FVector::ZeroVector;
				float DesiredSpeed = 0.0f;

				FVector Delta = Ghost.SteerTarget - CurrentLocation;
				Delta.Z = 0.0f;
				const float Distance = Delta.Size();
				if (Distance > StandDeadZoneRadius)
				{
					SteerDirection = Delta / Distance;
					if (Ghost.bEnteredFromMoveAction)
					{
						// If the current steering target is from approaching a move target, use the same speed logic as movement to ensure smooth transition.
						const float SpeedFade = FMath::Clamp((Distance - StandDeadZoneRadius) / FMath::Max(KINDA_SMALL_NUMBER, LookAheadDistance - StandDeadZoneRadius), 0.0f, 1.0f);
						DesiredSpeed = MoveTarget.DesiredSpeed.Get() * UE::MassMovement::CalcDirectionalSpeedScale(CurrentForward, SteerDirection) * UE::MassMovement::ArrivalSpeedEnvelope(SpeedFade);
					}
					else
					{
						// More aggressive movement when doing adjustments.
						constexpr float AdjustmentLookAheadDistance = 100.0f; // @todo: make configurable.
						// Intentionally not taking dead zone into account here, so that the speed does not drop to zero.
						const float SpeedFade = FMath::Clamp(Distance / FMath::Max(KINDA_SMALL_NUMBER, LookAheadDistance - AdjustmentLookAheadDistance), 0.0f, 1.0f);
						DesiredSpeed = MoveTarget.DesiredSpeed.Get() * UE::MassMovement::ArrivalSpeedEnvelope(SpeedFade);
					}
				}
				
				// @todo: This current completely overrides steering, we probably should have one processor that resets the steering at the beginning of the frame.
				Steering.DesiredVelocity = SteerDirection * DesiredSpeed;
				Steering.SteeringForce = SteerK * (Steering.DesiredVelocity - Velocity.Value); // Goal force
				
				MoveTarget.bSteeringFallingBehind = false;
			}
			else if (MoveTarget.GetCurrentAction() == EMassMovementAction::Animate)
			{
				// No steering when animating.
				Steering.Reset();
				MoveTarget.bSteeringFallingBehind = false;
			}

#if WITH_MASSGAMEPLAY_DEBUG && UNSAFE_FOR_MT
			FColor EntityColor = FColor::White;
			const bool bDisplayDebug = UE::Mass::Debug::IsDebuggingEntity(Entity, &EntityColor);
			if (bDisplayDebug)
			{
				const FVector ZOffset(0,0,25);

				const FColor DarkEntityColor = UE::MassMovement::Debug::MixColors(EntityColor, FColor::Black);
				const FColor LightEntityColor = UE::MassMovement::Debug::MixColors(EntityColor, FColor::White);
				
				const FVector MoveTargetCenter = MoveTarget.Center + ZOffset;

				// MoveTarget slack boundary
				UE_VLOG_CIRCLE_THICK(this, LogMassNavigation, Log, MoveTargetCenter, FVector::UpVector, CurrentMovementConfig->Steering.LookAheadDistance, EntityColor, /*Thickness*/2,
					TEXT("%s MoveTgt %s"), *Entity.DebugGetDescription(), *UEnum::GetDisplayValueAsText(MoveTarget.IntentAtGoal).ToString());

				// MoveTarget orientation
				UE_VLOG_SEGMENT_THICK(this, LogMassNavigation, Log, MoveTargetCenter, MoveTargetCenter + MoveTarget.Forward * CurrentMovementConfig->Steering.LookAheadDistance, EntityColor, /*Thickness*/2, TEXT(""));

				// MoveTarget - current location relation.
				if (FVector::Dist2D(CurrentLocation, MoveTarget.Center) > CurrentMovementConfig->Steering.LookAheadDistance * 1.5f)
				{
					UE_VLOG_SEGMENT_THICK(this, LogMassNavigation, Log, MoveTargetCenter, CurrentLocation + ZOffset, FColor::Red, /*Thickness*/1, TEXT("LOST"));
				}
				else
				{
					UE_VLOG_SEGMENT_THICK(this, LogMassNavigation, Log, MoveTargetCenter, CurrentLocation + ZOffset, DarkEntityColor, /*Thickness*/1, TEXT(""));
				}

				// Steering
				UE_VLOG_SEGMENT_THICK(this, LogMassNavigation, Log, CurrentLocation + ZOffset, CurrentLocation + Steering.DesiredVelocity + ZOffset, LightEntityColor, /*Thickness*/2,
					TEXT("%s Steer %.1f"), *Entity.DebugGetDescription(), Steering.DesiredVelocity.Length());
			}
#endif // WITH_MASSGAMEPLAY_DEBUG
			
		}
	});
}

#undef UNSAFE_FOR_MT
