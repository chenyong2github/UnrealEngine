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
#if UE_LLT_WITH_MOCK_ENGINE_DEFAULTS
#include "Materials/Material.h"
#endif // UE_LLT_WITH_MOCK_ENGINE_DEFAULTS
#include "Styling/UMGCoreStyle.h"
#endif //WITH_ENGINE

void InitCoreUObject()
{
	IPackageResourceManager::Initialize();

	FDelayedAutoRegisterHelper::RunAndClearDelayedAutoRegisterDelegates(EDelayedRegisterRunPhase::FileSystemReady);

	if (!GetTransientPackage())
	{
		FModuleManager::Get().AddExtraBinarySearchPaths();
		FConfigCacheIni::InitializeConfigSystem();
		FPlatformFileManager::Get().InitializeNewAsyncIO();

#if WITH_ENGINE && UE_LLT_WITH_MOCK_ENGINE_DEFAULTS
		GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("AIControllerClassName"), TEXT("/Script/AIModule.AIController"), GEngineIni);
		GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("DefaultMaterialName"), TEXT("/Engine/Transient.MockDefaultMaterial"), GEngineIni);
		GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("DefaultLightFunctionMaterialName"), TEXT("/Engine/Transient.MockDefaultMaterial"), GEngineIni);
		GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("DefaultDeferredDecalMaterialName"), TEXT("/Engine/Transient.MockDefaultMaterial"), GEngineIni);
		GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("DefaultPostProcessMaterialName"), TEXT("/Engine/Transient.MockDefaultMaterial"), GEngineIni);
#endif // WITH_ENGINE && UE_LLT_WITH_MOCK_ENGINE_DEFAULTS

		FModuleManager::Get().LoadModule(TEXT("CoreUObject"));
		FCoreDelegates::OnInit.Broadcast();

#if WITH_ENGINE
		FUMGCoreStyle::ResetToDefault();
	#if UE_LLT_WITH_MOCK_ENGINE_DEFAULTS
		UMaterial* MockMaterial = NewObject<UMaterial>(GetTransientPackage(), UMaterial::StaticClass(), TEXT("MockDefaultMaterial"), RF_Transient | RF_MarkAsRootSet);
	#endif // UE_LLT_WITH_MOCK_ENGINE_DEFAULTS
#endif // WITH_ENGINE

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