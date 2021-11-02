// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovementProcessor.h"
#include "AIHelpers.h"
#include "NavigationSystem.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "MassCommonFragments.h"
#include "MassAIMovementFragments.h"
#include "MassMovementTypes.h"
#include "VisualLogger/VisualLogger.h"

namespace UE::MassMovement
{
	struct FAvoidanceAgent
	{
		FVector Location;
		FVector Velocity;
		float Radius;

		FAvoidanceAgent(const FVector& InLocation, const FVector& InVelocity, const float InRadius = 1.f)
			: Location(InLocation), Velocity(InVelocity), Radius(InRadius)
		{
		}
	};

	float TimeToCollision(const FAvoidanceAgent& Agent, const FAvoidanceAgent& Obstacle)
	{
		const float RadiusSum = Agent.Radius + Obstacle.Radius;
		const FVector VecToObstacle = Obstacle.Location - Agent.Location;
		const float C = FVector::DotProduct(VecToObstacle, VecToObstacle) - RadiusSum * RadiusSum;

		if (C < 0.f) //agents are colliding
		{
			return 0.f;
		}
		const FVector VelocityDelta = Agent.Velocity - Obstacle.Velocity;
		const float A = FVector::DotProduct(VelocityDelta, VelocityDelta);
		const float B = FVector::DotProduct(VecToObstacle, VelocityDelta);
		const float Discriminator = B * B - A * C;
		if (Discriminator <= 0)
		{
			return FLT_MAX;
		}
		const float Tau = (B - FMath::Sqrt(Discriminator)) / A;
		return (Tau < 0) ? FLT_MAX : Tau;
	}

	namespace Tweakables
	{
		float AvoidDistanceCutOff = 500.f;
		float AvoidAgentRadius = 50.f;
		float AvoidMaxForce = 100.f;
		float AvoidTimeHorizon = 5.f;
	} // Tweakables

	FAutoConsoleVariableRef Vars[] = {
		FAutoConsoleVariableRef(TEXT("ai.mass.AvoidDistance"), Tweakables::AvoidDistanceCutOff, TEXT(""), ECVF_Default), FAutoConsoleVariableRef(TEXT("ai.mass.AvoidRadius"), Tweakables::AvoidAgentRadius, TEXT(""), ECVF_Default), FAutoConsoleVariableRef(TEXT("ai.mass.AvoidMaxForce"), Tweakables::AvoidMaxForce, TEXT(""), ECVF_Default), FAutoConsoleVariableRef(TEXT("ai.mass.AvoidTimeHorizon"), Tweakables::AvoidTimeHorizon, TEXT(""), ECVF_Default)
	};

 	static bool DebugIsSelected(const FMassEntityHandle Entity)
 	{
 #if WITH_MASSGAMEPLAY_DEBUG
 		return UE::Mass::Debug::IsDebuggingEntity(Entity);
 #else
 	return false;
 #endif // WITH_MASSGAMEPLAY_DEBUG
 	}

 	static void DebugDrawLine(const UObject* LogOwner, const UWorld* World, const FVector& Start, const FVector& End, const FColor& Color, const bool bPersistent = false)
 	{
 #if WITH_MASSGAMEPLAY_DEBUG
 		UE_VLOG_SEGMENT(LogOwner, LogNavigation, Log, Start, End, Color, TEXT(""));

 		if (World)
 		{
 			DrawDebugLine(World, Start, End, Color, bPersistent);
 		}
 #endif // WITH_MASSGAMEPLAY_DEBUG
 	}

 	static void DebugDrawArrow(const UObject* LogOwner, const UWorld* World, const FVector& Start, const FVector& End, const FColor& Color)
 	{
 #if WITH_MASSGAMEPLAY_DEBUG
 		UE_VLOG_ARROW(LogOwner, LogNavigation, Log, Start, End, Color, TEXT(""));

 		if (World)
 		{
 			DrawDebugDirectionalArrow(World, Start, End, /*arrow size = */20.f, Color);
 		}
 #endif // WITH_MASSGAMEPLAY_DEBUG
 	}

