// Copyright Epic Games, Inc. All Rights Reserved.

#include "EOSSharedModule.h"

#include "Features/IModularFeatures.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"

#include COMPILED_PLATFORM_HEADER(EOSSDKManager.h)

#define LOCTEXT_NAMESPACE "EOS"

IMPLEMENT_MODULE(FEOSSharedModule, EOSShared);

void FEOSSharedModule::StartupModule()
{
#if WITH_EOS_SDK
	SDKManager = MakeUnique<FPlatformEOSSDKManager>();
	check(SDKManager);

	IModularFeatures::Get().RegisterModularFeature(TEXT("EOSSDKManager"), SDKManager.Get());

	// Load from a configurable array of modules at this point, so things that need to bind to the SDK Manager init hooks can do so.
	TArray<FString> ModulesToLoad;
	GConfig->GetArray(TEXT("EOSShared"), TEXT("ModulesToLoad"), ModulesToLoad, GEngineIni);
	for (const FString& ModuleToLoad : ModulesToLoad)
	{
		if (FModuleManager::Get().ModuleExists(*ModuleToLoad))
		{
			FModuleManager::Get().LoadModule(*ModuleToLoad);
		}
	}
#endif // WITH_EOS_SDK
}

void FEOSSharedModule::ShutdownModule()
{
#if WITH_EOS_SDK
	if(SDKManager.IsValid())
	{
		IModularFeatures::Get().UnregisterModularFeature(TEXT("EOSSDKManager"), SDKManager.Get());
		SDKManager->Shutdown();
		SDKManager.Reset();
	}
#endif // WITH_EOS_SDK
}

#undef LOCTEXT_NAMESPACE