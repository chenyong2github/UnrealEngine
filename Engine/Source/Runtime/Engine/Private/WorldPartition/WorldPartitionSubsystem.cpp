// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionSubsystem.h"
#include "SceneView.h"
#include "UnrealEngine.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionDebugHelper.h"
#include "WorldPartition/DataLayer/WorldDataLayersActorDesc.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/ContentBundle/ContentBundleWorldSubsystem.h"
#include "Engine/Canvas.h"
#include "Engine/CoreSettings.h"
#include "Engine/LevelBounds.h"
#include "Debug/DebugDrawService.h"
#include "WorldPartition/WorldPartitionLog.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldPartitionSubsystem)

extern int32 GBlockOnSlowStreaming;
static const FName NAME_WorldPartitionRuntimeHash("WorldPartitionRuntimeHash");

static int32 GDrawWorldPartitionIndex = 0;
static FAutoConsoleCommand CVarDrawWorldPartitionIndex(
	TEXT("wp.Runtime.DrawWorldPartitionIndex"),
	TEXT("Sets the index of the wanted world partition to display debug draw."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() == 1)
		{
			GDrawWorldPartitionIndex = FCString::Atoi(*Args[0]);
		}
	}));

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

UWorldPartition* UWorldPartitionSubsystem::GetWorldPartition()
{
	return GetWorld()->GetWorldPartition();
}

const UWorldPartition* UWorldPartitionSubsystem::GetWorldPartition() const
{
	return GetWorld()->GetWorldPartition();
}

#if WITH_EDITOR
void UWorldPartitionSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UWorldPartitionSubsystem* This = CastChecked<UWorldPartitionSubsystem>(InThis);

	This->ActorDescContainerInstanceManager.AddReferencedObjects(Collector);
}

FWorldPartitionActorFilter UWorldPartitionSubsystem::GetWorldPartitionActorFilter(const FString& InWorldPackage) const
{
	TSet<FString> VisitedPackages;
	return GetWorldPartitionActorFilterInternal(InWorldPackage, VisitedPackages);
}


FWorldPartitionActorFilter UWorldPartitionSubsystem::GetWorldPartitionActorFilterInternal(const FString& InWorldPackage, TSet<FString>& InOutVisitedPackages) const
{
	if (InOutVisitedPackages.Contains(InWorldPackage))
	{
		return FWorldPartitionActorFilter(InWorldPackage);
	}

	InOutVisitedPackages.Add(InWorldPackage);

	// Most of the time if this will return an existing Container but when loading a new LevelInstance (Content Browser Drag&Drop, Create LI) 
	// This will make sure Container exists.
	UActorDescContainer* LevelContainer = ActorDescContainerInstanceManager.RegisterContainer(*InWorldPackage, GetWorld());
	check(LevelContainer);
	ON_SCOPE_EXIT{ ActorDescContainerInstanceManager.UnregisterContainer(LevelContainer); };

	// Lazy create filter for now
	TArray<const FWorldPartitionActorDesc*> ContainerActorDescs;
	const FWorldDataLayersActorDesc* WorldDataLayersActorDesc = nullptr;

	for (FActorDescList::TConstIterator<> ActorDescIt(LevelContainer); ActorDescIt; ++ActorDescIt)
	{
		if (ActorDescIt->GetActorNativeClass()->IsChildOf<AWorldDataLayers>())
		{
			check(!WorldDataLayersActorDesc);
			WorldDataLayersActorDesc = static_cast<const FWorldDataLayersActorDesc*>(*ActorDescIt);
		}
		else if (ActorDescIt->IsContainerInstance())
		{
			ContainerActorDescs.Add(*ActorDescIt);
		}
	}

	FWorldPartitionActorFilter Filter(InWorldPackage);

	if (WorldDataLayersActorDesc)
	{
		for (const FDataLayerInstanceDesc& DataLayerInstanceDesc : WorldDataLayersActorDesc->GetDataLayerInstances())
		{
			// For now consider all DataLayerInstances using Assets as filters that are included by default
			if (DataLayerInstanceDesc.SupportsActorFilters())
			{
				Filter.DataLayerFilters.Add(FSoftObjectPath(DataLayerInstanceDesc.GetAssetPath().ToString()), FWorldPartitionActorFilter::FDataLayerFilter(DataLayerInstanceDesc.GetShortName(), DataLayerInstanceDesc.IsIncludedInActorFilterDefault()));
			}
		}
	}

	for (const FWorldPartitionActorDesc* ContainerActorDesc : ContainerActorDescs)
	{
		TSet<FString> VisitedPackagesCopy(InOutVisitedPackages);

		// Get World Default Filter
		FWorldPartitionActorFilter* ChildFilter = new FWorldPartitionActorFilter(GetWorldPartitionActorFilterInternal(ContainerActorDesc->GetLevelPackage().ToString(), VisitedPackagesCopy));
		ChildFilter->DisplayName = ContainerActorDesc->GetActorLabelOrName().ToString();

		// Apply Filter to Default
		if (const FWorldPartitionActorFilter* ContainerFilter = ContainerActorDesc->GetContainerFilter())
		{
			ChildFilter->Override(*ContainerFilter);
		}

		Filter.AddChildFilter(ContainerActorDesc->GetGuid(), ChildFilter);
	}

	return Filter;
}

