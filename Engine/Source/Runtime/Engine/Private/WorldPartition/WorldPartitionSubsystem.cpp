// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionStreamingPolicy.h"
#include "WorldPartition/WorldPartitionDebugHelper.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Canvas.h"
#include "Engine/Console.h"
#include "Engine/CoreSettings.h"
#include "ConsoleSettings.h"
#include "Debug/DebugDrawService.h"

extern int32 GBlockOnSlowStreaming;
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

static int32 GDrawStreamingPerfs = 0;
static FAutoConsoleCommand CVarDrawStreamingPerfs(
	TEXT("wp.Runtime.ToggleDrawStreamingPerfs"),
	TEXT("Toggles debug display of world partition streaming perfs."),
	FConsoleCommandDelegate::CreateLambda([] { GDrawStreamingPerfs = !GDrawStreamingPerfs; }));

static int32 GDrawLegends = 0;
static FAutoConsoleCommand CVarGDrawLegends(
	TEXT("wp.Runtime.ToggleDrawLegends"),
	TEXT("Toggles debug display of world partition legends."),
	FConsoleCommandDelegate::CreateLambda([] { GDrawLegends = !GDrawLegends; }));

static int32 GDrawRuntimeCellsDetails = 0;
static FAutoConsoleCommand CVarDrawRuntimeCellsDetails(
	TEXT("wp.Runtime.ToggleDrawRuntimeCellsDetails"),
	TEXT("Toggles debug display of world partition runtime streaming cells."),
	FConsoleCommandDelegate::CreateLambda([] { GDrawRuntimeCellsDetails = !GDrawRuntimeCellsDetails; }));

static int32 GDrawDataLayers = 0;
static FAutoConsoleCommand CVarDrawDataLayers(
	TEXT("wp.Runtime.ToggleDrawDataLayers"),
	TEXT("Toggles debug display of active data layers."),
	FConsoleCommandDelegate::CreateLambda([] { GDrawDataLayers = !GDrawDataLayers; }));

int32 GDrawDataLayersLoadTime = 0;
static FAutoConsoleCommand CVarDrawDataLayersLoadTime(
	TEXT("wp.Runtime.ToggleDrawDataLayersLoadTime"),
	TEXT("Toggles debug display of active data layers load time."),
	FConsoleCommandDelegate::CreateLambda([] { GDrawDataLayersLoadTime = !GDrawDataLayersLoadTime; }));

int32 GLevelStreamingContinuouslyIncrementalGCWhileLevelsPendingPurgeForWP = 64;
static FAutoConsoleVariableRef CVarGLevelStreamingContinuouslyIncrementalGCWhileLevelsPendingPurgeForWP(
	TEXT("wp.Runtime.LevelStreamingContinuouslyIncrementalGCWhileLevelsPendingPurgeForWP"),
	GLevelStreamingContinuouslyIncrementalGCWhileLevelsPendingPurgeForWP,
	TEXT("Force a GC update when there's more than the number of specified pending purge levels."),
	ECVF_Default
);

static FAutoConsoleCommandWithOutputDevice GDumpStreamingSourcesCmd(
	TEXT("wp.DumpstreamingSources"),
	TEXT("Dumps active streaming sources to the log"),
	FConsoleCommandWithOutputDeviceDelegate::CreateStatic([](FOutputDevice& OutputDevice)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld())
			{
				if (const UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>())
				{				
					WorldPartitionSubsystem->DumpStreamingSources(OutputDevice);
				}
			}
		}
	})
);

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
		return WorldOuter->IsPartitionedWorld();
	}

	return false;
}

UWorldPartition* UWorldPartitionSubsystem::GetWorldPartition()
{
	return GetWorld()->GetWorldPartition();
}

const UWorldPartition* UWorldPartitionSubsystem::GetWorldPartition() const
{
	return GetWorld()->GetWorldPartition();
}

#if WITH_EDITOR
bool UWorldPartitionSubsystem::IsRunningConvertWorldPartitionCommandlet() const
{
	static UClass* WorldPartitionConvertCommandletClass = FindObject<UClass>(ANY_PACKAGE, TEXT("WorldPartitionConvertCommandlet"), true);
	check(WorldPartitionConvertCommandletClass);
	static const bool bIsRunningWorldPartitionConvertCommandlet = GetRunningCommandletClass() && GetRunningCommandletClass()->IsChildOf(WorldPartitionConvertCommandletClass);
	return bIsRunningWorldPartitionConvertCommandlet;
}
#endif

