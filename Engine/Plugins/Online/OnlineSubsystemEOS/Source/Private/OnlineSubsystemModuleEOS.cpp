// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemEOSModule.h"
#include "OnlineSubsystemModule.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemEOS.h"
#include "EOSSettings.h"

#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"

#include "Misc/CoreDelegates.h"

#if WITH_EDITOR
	#include "ISettingsModule.h"
	#include "ISettingsSection.h"
#endif

#define LOCTEXT_NAMESPACE "EOS"

IMPLEMENT_MODULE(FOnlineSubsystemEOSModule, OnlineSubsystemEOS);

/**
 * Class responsible for creating instance(s) of the subsystem
 */
class FOnlineFactoryEOS :
	public IOnlineFactory
{
public:

	FOnlineFactoryEOS() {}
	virtual ~FOnlineFactoryEOS() {}

	virtual IOnlineSubsystemPtr CreateSubsystem(FName InstanceName)
	{
		FOnlineSubsystemEOSPtr OnlineSub = MakeShared<FOnlineSubsystemEOS, ESPMode::ThreadSafe>(InstanceName);
		if (!OnlineSub->Init())
		{
			UE_LOG_ONLINE(Warning, TEXT("EOS API failed to initialize!"));
			OnlineSub->Shutdown();
			OnlineSub = nullptr;
		}

		return OnlineSub;
	}
};

void FOnlineSubsystemEOSModule::StartupModule()
{
	EOSFactory = new FOnlineFactoryEOS();

	// Create and register our singleton factory with the main online subsystem for easy access
	FOnlineSubsystemModule& OSS = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
	OSS.RegisterPlatformService(EOS_SUBSYSTEM, EOSFactory);

#if WITH_EOS_SDK
	// Have to call this as early as possible in order to hook the rendering device
	FOnlineSubsystemEOS::ModuleInit();
#endif

#if WITH_EDITOR
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FOnlineSubsystemEOSModule::OnPostEngineInit);
	FCoreDelegates::OnPreExit.AddRaw(this, &FOnlineSubsystemEOSModule::OnPreExit);
#endif
}

#if WITH_EDITOR
void FOnlineSubsystemEOSModule::OnPostEngineInit()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr)
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "Epic Online Services",
			LOCTEXT("EOSSettingsName", "Epic Online Services"),
			LOCTEXT("EOSSettingsDescription", "Configure the Epic Online Services"),
			GetMutableDefault<UEOSSettings>());
	}
}
#endif

#if WITH_EDITOR
void FOnlineSubsystemEOSModule::OnPreExit()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule)
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "Epic Online Services");
	}
}
#endif

void FOnlineSubsystemEOSModule::ShutdownModule()
{
	FCoreDelegates::OnInit.RemoveAll(this);
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
	FCoreDelegates::OnPreExit.RemoveAll(this);

	FOnlineSubsystemModule& OSS = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
	OSS.UnregisterPlatformService(EOS_SUBSYSTEM);

	delete EOSFactory;
	EOSFactory = nullptr;
}

#undef LOCTEXT_NAMESPACE