 	static void DebugDrawCylinder(const UObject* LogOwner, const UWorld* World, const FVector& Start, const FVector& End, const float Radius, const FColor& Color)
 	{
 #if WITH_MASSGAMEPLAY_DEBUG
 		UE_VLOG_CYLINDER(LogOwner, LogNavigation, Log, Start, End, Radius, Color, TEXT(""));

 		if (World)
 		{
 			DrawDebugCylinder(World, Start, End, Radius, /*segments = */16, Color);
 		}
 #endif // WITH_MASSGAMEPLAY_DEBUG
	}
} // namespace UE::Movement

UMassProcessor_Movement::UMassProcessor_Movement()
{
	bAutoRegisterWithProcessingPhases = false;
}

//----------------------------------------------------------------------//
// UMassProcessor_Movement 
//----------------------------------------------------------------------//
void UMassProcessor_Movement::ConfigureQueries()
{
	EntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassProcessor_Movement::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	QUICK_SCOPE_CYCLE_COUNTER(MovementProcessor_Run);

	const float TimeDelta = Context.GetDeltaTimeSeconds();

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this, TimeDelta](FMassExecutionContext& Context)
		{
			const int32 NumEntities = Context.GetNumEntities();
			const TArrayView<FDataFragment_Transform> LocationList = Context.GetMutableFragmentView<FDataFragment_Transform>();
			const TArrayView<FMassVelocityFragment> VelocityList = Context.GetMutableFragmentView<FMassVelocityFragment>();
			for (int32 i = 0; i < NumEntities; ++i)
			{
				LocationList[i].GetMutableTransform().AddToTranslation(VelocityList[i].Value * TimeDelta);

				if (const TOptional<float> Yaw = UE::AI::GetYawFromVector(VelocityList[i].Value))
				{
					FQuat Rotation(FVector::UpVector, Yaw.GetValue());
					LocationList[i].GetMutableTransform().SetRotation(Rotation);
				}
			}
		});
}

//----------------------------------------------------------------------//
//  UMassProcessor_AgentMovement
//----------------------------------------------------------------------//
UMassProcessor_AgentMovement::UMassProcessor_AgentMovement()
{
	bAutoRegisterWithProcessingPhases = false;
}

void UMassProcessor_AgentMovement::ConfigureQueries()
{
	Super::ConfigureQueries();
	EntityQuery.AddRequirement<FDataFragment_AgentRadius>(EMassFragmentAccess::ReadOnly);
}

//----------------------------------------------------------------------//
//  UMassFragmentDestructor_AvoidanceObstacleRemover
//----------------------------------------------------------------------//
UMassAvoidanceObstacleRemoverFragmentDestructor::UMassAvoidanceObstacleRemoverFragmentDestructor()
{
	FragmentType = FMassAvoidanceObstacleGridCellLocationFragment::StaticStruct();
	ExecutionFlags = (int32)(EProcessorExecutionFlags::All);
}

void UMassAvoidanceObstacleRemoverFragmentDestructor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassAvoidanceObstacleGridCellLocationFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassAvoidanceObstacleRemoverFragmentDestructor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	WeakMovementSubsystem = UWorld::GetSubsystem<UMassMovementSubsystem>(Owner.GetWorld());
}

void UMassAvoidanceObstacleRemoverFragmentDestructor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	UMassMovementSubsystem* MovementSubsystem = WeakMovementSubsystem.Get();
	if (!MovementSubsystem)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this, MovementSubsystem](FMassExecutionContext& Context)
		{
			const int32 NumEntities = Context.GetNumEntities();
			const TArrayView<FMassAvoidanceObstacleGridCellLocationFragment> AvoidanceObstacleCellLocationList = Context.GetMutableFragmentView<FMassAvoidanceObstacleGridCellLocationFragment>();

			for (int32 i = 0; i < NumEntities; ++i)
			{
				FMassAvoidanceObstacleItem ObstacleItem;
				ObstacleItem.Entity = Context.GetEntity(i);
				MovementSubsystem->GetGridMutable().Remove(ObstacleItem, AvoidanceObstacleCellLocationList[i].CellLoc);
			}
		});
}
