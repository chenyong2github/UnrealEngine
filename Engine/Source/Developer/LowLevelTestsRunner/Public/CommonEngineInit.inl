// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformOutputDevices.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/QueuedThreadPool.h"

#if WITH_APPLICATION_CORE
#include "HAL/PlatformApplicationMisc.h"
#endif // WITH_APPLICATION_CORE

#if WITH_ENGINE
#include "DistanceFieldAtlas.h"
#include "MeshCardRepresentation.h"
#include "RenderUtils.h"
#include "ShaderParameterMetadata.h"
#endif // WITH_ENGINE

#if WITH_EDITORONLY_DATA && defined(DERIVEDDATACACHE_API)
#include "DerivedDataBuild.h"
#include "DerivedDataCache.h"
#endif // WITH_EDITORONLY_DATA && defined(DERIVEDDATACACHE_API)

#if WITH_COREUOBJECT
#include "UObject/Package.h"
#include "UObject/PackageResourceManager.h"
#endif // WITH_COREUOBJECT

/**
 * A lot of what's in this file was taken from LaunchEngineLoop.cpp.
 * It's made available here in an effort to reuse the same init code through various low level tests programs.
 */

void InitThreadPool()
{
#if WITH_EDITOR
	{
		int32 NumThreadsInLargeThreadPool = FMath::Max( FPlatformMisc::NumberOfCoresIncludingHyperthreads() - 2, 2 );
		int32 NumThreadsInThreadPool = FPlatformMisc::NumberOfWorkerThreadsToSpawn();
		// when we are in the editor we like to do things like build lighting and such
		// this thread pool can be used for those purposes
		extern CORE_API int32 GUseNewTaskBackend;
		if ( !GUseNewTaskBackend )
		{
			GLargeThreadPool = FQueuedThreadPool::Allocate();
		}
		else
		{
			GLargeThreadPool = new FQueuedLowLevelThreadPool();
		}
	
		constexpr int32 StackSize = 128 * 1024;

		// TaskGraph has it's HP threads slightly below normal, we want to be below the taskgraph HP threads to avoid interfering with the game-thread.
		verify(GLargeThreadPool->Create(NumThreadsInLargeThreadPool, StackSize, TPri_BelowNormal, TEXT("LargeThreadPool")));

		// GThreadPool will schedule on the LargeThreadPool but limit max concurrency to the given number.
		GThreadPool = new FQueuedThreadPoolWrapper( GLargeThreadPool, NumThreadsInThreadPool );
	}
#endif // WITH_EDITOR
	if (FPlatformProcess::SupportsMultithreading())
	{
		GIOThreadPool = FQueuedThreadPool::Allocate();
		int32 NumThreadsInThreadPool = FPlatformMisc::NumberOfIOWorkerThreadsToSpawn();
		verify(GIOThreadPool->Create(NumThreadsInThreadPool, 96 * 1024, TPri_AboveNormal, TEXT("IOThreadPool")));
	}
}

void CleanupThreadPool()
{
	if (GThreadPool != nullptr)
	{
		GThreadPool->Destroy();
	}
	if (GBackgroundPriorityThreadPool != nullptr)
	{
		GBackgroundPriorityThreadPool->Destroy();
	}
	if (GIOThreadPool != nullptr)
	{
		GIOThreadPool->Destroy();
	}
}

void InitTaskGraph()
{
	FTaskGraphInterface::Startup(bMultithreaded ? FPlatformMisc::NumberOfWorkerThreadsToSpawn() : 1);
	FTaskGraphInterface::Get().AttachToThread(ENamedThreads::GameThread);
}

void CleanupTaskGraph()
{
	FTaskGraphInterface::Shutdown();
}

void InitCoreUObject()
{
#if WITH_COREUOBJECT
	if (!GetTransientPackage())
	{
		FModuleManager::Get().AddExtraBinarySearchPaths();
		FConfigCacheIni::InitializeConfigSystem();
		FPlatformFileManager::Get().InitializeNewAsyncIO();

		FModuleManager::Get().LoadModule(TEXT("CoreUObject"));
		FCoreDelegates::OnInit.Broadcast();
		ProcessNewlyLoadedUObjects();
	}
#endif // WITH_COREUOBJECT
}

