// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "ContentStreaming.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "ProfilingDebugging/CsvProfiler.h"

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

	// Register Tick 
	bCanEverTick = true;
	bTickEvenWhenPaused = true;
	bStartWithTickEnabled = true;
	TickGroup = TG_DuringPhysics;
	bAllowTickOnDedicatedServer = false;
	RegisterTickFunction(GetWorld()->PersistentLevel);

#if WITH_EDITOR
	GrassMapsBuilder = MakeUnique<FLandscapeGrassMapsBuilder>(GetWorld());
#endif
}

void ULandscapeSubsystem::Deinitialize()
{
	UnRegisterTickFunction();
	Proxies.Empty();

	Super::Deinitialize();
}

void ULandscapeSubsystem::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
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

FString ULandscapeSubsystem::DiagnosticMessage()
{
	static const FString Message(TEXT("ULandscapeSubsystem"));
	return Message;
}

FName ULandscapeSubsystem::DiagnosticContext(bool bDetailed)
{
	static const FName Context(TEXT("ULandscapeSubsystem"));
	return Context;
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
#endif