TMap<FActorContainerID, TSet<FGuid>> UWorldPartitionSubsystem::GetFilteredActorsPerContainer(const FActorContainerID& InContainerID, const FString& InWorldPackage, const FWorldPartitionActorFilter& InActorFilter)
{
	TMap<FActorContainerID, TSet<FGuid>> FilteredActors;

	FWorldPartitionActorFilter ContainerFilter = GetWorldPartitionActorFilter(InWorldPackage);
	ContainerFilter.Override(InActorFilter);

	// Flatten Filter to FActorContainerID map
	TMap<FActorContainerID, TMap<FSoftObjectPath, FWorldPartitionActorFilter::FDataLayerFilter>> DataLayerFiltersPerContainer;
	TFunction<void(const FActorContainerID&, const FWorldPartitionActorFilter&)> ProcessFilter = [&DataLayerFiltersPerContainer, &ProcessFilter](const FActorContainerID& InContainerID, const FWorldPartitionActorFilter& InContainerFilter)
	{
		check(!DataLayerFiltersPerContainer.Contains(InContainerID));
		TMap<FSoftObjectPath,FWorldPartitionActorFilter::FDataLayerFilter>& DataLayerFilters = DataLayerFiltersPerContainer.Add(InContainerID);
		
		for (auto& [AssetPath, DataLayerFilter] : InContainerFilter.DataLayerFilters)
		{
			DataLayerFilters.Add(AssetPath, DataLayerFilter);
		}

		for (auto& [ActorGuid, WorldPartitionActorFilter] : InContainerFilter.GetChildFilters())
		{
			ProcessFilter(FActorContainerID(InContainerID, ActorGuid), *WorldPartitionActorFilter);
		}
	};

	ProcessFilter(InContainerID, ContainerFilter);

	TFunction<void(const FActorContainerID&, const UActorDescContainer*)> ProcessContainers = [&DataLayerFiltersPerContainer, &FilteredActors, &ProcessContainers](const FActorContainerID& InContainerID, const UActorDescContainer* InContainer)
	{
		const TMap<FSoftObjectPath, FWorldPartitionActorFilter::FDataLayerFilter>& DataLayerFilters = DataLayerFiltersPerContainer.FindChecked(InContainerID);
		for (FActorDescList::TConstIterator<> ActorDescIt(InContainer); ActorDescIt; ++ActorDescIt)
		{
			if (ActorDescIt->GetDataLayers().Num() > 0 && ActorDescIt->IsUsingDataLayerAsset())
			{
				bool bExcluded = false;
				for (FName DataLayerName : ActorDescIt->GetDataLayers())
				{
					FSoftObjectPath DataLayerAsset(DataLayerName.ToString());
					if (const FWorldPartitionActorFilter::FDataLayerFilter* DataLayerFilter = DataLayerFilters.Find(DataLayerAsset))
					{
						if (DataLayerFilter->bIncluded)
						{
							bExcluded = false;
							break;
						}
						else
						{
							bExcluded = true;
						}
					}
				}

				if (bExcluded)
				{
					FilteredActors.FindOrAdd(InContainerID).Add(ActorDescIt->GetGuid());
				}
			}

			if (ActorDescIt->IsContainerInstance())
			{
				FWorldPartitionActorDesc::FContainerInstance ChildContainerInstance;
				if (ActorDescIt->GetContainerInstance(FWorldPartitionActorDesc::FGetContainerInstanceParams(), ChildContainerInstance))
				{
					ProcessContainers(FActorContainerID(InContainerID, ActorDescIt->GetGuid()), ChildContainerInstance.Container);
				}
			}
		}
	};

	UActorDescContainer* Container = RegisterContainer(*InWorldPackage);
	ProcessContainers(InContainerID, Container);
	UnregisterContainer(Container);

	return FilteredActors;
}


