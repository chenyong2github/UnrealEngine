// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "ContentStreaming.h"
#include "Landscape.h"
#include "ProfilingDebugging/CsvProfiler.h"

static int32 GUseStreamingManagerForCameras = 1;
static FAutoConsoleVariableRef CVarUseStreamingManagerForCameras(
	TEXT("grass.UseStreamingManagerForCameras"),
	GUseStreamingManagerForCameras,
	TEXT("1: Use Streaming Manager; 0: Use ViewLocationsRenderedLastFrame"));

DECLARE_CYCLE_STAT(TEXT("LandscapeSubsystem Tick"), STAT_LandscapeSubsystemTick, STATGROUP_Landscape);

ULandscapeSubsystem::ULandscapeSubsystem()
	: TickFunction(this)
{}

ULandscapeSubsystem::~ULandscapeSubsystem()
{

}

void ULandscapeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Register Tick 
	TickFunction.bCanEverTick = true;
	TickFunction.bTickEvenWhenPaused = true;
	TickFunction.bStartWithTickEnabled = true;
	TickFunction.TickGroup = TG_DuringPhysics;
	TickFunction.bAllowTickOnDedicatedServer = false;
	TickFunction.RegisterTickFunction(GetWorld()->PersistentLevel);
}

void ULandscapeSubsystem::Deinitialize()
{
	TickFunction.UnRegisterTickFunction();
	Proxies.Empty();
}

void ULandscapeSubsystem::RegisterActor(ALandscapeProxy* Proxy)
{
	Proxies.AddUnique(Proxy);
}

void ULandscapeSubsystem::UnregisterActor(ALandscapeProxy* Proxy)
{
	Proxies.Remove(Proxy);
}

void ULandscapeSubsystem::Tick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
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
	for(ALandscapeProxy* Proxy : Proxies)
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
			}
		}
#endif
		if (Cameras && Proxy->ShouldTickGrass())
		{
			Proxy->TickGrass(*Cameras, InOutNumComponentsCreated);
		}
	}
}

void FLandscapeSubsystemTickFunction::ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	Subsystem->Tick(DeltaTime, TickType, CurrentThread, MyCompletionGraphEvent);
}

FString FLandscapeSubsystemTickFunction::DiagnosticMessage()
{
	static const FString Message(TEXT("FLandscapeSubsystemTickFunction"));
	return Message;
}

FName FLandscapeSubsystemTickFunction::DiagnosticContext(bool bDetailed)
{
	static const FName Context(TEXT("FLandscapeSubsystemTickFunction"));
	return Context;
}