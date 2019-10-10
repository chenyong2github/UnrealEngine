// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosDebugDrawComponent.h"
#include "Chaos/DebugDrawQueue.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

#if CHAOS_DEBUG_DRAW
void DebugDrawChaos(UWorld* World)
{
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	using namespace Chaos;
	TArray<FLatentDrawCommand> LatenetDrawCommands;

	FDebugDrawQueue::GetInstance().ExtractAllElements(LatenetDrawCommands, !World->IsPaused());
	for (const FLatentDrawCommand& Command : LatenetDrawCommands)
	{
		switch (Command.Type)
		{
		case FLatentDrawCommand::EDrawType::Point:
		{
			DrawDebugPoint(World, Command.LineStart, Command.Thickness, Command.Color, Command.bPersistentLines, Command.LifeTime, Command.DepthPriority);
			break;
		}
		case FLatentDrawCommand::EDrawType::Line:
		{
			DrawDebugLine(World, Command.LineStart, Command.LineEnd, Command.Color, Command.bPersistentLines, Command.LifeTime, Command.DepthPriority, Command.Thickness);
			break;
		}
		case FLatentDrawCommand::EDrawType::DirectionalArrow:
		{
			DrawDebugDirectionalArrow(World, Command.LineStart, Command.LineEnd, Command.ArrowSize, Command.Color, Command.bPersistentLines, Command.LifeTime, Command.DepthPriority, Command.Thickness);
			break;
		}
		case FLatentDrawCommand::EDrawType::Sphere:
		{
			DrawDebugSphere(World, Command.LineStart, Command.Radius, Command.Segments, Command.Color, Command.bPersistentLines, Command.LifeTime, Command.DepthPriority, Command.Thickness);
			break;
		}
		case FLatentDrawCommand::EDrawType::Box:
		{
			DrawDebugBox(World, Command.Center, Command.Extent, Command.Rotation, Command.Color, Command.bPersistentLines, Command.LifeTime, Command.DepthPriority, Command.Thickness);
			break;
		}
		case FLatentDrawCommand::EDrawType::String:
		{
			DrawDebugString(World, Command.TextLocation, Command.Text, Command.TestBaseActor, Command.Color, Command.LifeTime, Command.bDrawShadow, Command.FontScale);
			break;
		}
		case FLatentDrawCommand::EDrawType::Circle:
		{
			FMatrix M = FRotationMatrix::MakeFromYZ(Command.YAxis, Command.ZAxis);
			M.SetOrigin(Command.Center);
			DrawDebugCircle(World, M, Command.Radius, Command.Segments, Command.Color, Command.bPersistentLines, Command.LifeTime, Command.DepthPriority, Command.Thickness, Command.bDrawAxis);
			break;
		}
		case FLatentDrawCommand::EDrawType::Capsule:
		{
			DrawDebugCapsule(World, Command.Center, Command.HalfHeight, Command.Radius, Command.Rotation, Command.Color, Command.bPersistentLines, Command.LifeTime, Command.DepthPriority, Command.Thickness);
			break;
		}
		default:
			break;
		}
	}
}
#endif



UChaosDebugDrawComponent::UChaosDebugDrawComponent()
	: bInPlay(false)
{
	// We must tick after anything that uses Chaos Debug Draw and also after the Line Batcher Component
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.SetTickFunctionEnable(true);
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UChaosDebugDrawComponent::BeginDestroy()
{
	Super::BeginDestroy();

#if CHAOS_DEBUG_DRAW
	// Don't allow new commands to be enqueued when we are paused
	Chaos::FDebugDrawQueue::GetInstance().SetConsumerActive(this, false);
#endif
}

void UChaosDebugDrawComponent::BeginPlay()
{
	Super::BeginPlay();

	SetTickableWhenPaused(true);

	bInPlay = true;

#if CHAOS_DEBUG_DRAW
	Chaos::FDebugDrawQueue::GetInstance().SetConsumerActive(this, bInPlay);
#endif
}

void UChaosDebugDrawComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	SetTickableWhenPaused(false);

	bInPlay = false;

#if CHAOS_DEBUG_DRAW
	Chaos::FDebugDrawQueue::GetInstance().SetConsumerActive(this, bInPlay);
#endif
}

void UChaosDebugDrawComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if CHAOS_DEBUG_DRAW
	DebugDrawChaos(GetWorld());
#endif
}

void UChaosDebugDrawComponent::BindWorldDelegates()
{
#if CHAOS_DEBUG_DRAW
	FWorldDelegates::OnPostWorldInitialization.AddStatic(&HandlePostWorldInitialization);
#endif
}

void UChaosDebugDrawComponent::HandlePostWorldInitialization(UWorld* World, const UWorld::InitializationValues IVS)
{
#if CHAOS_DEBUG_DRAW
	CreateDebugDrawActor(World);
#endif
}

void UChaosDebugDrawComponent::CreateDebugDrawActor(UWorld* World)
{
#if CHAOS_DEBUG_DRAW
	static FName NAME_ChaosDebugDrawActor = TEXT("ChaosDebugDrawActor");

	FActorSpawnParameters Params;
	Params.Name = NAME_ChaosDebugDrawActor;
	Params.ObjectFlags = Params.ObjectFlags | RF_Transient;

	AActor* Actor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	
	UChaosDebugDrawComponent* Comp = NewObject<UChaosDebugDrawComponent>(Actor);
	Actor->AddInstanceComponent(Comp);
	Comp->RegisterComponent();
#endif
}
