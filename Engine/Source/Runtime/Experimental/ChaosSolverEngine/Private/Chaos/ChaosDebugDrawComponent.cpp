// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosDebugDrawComponent.h"
#include "Chaos/DebugDrawQueue.h"
#include "ChaosLog.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "VisualLogger/VisualLogger.h"

int bChaosDebugDraw_DrawMode = 0;
FAutoConsoleVariableRef CVarArrowSize(TEXT("p.Chaos.DebugDrawMode"), bChaosDebugDraw_DrawMode, TEXT("Where to send debug draw commands. 0 = UE Debug Draw; 1 = VisLog; 2 = Both"));

#if CHAOS_DEBUG_DRAW
void DebugDrawChaos(AActor* DebugDrawActor)
{
	if (!DebugDrawActor)
	{
		return;
	}

	UWorld* World = DebugDrawActor->GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	using namespace Chaos;
	TArray<FLatentDrawCommand> LatenetDrawCommands;

	bool bDrawUe = bChaosDebugDraw_DrawMode != 1;
	bool bDrawVisLog = bChaosDebugDraw_DrawMode != 0;

	FDebugDrawQueue::GetInstance().ExtractAllElements(LatenetDrawCommands, !World->IsPaused());
	for (const FLatentDrawCommand& Command : LatenetDrawCommands)
	{
		AActor* Actor = (Command.TestBaseActor) ? Command.TestBaseActor : DebugDrawActor;

		switch (Command.Type)
		{
		case FLatentDrawCommand::EDrawType::Point:
		{
			if (bDrawUe)
			{
				DrawDebugPoint(World, Command.LineStart, Command.Thickness, Command.Color, Command.bPersistentLines, Command.LifeTime, Command.DepthPriority);
			}
			if (bDrawVisLog)
			{
				UE_VLOG_SEGMENT_THICK(Actor, LogChaos, Log, Command.LineStart, Command.LineStart, Command.Color, Command.Thickness, TEXT_EMPTY);
			}
			break;
		}
		case FLatentDrawCommand::EDrawType::Line:
		{
			if (bDrawUe)
			{
				DrawDebugLine(World, Command.LineStart, Command.LineEnd, Command.Color, Command.bPersistentLines, Command.LifeTime, Command.DepthPriority, Command.Thickness);
			}
			if (bDrawVisLog)
			{
				UE_VLOG_SEGMENT(Actor, LogChaos, Log, Command.LineStart, Command.LineEnd, Command.Color, TEXT_EMPTY);
			}
			break;
		}
		case FLatentDrawCommand::EDrawType::DirectionalArrow:
		{
			if (bDrawUe)
			{
				DrawDebugDirectionalArrow(World, Command.LineStart, Command.LineEnd, Command.ArrowSize, Command.Color, Command.bPersistentLines, Command.LifeTime, Command.DepthPriority, Command.Thickness);
			}
			if (bDrawVisLog)
			{
				UE_VLOG_SEGMENT(Actor, LogChaos, Log, Command.LineStart, Command.LineEnd, Command.Color, TEXT_EMPTY);
			}
			break;
		}
		case FLatentDrawCommand::EDrawType::Sphere:
		{
			if (bDrawUe)
			{
				DrawDebugSphere(World, Command.LineStart, Command.Radius, Command.Segments, Command.Color, Command.bPersistentLines, Command.LifeTime, Command.DepthPriority, Command.Thickness);
			}
			if (bDrawVisLog)
			{
				// VLOG Capsule uses the bottom end as the origin (though the variable is named Center)
				FVector Base = Command.LineStart - Command.Radius * FVector::UpVector;
				UE_VLOG_CAPSULE(Actor, LogChaos, Log, Base, Command.Radius + KINDA_SMALL_NUMBER, Command.Radius, FQuat::Identity, Command.Color, TEXT_EMPTY);
			}
			break;
		}
		case FLatentDrawCommand::EDrawType::Box:
		{
			if (bDrawUe)
			{
				DrawDebugBox(World, Command.Center, Command.Extent, Command.Rotation, Command.Color, Command.bPersistentLines, Command.LifeTime, Command.DepthPriority, Command.Thickness);
			}
			if (bDrawVisLog)
			{
				UE_VLOG_OBOX(Actor, LogChaos, Log, FBox(-Command.Extent, Command.Extent), FQuatRotationTranslationMatrix::Make(Command.Rotation, Command.Center), Command.Color, TEXT_EMPTY);
			}
			break;
		}
		case FLatentDrawCommand::EDrawType::String:
		{
			if (bDrawUe)
			{
				DrawDebugString(World, Command.TextLocation, Command.Text, Command.TestBaseActor, Command.Color, Command.LifeTime, Command.bDrawShadow, Command.FontScale);
			}
			if (bDrawVisLog)
			{
				UE_VLOG(Command.TestBaseActor, LogChaos, Log, TEXT("%s"), *Command.Text);
			}
			break;
		}
		case FLatentDrawCommand::EDrawType::Circle:
		{
			if (bDrawUe)
			{
				FMatrix M = FRotationMatrix::MakeFromYZ(Command.YAxis, Command.ZAxis);
				M.SetOrigin(Command.Center);
				DrawDebugCircle(World, M, Command.Radius, Command.Segments, Command.Color, Command.bPersistentLines, Command.LifeTime, Command.DepthPriority, Command.Thickness, Command.bDrawAxis);
			}
			//if (bDrawVisLog)
			//{
			//	UE_VLOG_CONE(Actor, LogChaos, Log, Command.Center, Command.ZAxis, 0.1f, PI, Command.Color, TEXT_EMPTY);
			//}
			break;
		}
		case FLatentDrawCommand::EDrawType::Capsule:
		{
			if (bDrawUe)
			{
				DrawDebugCapsule(World, Command.Center, Command.HalfHeight, Command.Radius, Command.Rotation, Command.Color, Command.bPersistentLines, Command.LifeTime, Command.DepthPriority, Command.Thickness);
			}
			if (bDrawVisLog)
			{
				// VLOG Capsule uses the bottom end as the origin (though the variable is named Center)
				FVector Base = Command.Center - Command.HalfHeight * (Command.Rotation * FVector::UpVector);
				UE_VLOG_CAPSULE(Actor, LogChaos, Log, Base, Command.HalfHeight, Command.Radius, Command.Rotation, Command.Color, TEXT_EMPTY);
			}
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
	DebugDrawChaos(GetOwner());
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