bool UWorldPartitionSubsystem::IsRunningConvertWorldPartitionCommandlet()
{
	static UClass* WorldPartitionConvertCommandletClass = FindObject<UClass>(nullptr, TEXT("/Script/UnrealEd.WorldPartitionConvertCommandlet"), true);
	check(WorldPartitionConvertCommandletClass);
	return GetRunningCommandletClass() && GetRunningCommandletClass()->IsChildOf(WorldPartitionConvertCommandletClass);
}

void UWorldPartitionSubsystem::FActorDescContainerInstanceManager::FActorDescContainerInstance::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Container);
}

void UWorldPartitionSubsystem::FActorDescContainerInstanceManager::FActorDescContainerInstance::UpdateBounds()
{
	Bounds.Init();
	for (FActorDescList::TIterator<> ActorDescIt(Container); ActorDescIt; ++ActorDescIt)
	{
		if (ActorDescIt->GetActorNativeClass()->IsChildOf<ALevelBounds>())
		{
			continue;
		}
		Bounds += ActorDescIt->GetRuntimeBounds();
	}
}

void UWorldPartitionSubsystem::FActorDescContainerInstanceManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (auto& [Name, ContainerInstance] : ActorDescContainers)
	{
		ContainerInstance.AddReferencedObjects(Collector);
	}
}

UActorDescContainer* UWorldPartitionSubsystem::FActorDescContainerInstanceManager::RegisterContainer(FName PackageName, UWorld* InWorld)
{
	FActorDescContainerInstance* ExistingContainerInstance = &ActorDescContainers.FindOrAdd(PackageName);
	UActorDescContainer* ActorDescContainer = ExistingContainerInstance->Container;

	if (ExistingContainerInstance->RefCount++ == 0)
	{
		ActorDescContainer = NewObject<UActorDescContainer>(GetTransientPackage());
		ExistingContainerInstance->Container = ActorDescContainer;

		// This will potentially invalidate ExistingContainerInstance due to ActorDescContainers reallocation
		ActorDescContainer->Initialize({ InWorld, PackageName });

		ExistingContainerInstance = &ActorDescContainers.FindChecked(PackageName);
		ExistingContainerInstance->UpdateBounds();
	}

	check(ActorDescContainer->IsTemplateContainer());
	return ActorDescContainer;
}

void UWorldPartitionSubsystem::FActorDescContainerInstanceManager::UnregisterContainer(UActorDescContainer* Container)
{
	FName PackageName = Container->GetContainerPackage();
	FActorDescContainerInstance& ExistingContainerInstance = ActorDescContainers.FindChecked(PackageName);

	if (--ExistingContainerInstance.RefCount == 0)
	{
		ExistingContainerInstance.Container->Uninitialize();
		ActorDescContainers.FindAndRemoveChecked(PackageName);
	}
}

FBox UWorldPartitionSubsystem::FActorDescContainerInstanceManager::GetContainerBounds(FName PackageName) const
{
	if (const FActorDescContainerInstance* ActorDescContainerInstance = ActorDescContainers.Find(PackageName))
	{
		return ActorDescContainerInstance->Bounds;
	}
	return FBox(ForceInit);
}

void UWorldPartitionSubsystem::FActorDescContainerInstanceManager::UpdateContainerBounds(FName PackageName)
{
	if (FActorDescContainerInstance* ActorDescContainerInstance = ActorDescContainers.Find(PackageName))
	{
		ActorDescContainerInstance->UpdateBounds();
	}
}
#endif

void UWorldPartitionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if WITH_EDITOR
	bIsRunningConvertWorldPartitionCommandlet = IsRunningConvertWorldPartitionCommandlet();
	if(bIsRunningConvertWorldPartitionCommandlet)
	{
		return;
	}
