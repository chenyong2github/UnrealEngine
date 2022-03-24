// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_COREUOBJECT

#include "TestCommon/CoreUObjectUtilities.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/PackageResourceManager.h"

void InitCoreUObject()
{
	if (!GetTransientPackage())
	{
		FModuleManager::Get().AddExtraBinarySearchPaths();
		FConfigCacheIni::InitializeConfigSystem();
		FPlatformFileManager::Get().InitializeNewAsyncIO();

		FModuleManager::Get().LoadModule(TEXT("CoreUObject"));
		FCoreDelegates::OnInit.Broadcast();
		ProcessNewlyLoadedUObjects();
	}
}

void CleanupCoreUObject()
{
	FCoreDelegates::OnPreExit.Broadcast();
	MALLOC_PROFILER(GMalloc->Exec(nullptr, TEXT("MPROF STOP"), *GLog); );
	FCoreDelegates::OnExit.Broadcast();
}

#endif // WITH_COREUOBJECT