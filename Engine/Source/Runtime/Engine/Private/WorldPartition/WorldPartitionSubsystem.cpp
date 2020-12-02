// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionStreamingPolicy.h"
#include "Engine/Canvas.h"
#include "Engine/Console.h"
#include "ConsoleSettings.h"
#include "Debug/DebugDrawService.h"

static const FName NAME_WorldPartitionRuntimeHash("WorldPartitionRuntimeHash");

static int32 GDrawRuntimeHash3D = 0;
static FAutoConsoleCommand CVarDrawRuntimeHash3D(
	TEXT("wp.Runtime.ToggleDrawRuntimeHash3D"),
	TEXT("Toggles 3D debug display of world partition runtime hash."),
	FConsoleCommandDelegate::CreateLambda([] { GDrawRuntimeHash3D = !GDrawRuntimeHash3D; }));

static int32 GDrawRuntimeHash2D = 0;
static FAutoConsoleCommand CVarDrawRuntimeHash2D(
	TEXT("wp.Runtime.ToggleDrawRuntimeHash2D"),
	TEXT("Toggles 2D debug display of world partition runtime hash."),
	FConsoleCommandDelegate::CreateLambda([] { GDrawRuntimeHash2D = !GDrawRuntimeHash2D; }));

UWorldPartitionSubsystem::UWorldPartitionSubsystem()
{}

bool UWorldPartitionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	if (UWorld* WorldOuter = Cast<UWorld>(Outer))
	{
		return WorldOuter->GetWorldPartition() != nullptr;
	}

	return false;
}

UWorldPartition* UWorldPartitionSubsystem::GetMainWorldPartition()
{
	return GetWorld()->GetWorldPartition();
}

const UWorldPartition* UWorldPartitionSubsystem::GetMainWorldPartition() const
{
	return GetWorld()->GetWorldPartition();
}

void UWorldPartitionSubsystem::PostInitialize()
{
	Super::PostInitialize();

	if (UWorldPartition* MainPartition = GetMainWorldPartition())
	{
		MainPartition->Initialize(GetWorld(), FTransform::Identity);

		if (GetWorld()->IsGameWorld() && (GetWorld()->GetNetMode() != NM_DedicatedServer))
		{
			DrawRuntimeHash2DHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &UWorldPartitionSubsystem::DrawRuntimeHash2D));
		}
	}
}

void UWorldPartitionSubsystem::Deinitialize()
{
	if (DrawRuntimeHash2DHandle.IsValid())
	{
		UDebugDrawService::Unregister(DrawRuntimeHash2DHandle);
		DrawRuntimeHash2DHandle.Reset();
	}

	while (RegisteredWorldPartitions.Num() > 0)
	{
		// Uninitialize registered world partitions
		UWorldPartition* WorldPartition = RegisteredWorldPartitions.Last();
		check(WorldPartition->IsInitialized());
		WorldPartition->Uninitialize();
		// Make sure they are unregistered
		check(!RegisteredWorldPartitions.Contains(WorldPartition));
	}

	UWorldSubsystem::Deinitialize();
}

void UWorldPartitionSubsystem::RegisterWorldPartition(UWorldPartition* WorldPartition)
{
	if (ensure(!RegisteredWorldPartitions.Contains(WorldPartition)))
	{
		check(WorldPartition->IsInitialized());
		RegisteredWorldPartitions.Add(WorldPartition);
	}
}

void UWorldPartitionSubsystem::UnregisterWorldPartition(UWorldPartition* WorldPartition)
{
	if (ensure(RegisteredWorldPartitions.Contains(WorldPartition)))
	{
		RegisteredWorldPartitions.Remove(WorldPartition);
	}
}

void UWorldPartitionSubsystem::Tick(float DeltaSeconds)
{
	for (UWorldPartition* Partition : RegisteredWorldPartitions)
	{
		Partition->Tick(DeltaSeconds);
		
		if (GDrawRuntimeHash3D && GetWorld()->IsGameWorld())
		{
			Partition->DrawRuntimeHash3D();
		}
	}
}

bool UWorldPartitionSubsystem::IsTickableInEditor() const
{
	return true;
}

UWorld* UWorldPartitionSubsystem::GetTickableGameObjectWorld() const
{
	return GetWorld();
}

ETickableTickType UWorldPartitionSubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always;
}

TStatId UWorldPartitionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWorldPartitionSubsystem, STATGROUP_Tickables);
}

void UWorldPartitionSubsystem::UpdateStreamingState()
{
#if WITH_EDITOR
	// Do not update during transaction
	if (GUndo)
	{
		return;
	}
#endif

	for (UWorldPartition* Partition : RegisteredWorldPartitions)
	{
		Partition->UpdateStreamingState();
	}
}

void UWorldPartitionSubsystem::DrawRuntimeHash2D(UCanvas* Canvas, class APlayerController* PC)
{
	if (!GDrawRuntimeHash2D || !Canvas || !Canvas->SceneView)
	{
		return;
	}

	const FVector2D CanvasTopLeftPadding(10.f, 10.f);
	const FVector2D CanvasBottomRightPadding(10.f, 10.f);
	const FVector2D CanvasMinimumSize(100.f, 100.f);
	const FVector2D CanvasMaxScreenSize = FVector2D::Max(FVector2D(Canvas->ClipX, Canvas->ClipY) - CanvasBottomRightPadding - CanvasTopLeftPadding, CanvasMinimumSize);

	FVector2D TotalFootprint(ForceInitToZero);
	for (UWorldPartition* Partition : RegisteredWorldPartitions)
	{
		FVector2D Footprint = Partition->GetDrawRuntimeHash2DDesiredFootprint(CanvasMaxScreenSize);
		TotalFootprint.X += Footprint.X;
	}

	if (TotalFootprint.X > 0.f)
	{
		FVector2D PartitionCanvasOffset(CanvasTopLeftPadding);
		for (UWorldPartition* Partition : RegisteredWorldPartitions)
		{
			FVector2D Footprint = Partition->GetDrawRuntimeHash2DDesiredFootprint(CanvasMaxScreenSize);
			float FootprintRatio = Footprint.X / TotalFootprint.X;
			FVector2D PartitionCanvasSize = FVector2D(CanvasMaxScreenSize.X * FootprintRatio, CanvasMaxScreenSize.Y);
			Partition->DrawRuntimeHash2D(Canvas, PartitionCanvasOffset, PartitionCanvasSize);
			PartitionCanvasOffset.X += PartitionCanvasSize.X;
		}
	}
}

//
// Methods only used by Editor for non-game world
//

#if WITH_EDITOR
void UWorldPartitionSubsystem::ForEachIntersectingActorDesc(const FBox& Box, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Predicate) const
{
	if (const UWorldPartition* MainPartition = GetMainWorldPartition())
	{
		MainPartition->ForEachIntersectingActorDesc(Box, ActorClass, Predicate);
	}
}

void UWorldPartitionSubsystem::ForEachActorDesc(TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Predicate) const
{
	if (const UWorldPartition* MainPartition = GetMainWorldPartition())
	{
		MainPartition->ForEachActorDesc(ActorClass, Predicate);
	}
}
#endif // WITH_EDITOR