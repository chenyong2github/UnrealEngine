// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "ContentStreaming.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeStreamingProxy.h"
#include "LandscapeInfo.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "ActorPartition/ActorPartitionSubsystem.h"
#include "Engine/World.h"
#include "Math/IntRect.h"

static int32 GUseStreamingManagerForCameras = 1;
static FAutoConsoleVariableRef CVarUseStreamingManagerForCameras(
	TEXT("grass.UseStreamingManagerForCameras"),
	GUseStreamingManagerForCameras,
	TEXT("1: Use Streaming Manager; 0: Use ViewLocationsRenderedLastFrame"));

DECLARE_CYCLE_STAT(TEXT("LandscapeSubsystem Tick"), STAT_LandscapeSubsystemTick, STATGROUP_Landscape);

ULandscapeSubsystem::ULandscapeSubsystem()
{
}

ULandscapeSubsystem::~ULandscapeSubsystem()
{
}

void ULandscapeSubsystem::RegisterActor(ALandscapeProxy* Proxy)
{
	Proxies.AddUnique(Proxy);
}

void ULandscapeSubsystem::UnregisterActor(ALandscapeProxy* Proxy)
{
	Proxies.Remove(Proxy);
}

void ULandscapeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if WITH_EDITOR
	UWorldPartitionSubsystem* WorldPartitionSubsystem = Collection.InitializeDependency<UWorldPartitionSubsystem>();
	if (WorldPartitionSubsystem)
	{
		WorldPartitionSubsystem->RegisterActorDescFactory(ALandscapeProxy::StaticClass(), &LandscapeActorDescFactory);
	}
#endif

#if WITH_EDITOR
	GrassMapsBuilder = new FLandscapeGrassMapsBuilder(GetWorld());
#endif
}

void ULandscapeSubsystem::Deinitialize()
{
#if WITH_EDITOR
	if (GrassMapsBuilder)
	{
		delete GrassMapsBuilder;
	}
#endif
	Proxies.Empty();

	Super::Deinitialize();
}

TStatId ULandscapeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULandscapeSubsystem, STATGROUP_Tickables);
}

ETickableTickType ULandscapeSubsystem::GetTickableTickType() const
{
	return HasAnyFlags(RF_ClassDefaultObject) || !GetWorld() || GetWorld()->IsNetMode(NM_DedicatedServer) ? ETickableTickType::Never : ETickableTickType::Always;
}

void ULandscapeSubsystem::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_LandscapeSubsystemTick);
	TRACE_CPUPROFILER_EVENT_SCOPE(ULandscapeSubsystem::Tick);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Landscape);
	LLM_SCOPE(ELLMTag::Landscape);

	UWorld* World = GetWorld();

	static TArray<FVector> OldCameras;
	TArray<FVector>* Cameras = nullptr;
	if (GUseStreamingManagerForCameras == 0)
	{
		if (OldCameras.Num() || World->ViewLocationsRenderedLastFrame.Num())
		{
			Cameras = &OldCameras;
			// there is a bug here, which often leaves us with no cameras in the editor
			if (World->ViewLocationsRenderedLastFrame.Num())
			{
				check(IsInGameThread());
				Cameras = &World->ViewLocationsRenderedLastFrame;
				OldCameras = *Cameras;
			}
		}
	}
	else
	{
		int32 Num = IStreamingManager::Get().GetNumViews();
		if (Num)
		{
			OldCameras.Reset(Num);
			for (int32 Index = 0; Index < Num; Index++)
			{
				auto& ViewInfo = IStreamingManager::Get().GetViewInformation(Index);
				OldCameras.Add(ViewInfo.ViewOrigin);
			}
			Cameras = &OldCameras;
		}
	}

	int32 InOutNumComponentsCreated = 0;
	for (ALandscapeProxy* Proxy : Proxies)
	{
#if WITH_EDITOR
		if (GIsEditor)
		{
			if (ALandscape* Landscape = Cast<ALandscape>(Proxy))
			{
				Landscape->TickLayers(DeltaTime);
			}

			// editor-only
			if (!World->IsPlayInEditor())
			{
				Proxy->UpdateBakedTextures();
				Proxy->UpdatePhysicalMaterialTasks();
			}
		}
#endif
		if (Cameras && Proxy->ShouldTickGrass())
		{
			Proxy->TickGrass(*Cameras, InOutNumComponentsCreated);
		}
	}

#if WITH_EDITOR
	if (GIsEditor && !World->IsPlayInEditor())
	{
		LandscapePhysicalMaterial::GarbageCollectTasks();
	}
#endif
}

#if WITH_EDITOR
void ULandscapeSubsystem::BuildGrassMaps()
{
	GrassMapsBuilder->Build();
}

int32 ULandscapeSubsystem::GetOutdatedGrassMapCount()
{
	return GrassMapsBuilder->GetOutdatedGrassMapCount(/*bInForceUpdate*/false);
}

bool ULandscapeSubsystem::IsGridBased() const
{
	return UWorld::HasSubsystem<UWorldPartitionSubsystem>(GetWorld());
}