void CleanupCoreUObject()
{
#if WITH_COREUOBJECT
	FCoreDelegates::OnPreExit.Broadcast();
	MALLOC_PROFILER(GMalloc->Exec(nullptr, TEXT("MPROF STOP"), *GLog); );
	FCoreDelegates::OnExit.Broadcast();
#endif // WITH_COREUOBJECT
}

void InitOutputDevices()
{
#if WITH_APPLICATION_CORE
	GError = FPlatformApplicationMisc::GetErrorOutputDevice();
	GWarn = FPlatformApplicationMisc::GetFeedbackContext();
#else
	GError = FPlatformOutputDevices::GetError();
	GWarn = FPlatformOutputDevices::GetFeedbackContext();
#endif
}

#if STATS
void InitStats()
{
	FThreadStats::StartThread();
}
#endif // #if STATS

void InitRendering()
{
#if WITH_ENGINE
	FShaderParametersMetadata::InitializeAllUniformBufferStructs();

	{
		// Initialize the RHI.
		const bool bHasEditorToken = false;
		RHIInit(bHasEditorToken);
	}

	{
		// One-time initialization of global variables based on engine configuration.
		RenderUtilsInit();
	}
#endif // WITH_ENGINE
}

void InitDerivedDataCache()
{
#if WITH_EDITORONLY_DATA && defined(DERIVEDDATACACHE_API)
	if (!FPlatformProperties::RequiresCookedData())
	{
		// Ensure that DDC is initialized from the game thread.
		UE::DerivedData::GetBuild();
		UE::DerivedData::GetCache();
		GetDerivedDataCacheRef();
	}
#endif // WITH_EDITORONLY_DATA && defined(DERIVEDDATACACHE_API)
}


void InitAsyncQueues()
{
#if WITH_ENGINE
	check(!GDistanceFieldAsyncQueue);
	GDistanceFieldAsyncQueue = new FDistanceFieldAsyncQueue();

	check(!GCardRepresentationAsyncQueue);
	GCardRepresentationAsyncQueue = new FCardRepresentationAsyncQueue();
#endif // WITH_ENGINE
}

void InitSlate()
{
#if WITH_EDITOR
	FSlateApplication::Create();

	TSharedPtr<FSlateRenderer> SlateRenderer = FModuleManager::Get().LoadModuleChecked<ISlateNullRendererModule>("SlateNullRenderer").CreateSlateNullRenderer();
	TSharedRef<FSlateRenderer> SlateRendererSharedRef = SlateRenderer.ToSharedRef();

	// If Slate is being used, initialize the renderer after RHIInit
	FSlateApplication& CurrentSlateApp = FSlateApplication::Get();
	CurrentSlateApp.InitializeRenderer(SlateRendererSharedRef);
#endif // WITH_EDITOR
}

void InitForWithEditorOnlyData()
{
#if WITH_COREUOBJECT
	//Initialize the PackageResourceManager, which is needed to load any (non-script) Packages. It is first used in ProcessNewlyLoadedObjects (due to the loading of asset references in Class Default Objects)
	// It has to be intialized after the AssetRegistryModule; the editor implementations of PackageResourceManager relies on it
	IPackageResourceManager::Initialize();
#endif // WITH_COREUOBJECT
#if WITH_EDITOR
	// Initialize the BulkDataRegistry, which registers BulkData structs loaded from Packages for later building. It uses the same lifetime as IPackageResourceManager
	IBulkDataRegistry::Initialize();
#endif // WITH_EDITOR
}

void InitEditor()
{
#if WITH_EDITOR
	FModuleManager::Get().LoadModuleChecked("UnrealEd");

	GIsEditor = true;
	GEngine = GEditor = NewObject<UEditorEngine>(GetTransientPackage(), UEditorEngine::StaticClass());

	GEngine->ParseCommandline();
	GEditor->InitEditor(&GEngineLoop);
#endif // #if WITH_EDITOR
}

void CleanupPlatform()
{
	FPlatformMisc::PlatformTearDown();
	FGenericPlatformMisc::RequestExit(false);
}

