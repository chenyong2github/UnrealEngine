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
#include "LandscapeConfigHelper.h"
#include "Engine/Canvas.h"

static int32 GUseStreamingManagerForCameras = 1;
static FAutoConsoleVariableRef CVarUseStreamingManagerForCameras(
	TEXT("grass.UseStreamingManagerForCameras"),
	GUseStreamingManagerForCameras,
	TEXT("1: Use Streaming Manager; 0: Use ViewLocationsRenderedLastFrame"));

DECLARE_CYCLE_STAT(TEXT("LandscapeSubsystem Tick"), STAT_LandscapeSubsystemTick, STATGROUP_Landscape);

#define LOCTEXT_NAMESPACE "LandscapeSubsystem"

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
	GrassMapsBuilder = new FLandscapeGrassMapsBuilder(GetWorld());
	BakedGITextureBuilder = new FLandscapeBakedGITextureBuilder(GetWorld());
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
void ULandscapeSubsystem::BuildAll()
{
	BuildGrassMaps();
	BuildGITextures();
}

void ULandscapeSubsystem::BuildGrassMaps()
{
	GrassMapsBuilder->Build();
}

int32 ULandscapeSubsystem::GetOutdatedGrassMapCount()
{
	return GrassMapsBuilder->GetOutdatedGrassMapCount(/*bInForceUpdate*/false);
}

void ULandscapeSubsystem::BuildGITextures()
{
	BakedGITextureBuilder->Build();
}

int32 ULandscapeSubsystem::GetComponentsNeedingGITextureBaking()
{
	return BakedGITextureBuilder->GetComponentsNeedingTextureBaking();
}

bool ULandscapeSubsystem::IsGridBased() const
{
	return UWorld::HasSubsystem<UWorldPartitionSubsystem>(GetWorld());
}

void ULandscapeSubsystem::ChangeGridSize(ULandscapeInfo* LandscapeInfo, uint32 GridSizeInComponents)
{
	if (!IsGridBased())
	{
		return;
	}

	TSet<AActor*> ActorsToDelete;
	FLandscapeConfigHelper::ChangeGridSize(LandscapeInfo, GridSizeInComponents, ActorsToDelete);
	// This code path is used for converting a non grid based Landscape to a gridbased so it shouldn't delete any actors
	check(!ActorsToDelete.Num());
}

ALandscapeProxy* ULandscapeSubsystem::FindOrAddLandscapeProxy(ULandscapeInfo* LandscapeInfo, const FIntPoint& SectionBase)
{
	if (!IsGridBased())
	{
		return LandscapeInfo->GetCurrentLevelLandscapeProxy(true);
	}

	return FLandscapeConfigHelper::FindOrAddLandscapeStreamingProxy(LandscapeInfo, SectionBase);
}

void ULandscapeSubsystem::DisplayBuildMessages(FCanvas* Canvas, float& XPos, float& YPos)
{
	const int32 FontSizeY = 20;
	FCanvasTextItem SmallTextItem(FVector2D(0, 0), FText::GetEmpty(), GEngine->GetSmallFont(), FLinearColor::White);
	SmallTextItem.EnableShadow(FLinearColor::Black);

	if (int32 OutdatedGrassMapCount = GetOutdatedGrassMapCount())
	{
		SmallTextItem.SetColor(FLinearColor::Red);
		SmallTextItem.Text = FText::Format(LOCTEXT("GRASS_MAPS_NEED_TO_BE_REBUILT_FMT", "GRASS MAPS NEED TO BE REBUILT ({0} {0}|plural(one=object,other=objects))"), OutdatedGrassMapCount);
		Canvas->DrawItem(SmallTextItem, FVector2D(XPos, YPos));
		YPos += FontSizeY;
	}

	if (int32 ComponentsNeedingGITextureBaking = GetComponentsNeedingGITextureBaking())
	{
		SmallTextItem.SetColor(FLinearColor::Red);
		SmallTextItem.Text = FText::Format(LOCTEXT("LANDSCAPE_TEXTURES_NEED_TO_BE_REBUILT_FMT", "LANDSCAPE BAKED TEXTURES NEED TO BE REBUILT ({0} {0}|plural(one=object,other=objects))"), ComponentsNeedingGITextureBaking);
		Canvas->DrawItem(SmallTextItem, FVector2D(XPos, YPos));
		YPos += FontSizeY;
	}
}

#endif

#undef LOCTEXT_NAMESPACE