namespace LandscapeSubsystemUtils
{
	ALandscapeProxy* FindOrAddLandscapeProxy(UActorPartitionSubsystem* ActorPartitionSubsystem, ULandscapeInfo* LandscapeInfo, const UActorPartitionSubsystem::FCellCoord& CellCoord)
	{
		ALandscape* Landscape = LandscapeInfo->LandscapeActor.Get();
		check(Landscape);
							
		auto LandscapeProxyCreated = [CellCoord, Landscape](APartitionActor* PartitionActor)
		{
			const FIntPoint CellLocation(CellCoord.X * Landscape->GridSize, CellCoord.Y * Landscape->GridSize);

			ALandscapeProxy* LandscapeProxy = CastChecked<ALandscapeProxy>(PartitionActor);
			// copy shared properties to this new proxy
			LandscapeProxy->GetSharedProperties(Landscape);
			const FVector ProxyLocation = Landscape->GetActorLocation() + FVector(CellLocation.X * Landscape->GetActorRelativeScale3D().X, CellLocation.Y * Landscape->GetActorRelativeScale3D().Y, 0.0f);

			LandscapeProxy->CreateLandscapeInfo();
			LandscapeProxy->SetActorLocationAndRotation(ProxyLocation, Landscape->GetActorRotation());
			LandscapeProxy->LandscapeSectionOffset = FIntPoint(CellLocation.X, CellLocation.Y);
		};

		const bool bCreate = true;
		const bool bBoundsSearch = false;
		return Cast<ALandscapeProxy>(ActorPartitionSubsystem->GetActor(ALandscapeStreamingProxy::StaticClass(), CellCoord, bCreate, LandscapeInfo->LandscapeGuid, Landscape->GridSize, bBoundsSearch, LandscapeProxyCreated));
	}
}

void ULandscapeSubsystem::UpdateGrid(ULandscapeInfo* LandscapeInfo, uint32 GridSizeInComponents)
{
	if (!IsGridBased())
	{
		return;
	}

	check(LandscapeInfo);

	const uint32 GridSize = LandscapeInfo->GetGridSize(GridSizeInComponents);
	LandscapeInfo->LandscapeActor->Modify();
	LandscapeInfo->LandscapeActor->GridSize = GridSize;

	FIntRect Extent;
	LandscapeInfo->GetLandscapeExtent(Extent.Min.X, Extent.Min.Y, Extent.Max.X, Extent.Max.Y);
	const FBox Bounds(FVector(Extent.Min), FVector(Extent.Max));

	UActorPartitionSubsystem* ActorPartitionSubsystem = GetWorld()->GetSubsystem<UActorPartitionSubsystem>();

	TArray<ULandscapeComponent*> LandscapeComponents;
	LandscapeComponents.Reserve(LandscapeInfo->XYtoComponentMap.Num());
	LandscapeInfo->ForAllLandscapeComponents([&LandscapeComponents](ULandscapeComponent* LandscapeComponent)
	{
		LandscapeComponents.Add(LandscapeComponent);
	});

	FActorPartitionGridHelper::ForEachIntersectingCell(ALandscapeStreamingProxy::StaticClass(), Extent, GetWorld()->PersistentLevel, [ActorPartitionSubsystem, LandscapeInfo, GridSizeInComponents, &LandscapeComponents, GridSize](const UActorPartitionSubsystem::FCellCoord& CellCoord, const FIntRect& CellBounds)
	{		
		TArray<ULandscapeComponent*> ComponentsToMove;
		const int32 MaxComponents = (int32)(GridSizeInComponents * GridSizeInComponents);
		ComponentsToMove.Reserve(MaxComponents);
		for (int32 i = 0; i < LandscapeComponents.Num();)
		{
			ULandscapeComponent* LandscapeComponent = LandscapeComponents[i];
			if (CellBounds.Contains(LandscapeComponent->GetSectionBase()))
			{
				ComponentsToMove.Add(LandscapeComponent);
				LandscapeComponents.RemoveAtSwap(i);
			}
			else
			{
				i++;
			}
		}
		
		check(ComponentsToMove.Num() <= MaxComponents);
		if (ComponentsToMove.Num())
		{
			ALandscapeProxy* LandscapeProxy = LandscapeSubsystemUtils::FindOrAddLandscapeProxy(ActorPartitionSubsystem, LandscapeInfo, CellCoord);
			check(LandscapeProxy);
			LandscapeInfo->MoveComponentsToProxy(ComponentsToMove, LandscapeProxy);
		}

		return true;
	}, GridSize);
}

ALandscapeProxy* ULandscapeSubsystem::FindOrAddLandscapeProxy(ULandscapeInfo* LandscapeInfo, const FIntPoint& SectionBase)
{
	if (!IsGridBased())
	{
		return LandscapeInfo->GetCurrentLevelLandscapeProxy(true);
	}

	UActorPartitionSubsystem* ActorPartitionSubsystem = GetWorld()->GetSubsystem<UActorPartitionSubsystem>();
	const uint32 GridSize = LandscapeInfo->LandscapeActor->GridSize;
	
	UActorPartitionSubsystem::FCellCoord CellCoord = UActorPartitionSubsystem::FCellCoord::GetCellCoord(SectionBase, GetWorld()->PersistentLevel, GridSize);
	return LandscapeSubsystemUtils::FindOrAddLandscapeProxy(ActorPartitionSubsystem, LandscapeInfo, CellCoord);
}

#endif