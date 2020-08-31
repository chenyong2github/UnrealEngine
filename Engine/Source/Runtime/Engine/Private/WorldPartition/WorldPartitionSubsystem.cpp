// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionStreamingPolicy.h"
#include "WorldPartition/WorldPartitionActorDescFactory.h"
#include "GameFramework/HUD.h"
#include "Engine/Canvas.h"
#include "Engine/Console.h"
#include "ConsoleSettings.h"

static const FName NAME_WorldPartitionRuntimeGrids("WorldPartitionRuntimeGrids");

namespace WorldPartitionSubsystemConsole
{
	static void PopulateAutoCompleteEntries(TArray<FAutoCompleteCommand>& AutoCompleteList)
	{
		const UConsoleSettings* ConsoleSettings = GetDefault<UConsoleSettings>();
		AutoCompleteList.AddDefaulted();

		FAutoCompleteCommand& AutoCompleteCommand = AutoCompleteList.Last();
		AutoCompleteCommand.Command = FString::Printf(TEXT("showdebug %s"), *NAME_WorldPartitionRuntimeGrids.ToString());
		AutoCompleteCommand.Desc = TEXT("Toggles display of world partition runtime grids");
		AutoCompleteCommand.Color = ConsoleSettings->AutoCompleteCommandColor;
	}
}

UWorldPartitionSubsystem::UWorldPartitionSubsystem()
{}

bool UWorldPartitionSubsystem::IsEnabled() const
{
	return GetMainWorldPartition() != nullptr;
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

	if (GetWorld()->IsGameWorld() && (GetWorld()->GetNetMode() != NM_DedicatedServer))
	{
		AHUD::OnShowDebugInfo.AddUObject(this, &UWorldPartitionSubsystem::OnShowDebugInfo);
		static bool bRegisteredToConsole = false;
		if (!bRegisteredToConsole)
		{
			UConsole::RegisterConsoleAutoCompleteEntries.AddStatic(&WorldPartitionSubsystemConsole::PopulateAutoCompleteEntries);
			bRegisteredToConsole = true;
		}
	}

	if (UWorldPartition* MainPartition = GetMainWorldPartition())
	{
		MainPartition->Initialize(GetWorld(), FTransform::Identity);
	}
}

void UWorldPartitionSubsystem::Deinitialize()
{
	AHUD::OnShowDebugInfo.RemoveAll(this);

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

void UWorldPartitionSubsystem::OnShowDebugInfo(class AHUD* HUD, UCanvas* Canvas, const class FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos)
{
	if (!Canvas || !HUD || !HUD->ShouldDisplayDebug(NAME_WorldPartitionRuntimeGrids))
	{
		return;
	}

	const FVector2D CanvasTopLeftPadding(10.f, YPos);
	const FVector2D CanvasBottomRightPadding(10.f, 10.f);
	const FVector2D CanvasMinimumSize(100.f, 100.f);
	const FVector2D CanvasMaxScreenSize = FVector2D::Max(FVector2D(Canvas->ClipX, Canvas->ClipY) - CanvasBottomRightPadding - CanvasTopLeftPadding, CanvasMinimumSize);

	FVector2D TotalFootprint(ForceInitToZero);
	for (UWorldPartition* Partition : RegisteredWorldPartitions)
	{
		FVector2D Footprint = Partition->GetShowDebugDesiredFootprint(CanvasMaxScreenSize);
		TotalFootprint.X += Footprint.X;
	}

	if (TotalFootprint.X > 0.f)
	{
		FVector2D PartitionCanvasOffset(CanvasTopLeftPadding);
		for (UWorldPartition* Partition : RegisteredWorldPartitions)
		{
			FVector2D Footprint = Partition->GetShowDebugDesiredFootprint(CanvasMaxScreenSize);
			float FootprintRatio = Footprint.X / TotalFootprint.X;
			FVector2D PartitionCanvasSize = FVector2D(CanvasMaxScreenSize.X * FootprintRatio, CanvasMaxScreenSize.Y);
			Partition->ShowDebugInfo(Canvas, PartitionCanvasOffset, PartitionCanvasSize);
			PartitionCanvasOffset.X += PartitionCanvasSize.X;
		}
	}
}

//
// Methods only used by Editor for non-game world
//

#if WITH_EDITOR
TArray<const FWorldPartitionActorDesc*> UWorldPartitionSubsystem::GetIntersectingActorDescs(const FBox& Box, TSubclassOf<AActor> ActorClass) const
{
	TArray<const FWorldPartitionActorDesc*> ActorDescs;
	if (const UWorldPartition* MainPartition = GetMainWorldPartition())
	{
		ActorDescs = MainPartition->GetIntersectingActorDescs(Box, ActorClass);
	}
	return MoveTemp(ActorDescs);
}

void UWorldPartitionSubsystem::UpdateActorDesc(AActor* Actor)
{
	if (UWorldPartition* MainPartition = GetMainWorldPartition())
	{
		MainPartition->UpdateActorDesc(Actor);
	}
}

void UWorldPartitionSubsystem::AddActor(AActor* Actor)
{
	if (UWorldPartition* MainPartition = GetMainWorldPartition())
	{
		MainPartition->AddActor(Actor);
	}
}

void UWorldPartitionSubsystem::RemoveActor(AActor* Actor)
{
	if (UWorldPartition* MainPartition = GetMainWorldPartition())
	{
		MainPartition->RemoveActor(Actor);
	}
}

void UWorldPartitionSubsystem::RegisterActorDescFactory(TSubclassOf<AActor> Class, FWorldPartitionActorDescFactory* Factory)
{
	if (IsEnabled())
	{
		UWorldPartition::RegisterActorDescFactory(Class, Factory);
	}
}

FBox UWorldPartitionSubsystem::GetWorldBounds()
{
	if (UWorldPartition* WorldPartition = GetMainWorldPartition())
	{
		return WorldPartition->GetWorldBounds();
	}

	return FBox(ForceInit);
}

#endif // WITH_EDITOR