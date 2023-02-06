// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_COREUOBJECT

#include "TestCommon/CoreUObjectUtilities.h"

#include "HAL/PlatformFileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/PackageResourceManager.h"

#if WITH_ENGINE
#include "Framework/Application/SlateApplication.h"
#endif

void InitCoreUObject()
{
	IPackageResourceManager::Initialize();

	if (!GetTransientPackage())
	{
		FModuleManager::Get().AddExtraBinarySearchPaths();
		FConfigCacheIni::InitializeConfigSystem();
		FPlatformFileManager::Get().InitializeNewAsyncIO();

		FModuleManager::Get().LoadModule(TEXT("CoreUObject"));
		FCoreDelegates::OnInit.Broadcast();

#if WITH_ENGINE
		FSlateApplication::InitializeCoreStyle();
#endif

		ProcessNewlyLoadedUObjects();
	}
}

void CleanupCoreUObject()
{
	IPackageResourceManager::Shutdown();

	FCoreDelegates::OnPreExit.Broadcast();
	MALLOC_PROFILER(GMalloc->Exec(nullptr, TEXT("MPROF STOP"), *GLog); );
	FCoreDelegates::OnExit.Broadcast();
}

#endif // WITH_COREUOBJECT