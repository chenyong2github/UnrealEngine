// Copyright Epic Games, Inc. All Rights Reserved.

#include "EOSSharedModule.h"

#include "Features/IModularFeatures.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "ISettingsModule.h"
#endif // WITH_EDITOR

#include COMPILED_PLATFORM_HEADER(EOSSDKManager.h)

#define LOCTEXT_NAMESPACE "EOS"

IMPLEMENT_MODULE(FEOSSharedModule, EOSShared);

void FEOSSharedModule::StartupModule()
{
#if WITH_EOS_SDK
	SDKManager = MakeUnique<FPlatformEOSSDKManager>();
	check(SDKManager);

	IModularFeatures::Get().RegisterModularFeature(TEXT("EOSSDKManager"), SDKManager.Get());
#endif // WITH_EOS_SDK

#if WITH_EDITOR
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FEOSSharedModule::OnPostEngineInit);
	FCoreDelegates::OnPreExit.AddRaw(this, &FEOSSharedModule::OnPreExit);
#endif // WITH_EDITOR
}

#if WITH_EDITOR
void FEOSSharedModule::OnPostEngineInit()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "Epic Online Services SDK",
			LOCTEXT("EOSSDKSettingsName", "Epic Online Services SDK"),
			LOCTEXT("EOSSDKSettingsDescription", "Configure the Epic Online Services SDK"),
			GetMutableDefault<UEOSSharedSettings>());
	}
}

void FEOSSharedModule::OnPreExit()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "Epic Online Services SDK");
	}
}
#endif // WITH_EDITOR

void FEOSSharedModule::ShutdownModule()
{
#if WITH_EDITOR
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
	FCoreDelegates::OnPreExit.RemoveAll(this);
#endif // WITH_EDITOR

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