#endif

	GetWorld()->OnWorldPartitionInitialized().AddUObject(this, &UWorldPartitionSubsystem::OnWorldPartitionInitialized);
	GetWorld()->OnWorldPartitionUninitialized().AddUObject(this, &UWorldPartitionSubsystem::OnWorldPartitionUninitialized);
}

void UWorldPartitionSubsystem::Deinitialize()
{
#if WITH_EDITOR
	if (bIsRunningConvertWorldPartitionCommandlet)
	{
		Super::Deinitialize();
		return;
	}
#endif 

	GetWorld()->OnWorldPartitionInitialized().RemoveAll(this);
	GetWorld()->OnWorldPartitionUninitialized().RemoveAll(this);

	// At this point World Partition should be uninitialized
	check(!GetWorldPartition() || !GetWorldPartition()->IsInitialized());

	Super::Deinitialize();
}

// We allow creating UWorldPartitionSubsystem for inactive worlds as WorldPartition initialization is necessary 
// because DataLayerManager is required to be initialized when duplicating a partitioned world.
bool UWorldPartitionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return Super::DoesSupportWorldType(WorldType) || WorldType == EWorldType::Inactive || WorldType == EWorldType::EditorPreview;
}

void UWorldPartitionSubsystem::ForEachWorldPartition(TFunctionRef<bool(UWorldPartition*)> Func)
{
	for (UWorldPartition* WorldPartition : RegisteredWorldPartitions)
	{
		if (!Func(WorldPartition))
		{
			return;
		}
	}
}

void UWorldPartitionSubsystem::OnWorldPartitionInitialized(UWorldPartition* InWorldPartition)
{
	if (RegisteredWorldPartitions.IsEmpty())
	{
		DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &UWorldPartitionSubsystem::Draw));

		// Enforce some GC settings when using World Partition
		if (GetWorld()->IsGameWorld())
		{
			LevelStreamingContinuouslyIncrementalGCWhileLevelsPendingPurge = GLevelStreamingContinuouslyIncrementalGCWhileLevelsPendingPurge;
			LevelStreamingForceGCAfterLevelStreamedOut = GLevelStreamingForceGCAfterLevelStreamedOut;

			GLevelStreamingContinuouslyIncrementalGCWhileLevelsPendingPurge = GLevelStreamingContinuouslyIncrementalGCWhileLevelsPendingPurgeForWP;
			GLevelStreamingForceGCAfterLevelStreamedOut = 0;
		}
	}

	check(!RegisteredWorldPartitions.Contains(InWorldPartition));
	RegisteredWorldPartitions.Add(InWorldPartition);
}

void UWorldPartitionSubsystem::OnWorldPartitionUninitialized(UWorldPartition* InWorldPartition)
{
	check(RegisteredWorldPartitions.Contains(InWorldPartition));
	RegisteredWorldPartitions.Remove(InWorldPartition);

	if (RegisteredWorldPartitions.IsEmpty())
	{
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
	}
}

void UWorldPartitionSubsystem::RegisterStreamingSourceProvider(IWorldPartitionStreamingSourceProvider* StreamingSource)
{
	bool bIsAlreadyInSet = false;
	StreamingSourceProviders.Add(StreamingSource, &bIsAlreadyInSet);
	UE_CLOG(bIsAlreadyInSet, LogWorldPartition, Warning, TEXT("Streaming source provider already registered."));
}

bool UWorldPartitionSubsystem::IsStreamingSourceProviderRegistered(IWorldPartitionStreamingSourceProvider* StreamingSource) const
{
	return StreamingSourceProviders.Contains(StreamingSource);
}

bool UWorldPartitionSubsystem::UnregisterStreamingSourceProvider(IWorldPartitionStreamingSourceProvider* StreamingSource)
{
	return !!StreamingSourceProviders.Remove(StreamingSource);
}

TSet<IWorldPartitionStreamingSourceProvider*> UWorldPartitionSubsystem::GetStreamingSourceProviders() const
{
	TSet<IWorldPartitionStreamingSourceProvider*> Result = StreamingSourceProviders;
	if (!Result.IsEmpty() && IsStreamingSourceProviderFiltered.IsBound())
	{
		for (auto It = Result.CreateIterator(); It; ++It)
		{
			if (IsStreamingSourceProviderFiltered.Execute(*It))
			{
				It.RemoveCurrent();
			}
		}
	}
	return Result;
}

void UWorldPartitionSubsystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	for (UWorldPartition* RegisteredWorldPartition : RegisteredWorldPartitions)
	{
		RegisteredWorldPartition->Tick(DeltaSeconds);

		if (GDrawRuntimeHash3D && RegisteredWorldPartition->CanDebugDraw())
		{
			RegisteredWorldPartition->DrawRuntimeHash3D();
		}

#if WITH_EDITOR
		if (!GetWorld()->IsGameWorld())
		{
			RegisteredWorldPartition->DrawRuntimeHashPreview();
		}
#endif
	}
}

ETickableTickType UWorldPartitionSubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always;
}

TStatId UWorldPartitionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWorldPartitionSubsystem, STATGROUP_Tickables);
}

bool UWorldPartitionSubsystem::IsAllStreamingCompleted()
{
	return const_cast<UWorldPartitionSubsystem*>(this)->IsStreamingCompleted();
}

bool UWorldPartitionSubsystem::IsStreamingCompleted(const IWorldPartitionStreamingSourceProvider* InStreamingSourceProvider) const
{
	// Convert specified/optional streaming source provider to a world partition 
	// streaming source and pass it along to each registered world partition
	TArray<FWorldPartitionStreamingSource> StreamingSources;
	TArray<FWorldPartitionStreamingSource>* StreamingSourcesPtr = nullptr;
	if (InStreamingSourceProvider)
	{
		StreamingSourcesPtr = &StreamingSources;
		if (!InStreamingSourceProvider->GetStreamingSources(StreamingSources))
		{
			return true;
		}
	}

	for (UWorldPartition* RegisteredWorldPartition : RegisteredWorldPartitions)
	{
		if (!RegisteredWorldPartition->IsStreamingCompleted(StreamingSourcesPtr))
		{
			return false;
		}
	}
	return true;
}

bool UWorldPartitionSubsystem::IsStreamingCompleted(EWorldPartitionRuntimeCellState QueryState, const TArray<FWorldPartitionStreamingQuerySource>& QuerySources, bool bExactState) const
{
	for (UWorldPartition* RegisteredWorldPartition : RegisteredWorldPartitions)
	{
		if (!RegisteredWorldPartition->IsStreamingCompleted(QueryState, QuerySources, bExactState))
		{
			return false;
		}
	}

	return true;
}

void UWorldPartitionSubsystem::DumpStreamingSources(FOutputDevice& OutputDevice) const
{
	if (const UWorldPartition* WorldPartition = GetWorldPartition())
	{
		const TArray<FWorldPartitionStreamingSource>* StreamingSources = &WorldPartition->GetStreamingSources();
		if (StreamingSources && (StreamingSources->Num() > 0))
		{
			OutputDevice.Logf(TEXT("Streaming Sources:"));
			for (const FWorldPartitionStreamingSource& StreamingSource : *StreamingSources)
			{
				OutputDevice.Logf(TEXT("  - %s: %s"), *StreamingSource.Name.ToString(), *StreamingSource.ToString());
			}
		}
	}
}

void UWorldPartitionSubsystem::UpdateStreamingState()
{
	//make temp copy of array as UpdateStreamingState may FlushAsyncLoading, which may add a new world partition to RegisteredWorldPartitions while iterating
	const TArray<UWorldPartition*> RegisteredWorldPartitionsCopy = RegisteredWorldPartitions;
	for (UWorldPartition* RegisteredWorldParition : RegisteredWorldPartitionsCopy)
	{
		RegisteredWorldParition->UpdateStreamingState();
	}
}