void UWorldPartitionSubsystem::PostInitialize()
{
	Super::PostInitialize();

#if WITH_EDITOR
	if (IsRunningConvertWorldPartitionCommandlet())
	{
		return;
	}
#endif

	if (UWorldPartition* WorldPartition = GetWorldPartition())
	{
		WorldPartition->Initialize(GetWorld(), FTransform::Identity);

		if (WorldPartition->CanDrawRuntimeHash() && (GetWorld()->GetNetMode() != NM_DedicatedServer))
		{
			DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &UWorldPartitionSubsystem::Draw));
		}

		// Enforce some GC settings when using World Partition
		if (GetWorld()->IsGameWorld())
		{
			LevelStreamingContinuouslyIncrementalGCWhileLevelsPendingPurge = GLevelStreamingContinuouslyIncrementalGCWhileLevelsPendingPurge;
			LevelStreamingForceGCAfterLevelStreamedOut = GLevelStreamingForceGCAfterLevelStreamedOut;

			GLevelStreamingContinuouslyIncrementalGCWhileLevelsPendingPurge = GLevelStreamingContinuouslyIncrementalGCWhileLevelsPendingPurgeForWP;
			GLevelStreamingForceGCAfterLevelStreamedOut = 0;
		}
	}
}

void UWorldPartitionSubsystem::Deinitialize()
{
#if WITH_EDITOR
	if (IsRunningConvertWorldPartitionCommandlet())
	{
		Super::Deinitialize();
		return;
	}
#endif 

	if (GetWorld()->IsGameWorld())
	{
		GLevelStreamingContinuouslyIncrementalGCWhileLevelsPendingPurge = LevelStreamingContinuouslyIncrementalGCWhileLevelsPendingPurge;
		GLevelStreamingForceGCAfterLevelStreamedOut = LevelStreamingForceGCAfterLevelStreamedOut;
	}

	if (DrawHandle.IsValid())
	{
		UDebugDrawService::Unregister(DrawHandle);
		DrawHandle.Reset();
	}

	// During garbage collection, the world partition object can be uninitialized before the subsystem due to unordered calls to BeginDestroy
	if (UWorldPartition* WorldPartition = GetWorldPartition(); WorldPartition && WorldPartition->IsInitialized())
	{
		// Uninitialize registered world partitions
		WorldPartition->Uninitialize();
	}

	Super::Deinitialize();
}

void UWorldPartitionSubsystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UWorldPartition* Partition = GetWorldPartition();
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

ETickableTickType UWorldPartitionSubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always;
}

TStatId UWorldPartitionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWorldPartitionSubsystem, STATGROUP_Tickables);
}

bool UWorldPartitionSubsystem::IsStreamingCompleted(EWorldPartitionRuntimeCellState QueryState, const TArray<FWorldPartitionStreamingQuerySource>& QuerySources, bool bExactState) const
{
	const UWorldPartition* Partition = GetWorldPartition();

	if (!Partition->IsStreamingCompleted(QueryState, QuerySources, bExactState))
	{
		return false;
	}

	return true;
}

void UWorldPartitionSubsystem::DumpStreamingSources(FOutputDevice& OutputDevice) const
{
	const UWorldPartition* WorldPartition = GetWorldPartition();
	const TArray<FWorldPartitionStreamingSource>* StreamingSources = WorldPartition ? &WorldPartition->GetStreamingSources() : nullptr;
	if (StreamingSources && (StreamingSources->Num() > 0))
	{
		OutputDevice.Logf(TEXT("Streaming Sources:"));
		for (const FWorldPartitionStreamingSource& StreamingSource : *StreamingSources)
		{
			OutputDevice.Logf(TEXT("  - %s: %s"), *StreamingSource.Name.ToString(), *StreamingSource.ToString());
		}
	}
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

	UWorldPartition* Partition = GetWorldPartition();
	Partition->UpdateStreamingState();
}

