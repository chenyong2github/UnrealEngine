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

static int32 GDrawStreamingSources = 0;
static FAutoConsoleCommand CVarDrawStreamingSources(
	TEXT("wp.Runtime.ToggleDrawStreamingSources"),
	TEXT("Toggles debug display of world partition streaming sources."),
	FConsoleCommandDelegate::CreateLambda([] { GDrawStreamingSources = !GDrawStreamingSources; }));

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

		if (MainPartition->CanDrawRuntimeHash() && (GetWorld()->GetNetMode() != NM_DedicatedServer))
		{
			DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &UWorldPartitionSubsystem::Draw));
		}
	}
}

void UWorldPartitionSubsystem::Deinitialize()
{
	if (DrawHandle.IsValid())
	{
		UDebugDrawService::Unregister(DrawHandle);
		DrawHandle.Reset();
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
		
		if (GDrawRuntimeHash3D && Partition->CanDrawRuntimeHash())
		{
			Partition->DrawRuntimeHash3D();
		}

#if WITH_EDITOR
		if (!GetWorld()->IsGameWorld())
		{
			Partition->DrawRuntimeHashPreview();
		}
#endif
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

void UWorldPartitionSubsystem::Draw(UCanvas* Canvas, class APlayerController* PC)
{
	if (!Canvas || !Canvas->SceneView)
	{
		return;
	}

	const FVector2D CanvasTopLeftPadding(10.f, 10.f);
	FVector2D Pos = CanvasTopLeftPadding;

	auto DrawText = [](UCanvas* Canvas, const FString& Text, UFont* Font, const FColor& Color, FVector2D& Pos)
	{
		float TextWidth, TextHeight;
		Canvas->StrLen(Font, Text, TextWidth, TextHeight);
		Canvas->SetDrawColor(Color);
		Canvas->DrawText(Font, Text, Pos.X, Pos.Y);
		Pos.Y += TextHeight + 1;
	};

	if (GDrawRuntimeHash2D)
	{
		const float MaxScreenRatio = 0.75f;
		const FVector2D CanvasBottomRightPadding(10.f, 10.f);
		const FVector2D CanvasMinimumSize(100.f, 100.f);
		const FVector2D CanvasMaxScreenSize = FVector2D::Max(MaxScreenRatio*FVector2D(Canvas->ClipX, Canvas->ClipY) - CanvasBottomRightPadding - CanvasTopLeftPadding, CanvasMinimumSize);

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

	if (GDrawStreamingSources)
	{
		if (const UWorldPartition* WorldPartition = GetMainWorldPartition())
		{
			const TArray<FWorldPartitionStreamingSource>& StreamingSources = WorldPartition->GetStreamingSources();

			FString Title(TEXT("Streaming Sources"));
			DrawText(Canvas, Title, GEngine->GetSmallFont(), FColor::Yellow, Pos);
			UFont* Font = GEngine->GetTinyFont();
			for (const FWorldPartitionStreamingSource& StreamingSource : StreamingSources)
			{
				const FString Text = FString::Printf(TEXT("%s %s %s"), *StreamingSource.Name.ToString(), *StreamingSource.Location.ToString(), *StreamingSource.Rotation.ToString());
				DrawText(Canvas, Text, Font, FColor::White, Pos);
			}
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