// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_COREUOBJECT

#include "TestCommon/CoreUObjectUtilities.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/PackageResourceManager.h"

bool CoreUObjectInitialised = false;

void InitCoreUObject()
{
	if (CoreUObjectInitialised == true)
		return;

	//Initialize the PackageResourceManager, which is needed to load any (non-script) Packages. It is first used in ProcessNewlyLoadedObjects (due to the loading of asset references in Class Default Objects)
	// It has to be intialized after the AssetRegistryModule; the editor implementations of PackageResourceManager relies on it
	IPackageResourceManager::Initialize();

	if (!GetTransientPackage())
	{
		FModuleManager::Get().AddExtraBinarySearchPaths();
		FConfigCacheIni::InitializeConfigSystem();
		FPlatformFileManager::Get().InitializeNewAsyncIO();

		FModuleManager::Get().LoadModule(TEXT("CoreUObject"));
		FCoreDelegates::OnInit.Broadcast();
		ProcessNewlyLoadedUObjects();
	}

	CoreUObjectInitialised = true;
}

void CleanupCoreUObject()
{
	if (CoreUObjectInitialised == false)
		return;

	IPackageResourceManager::Shutdown();

	FCoreDelegates::OnPreExit.Broadcast();
	MALLOC_PROFILER(GMalloc->Exec(nullptr, TEXT("MPROF STOP"), *GLog); );
	FCoreDelegates::OnExit.Broadcast();

	CoreUObjectInitialised = false;
}

#endif // WITH_COREUOBJECT