void UWorldPartitionSubsystem::Draw(UCanvas* Canvas, class APlayerController* PC)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionSubsystem::Draw);
	if (!Canvas || !Canvas->SceneView || !RegisteredWorldPartitions.IsValidIndex(GDrawWorldPartitionIndex))
	{
		return;
	}

	UWorldPartition* WorldPartition = RegisteredWorldPartitions[GDrawWorldPartitionIndex];
	if (!WorldPartition->CanDebugDraw())
	{
		return;
	}

	// Filter out views that don't match our world
	if (!WorldPartition->GetWorld()->IsNetMode(NM_DedicatedServer) && !UWorldPartition::IsSimulating(false) &&
		(Canvas->SceneView->ViewActor == nullptr || Canvas->SceneView->ViewActor->GetWorld() != GetWorld()))
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

		FVector2D PartitionCanvasSize = FVector2D(CanvasMaxScreenSize.X, CanvasMaxScreenSize.Y);
		FVector2D UsedCanvasSize = FVector2D::ZeroVector;
		if (WorldPartition->DrawRuntimeHash2D(Canvas, PartitionCanvasSize, CurrentOffset, UsedCanvasSize))
		{
			CurrentOffset.X = CanvasBottomRightPadding.X;
			CurrentOffset.Y += UsedCanvasSize.Y;
		}
	}
	
	if (GDrawStreamingPerfs || GDrawRuntimeHash2D)
	{
		{
			FString StatusText;
			if (IsIncrementalPurgePending()) { StatusText += TEXT("(Purging) "); }
			if (IsIncrementalUnhashPending()) { StatusText += TEXT("(Unhashing) "); }
			if (IsAsyncLoading()) { StatusText += TEXT("(AsyncLoading) "); }
			if (StatusText.IsEmpty()) { StatusText = TEXT("(Idle) "); }

			FString DebugWorldText = FString::Printf(TEXT("(%s)"), *GetDebugStringForWorld(GetWorld()));
			if (WorldPartition->IsServer())
			{
				DebugWorldText += FString::Printf(TEXT(" (Server Streaming %s)"), WorldPartition->IsServerStreamingEnabled() ? TEXT("Enabled") : TEXT("Disabled"));
			}
			
			const FString Text = FString::Printf(TEXT("Streaming Status for %s: %s"), *DebugWorldText, *StatusText);
			FWorldPartitionDebugHelper::DrawText(Canvas, Text, GEngine->GetSmallFont(), FColor::White, CurrentOffset);
		}

		{
			FString StatusText;
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

		const TArray<FWorldPartitionStreamingSource>& StreamingSources = WorldPartition->GetStreamingSources();
		if (StreamingSources.Num() > 0)
		{
			FString Title(TEXT("Streaming Sources"));
			FWorldPartitionDebugHelper::DrawText(Canvas, Title, GEngine->GetSmallFont(), FColor::Yellow, CurrentOffset);

			FVector2D Pos = CurrentOffset;
			float MaxTextWidth = 0;
			for (const FWorldPartitionStreamingSource& StreamingSource : StreamingSources)
			{
				FString StreamingSourceDisplay = StreamingSource.Name.ToString();
				if (StreamingSource.bReplay)
				{
					StreamingSourceDisplay += TEXT(" (Replay)");
				}
				FWorldPartitionDebugHelper::DrawText(Canvas, StreamingSourceDisplay, GEngine->GetSmallFont(), StreamingSource.GetDebugColor(), Pos, &MaxTextWidth);
			}
			Pos = CurrentOffset + FVector2D(MaxTextWidth + 10, 0.f);
			for (const FWorldPartitionStreamingSource& StreamingSource : StreamingSources)
			{
				FWorldPartitionDebugHelper::DrawText(Canvas, *StreamingSource.ToString(), GEngine->GetSmallFont(), FColor::White, Pos);
			}
			CurrentOffset.Y = Pos.Y;
		}
	}

	if (GDrawLegends || GDrawRuntimeHash2D)
	{
		// Streaming Status Legend
		WorldPartition->DrawStreamingStatusLegend(Canvas, CurrentOffset);
	}

	if (GDrawDataLayers || GDrawDataLayersLoadTime || GDrawRuntimeHash2D)
	{
		if (UDataLayerManager* DataLayerManager = WorldPartition->GetDataLayerManager())
		{
			DataLayerManager->DrawDataLayersStatus(Canvas, CurrentOffset);
		}
	}

	UContentBundleManager* ContentBundleManager = GetWorld()->ContentBundleManager;
	if (ContentBundleManager && (FWorldPartitionDebugHelper::CanDrawContentBundles() && GDrawRuntimeHash2D))
	{
		ContentBundleManager->DrawContentBundlesStatus(GetWorld(), Canvas, CurrentOffset);
	}

	if (GDrawRuntimeCellsDetails)
	{
		WorldPartition->DrawRuntimeCellsDetails(Canvas, CurrentOffset);
	}
}
