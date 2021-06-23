// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlatformWithModularFeature/ModularFeaturePlatformBackgroundHttp.h"

#include "GenericPlatform/GenericPlatformBackgroundHttp.h"

#include "Features/IModularFeatures.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogBackgroundHttpModularFeature);

IBackgroundHttpModularFeature* FModularFeaturePlatformBackgroundHttp::CachedModularFeature = nullptr;
bool FModularFeaturePlatformBackgroundHttp::bHasCheckedForModularFeature = false;

void FModularFeaturePlatformBackgroundHttp::Initialize()
{
	CacheModularFeature();

	if (nullptr != CachedModularFeature)
	{
		CachedModularFeature->Initialize();
	}
	else
	{
		FGenericPlatformBackgroundHttp::Initialize();
	}
}

void FModularFeaturePlatformBackgroundHttp::Shutdown()
{
	if (nullptr != CachedModularFeature)
	{
		CachedModularFeature->Shutdown();
	}
	else
	{
		FGenericPlatformBackgroundHttp::Shutdown();
	}
}

FBackgroundHttpManagerPtr FModularFeaturePlatformBackgroundHttp::CreatePlatformBackgroundHttpManager()
{
	CacheModularFeature();

	if (nullptr != CachedModularFeature)
	{
		return CachedModularFeature->CreatePlatformBackgroundHttpManager();
	}
	else
	{
		return FGenericPlatformBackgroundHttp::CreatePlatformBackgroundHttpManager();
	}
}

FBackgroundHttpRequestPtr FModularFeaturePlatformBackgroundHttp::ConstructBackgroundRequest()
{
	CacheModularFeature();

	if (nullptr != CachedModularFeature)
	{
		return CachedModularFeature->ConstructBackgroundRequest();
	}
	else
	{
		return FGenericPlatformBackgroundHttp::ConstructBackgroundRequest();
	}
}

FBackgroundHttpResponsePtr FModularFeaturePlatformBackgroundHttp::ConstructBackgroundResponse(int32 ResponseCode, const FString& TempFilePath)
{
	CacheModularFeature();

	if (nullptr != CachedModularFeature)
	{
		return CachedModularFeature->ConstructBackgroundResponse(ResponseCode, TempFilePath);
	}
	else
	{
		return FGenericPlatformBackgroundHttp::ConstructBackgroundResponse(ResponseCode, TempFilePath);
	}
}

FName FModularFeaturePlatformBackgroundHttp::GetModularFeatureName()
{
	static FName ModularFeatureName = FName(TEXT("BackgroundHttpModularFeature"));
	return ModularFeatureName;
}

void FModularFeaturePlatformBackgroundHttp::CacheModularFeature()
{
	if (!bHasCheckedForModularFeature)
	{
		bHasCheckedForModularFeature = true;

		FString CommandLineOverrideName;
		const bool bDidCommandLineOverride = FParse::Value(FCommandLine::Get(), TEXT("-BackgroundHttpModularFeatureNameOverride="), CommandLineOverrideName);
		
		//First get name of plugin from the .ini
		FString ModuleName;
		if (bDidCommandLineOverride)
		{
			ModuleName = CommandLineOverrideName;
		}
		else if (!GEngineIni.IsEmpty())
		{
			GConfig->GetString(TEXT("BackgroundHttp"), TEXT("PlatformModularFeatureName"), ModuleName, GEngineIni);
		}

		//If we don't have any expected module then we don't want to cache any modular features
		if (ModuleName.IsEmpty())
		{
			//Should still be nullptr but explicitly setting it here as a sanity check
			CachedModularFeature = nullptr;
			UE_LOG(LogBackgroundHttpModularFeature, Display, TEXT("BackgroundHttpModularFeature module not set, falling back to generic implementation."));
			return;
		}

		FModuleManager& ModuleManager = FModuleManager::Get();
		const bool bDesiredModuleExists = ModuleName.IsEmpty() ? false : ModuleManager.ModuleExists(*ModuleName);
		if (bDesiredModuleExists)
		{
			if (false == ModuleManager.IsModuleLoaded(*ModuleName))
			{
				ModuleManager.LoadModule(*ModuleName);
			}
		
			if (IModularFeatures::Get().IsModularFeatureAvailable(GetModularFeatureName()))
			{
				CachedModularFeature = &(IModularFeatures::Get().GetModularFeature<IBackgroundHttpModularFeature>(GetModularFeatureName()));
				UE_LOG(LogBackgroundHttpModularFeature, Display, TEXT("Using BackgroundHTTPModularFeature module: %s for ModularFeatureName: %s"), *CachedModularFeature->GetDebugModuleName(), *(GetModularFeatureName().ToString()));
			}
			else
			{
				UE_LOG(LogBackgroundHttpModularFeature, Error, TEXT("Module %s exists but is not available for ModularFeatureName: %s! Make sure it correctly registers itself as a modular feature when loaded!"), *ModuleName, *(GetModularFeatureName().ToString()));
			}
		}
		
		ensureAlwaysMsgf((CachedModularFeature != nullptr), TEXT("Unable to load expected BackgroundHttp ModularFeature module %s for ModularFeatureName: %s ! BackgroundHttp will fallback to generic implementation!"), *ModuleName, *(GetModularFeatureName().ToString()));
	}
}