void UWorldPartitionSubsystem::Draw(UCanvas* Canvas, class APlayerController* PC)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionSubsystem::Draw);
	if (!Canvas || !Canvas->SceneView)
	{
		return;
	}

	// Filter out views that don't match our world
	if (Canvas->SceneView->ViewActor != nullptr && 
		Canvas->SceneView->ViewActor->GetWorld() != GetWorld())
	{
		return;
	}

	const FVector2D CanvasTopLeftPadding(10.f, 10.f);

	FVector2D CurrentOffset(CanvasTopLeftPadding);

	if (GDrawRuntimeHash2D)
	{
		const float MaxScreenRatio = 0.75f;
		const FVector2D CanvasBottomRightPadding(10.f, 10.f);
		const FVector2D CanvasMinimumSize(100.f, 100.f);
		const FVector2D CanvasMaxScreenSize = FVector2D::Max(MaxScreenRatio*FVector2D(Canvas->ClipX, Canvas->ClipY) - CanvasBottomRightPadding - CurrentOffset, CanvasMinimumSize);

		UWorldPartition* Partition = GetWorldPartition();
		FVector2D PartitionCanvasSize = FVector2D(CanvasMaxScreenSize.X, CanvasMaxScreenSize.Y);
		Partition->DrawRuntimeHash2D(Canvas, PartitionCanvasSize, CurrentOffset);
		CurrentOffset.X = CanvasBottomRightPadding.X;
	}
	
	if (GDrawStreamingPerfs || GDrawRuntimeHash2D)
	{
		{
			FString StatusText;
			if (IsIncrementalPurgePending()) { StatusText += TEXT("(Purging) "); }
			if (IsIncrementalUnhashPending()) { StatusText += TEXT("(Unhashing) "); }
			if (IsAsyncLoading()) { StatusText += TEXT("(AsyncLoading) "); }
			if (StatusText.IsEmpty()) { StatusText = TEXT("(Idle)"); }

			const ENetMode NetMode = GetWorld()->GetNetMode();
			if (NetMode == NM_ListenServer)
			{
				StatusText += TEXT("(ListenServer)");
			}
			
			const FString Text = FString::Printf(TEXT("Streaming Status: %s"), *StatusText);
			FWorldPartitionDebugHelper::DrawText(Canvas, Text, GEngine->GetSmallFont(), FColor::White, CurrentOffset);
		}

		{
			FString StatusText;
			const UWorldPartition* WorldPartition = GetWorldPartition();
			EWorldPartitionStreamingPerformance StreamingPerformance = WorldPartition->GetStreamingPerformance();
			switch (StreamingPerformance)
			{
			case EWorldPartitionStreamingPerformance::Good:
				StatusText = TEXT("Good");
				break;
			case EWorldPartitionStreamingPerformance::Slow:
				StatusText = TEXT("Slow");
				break;
			case EWorldPartitionStreamingPerformance::Critical:
				StatusText = TEXT("Critical");
				break;
			default:
				StatusText = TEXT("Unknown");
				break;
			}
			const FString Text = FString::Printf(TEXT("Streaming Performance: %s (Blocking %s)"), *StatusText, GBlockOnSlowStreaming ? TEXT("Enabled") : TEXT("Disabled"));
			FWorldPartitionDebugHelper::DrawText(Canvas, Text, GEngine->GetSmallFont(), FColor::White, CurrentOffset);
		}
	}

	if (GDrawStreamingSources || GDrawRuntimeHash2D)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionSubsystem::DrawStreamingSources);

		const UWorldPartition* WorldPartition = GetWorldPartition();
		const TArray<FWorldPartitionStreamingSource>* StreamingSources = WorldPartition ? &WorldPartition->GetStreamingSources() : nullptr;
		if (StreamingSources && (StreamingSources->Num() > 0))
		{
			FString Title(TEXT("Streaming Sources"));
			FWorldPartitionDebugHelper::DrawText(Canvas, Title, GEngine->GetSmallFont(), FColor::Yellow, CurrentOffset);

			FVector2D Pos = CurrentOffset;
			float MaxTextWidth = 0;
			for (const FWorldPartitionStreamingSource& StreamingSource : *StreamingSources)
			{
				FString StreamingSourceDisplay = StreamingSource.Name.ToString();
				FWorldPartitionDebugHelper::DrawText(Canvas, StreamingSourceDisplay, GEngine->GetSmallFont(), StreamingSource.GetDebugColor(), Pos, &MaxTextWidth);
			}
			Pos = CurrentOffset + FVector2D(MaxTextWidth + 10, 0.f);
			for (const FWorldPartitionStreamingSource& StreamingSource : *StreamingSources)
			{
				FWorldPartitionDebugHelper::DrawText(Canvas, *StreamingSource.ToString(), GEngine->GetSmallFont(), FColor::White, Pos);
			}
			CurrentOffset.Y = Pos.Y;
		}
	}

	if (UWorldPartition* WorldPartition = GetWorldPartition())
	{
		UDataLayerSubsystem* DataLayerSubsystem = WorldPartition->GetWorld()->GetSubsystem<UDataLayerSubsystem>();

		if (GDrawLegends || GDrawRuntimeHash2D)
		{
			// Streaming Status Legend
			WorldPartition->DrawStreamingStatusLegend(Canvas, CurrentOffset);
		}

		if (DataLayerSubsystem && (GDrawDataLayers || GDrawDataLayersLoadTime || GDrawRuntimeHash2D))
		{
			DataLayerSubsystem->DrawDataLayersStatus(Canvas, CurrentOffset);
		}
	}

	if (GDrawRuntimeCellsDetails)
	{
		if (UWorldPartition* Partition = GetWorldPartition())
		{
			Partition->DrawRuntimeCellsDetails(Canvas, CurrentOffset);
		}
	}
}