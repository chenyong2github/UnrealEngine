// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemEOSPlusModule.h"
#include "OnlineSubsystemModule.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemEOSPlus.h"

#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"

#define LOCTEXT_NAMESPACE "EOSPlus"

IMPLEMENT_MODULE(FOnlineSubsystemEOSPlusModule, OnlineSubsystemEOSPlus);

/**
 * Class responsible for creating instance(s) of the subsystem
 */
class FOnlineFactoryEOSPlus :
	public IOnlineFactory
{
public:

	FOnlineFactoryEOSPlus() {}
	virtual ~FOnlineFactoryEOSPlus() {}

	virtual IOnlineSubsystemPtr CreateSubsystem(FName InstanceName)
	{
		FOnlineSubsystemEOSPlusPtr OnlineSub = MakeShared<FOnlineSubsystemEOSPlus, ESPMode::ThreadSafe>(InstanceName);
		if (!OnlineSub->Init())
		{
			UE_LOG_ONLINE(Warning, TEXT("EOSPlus failed to initialize!"));
			OnlineSub->Shutdown();
			OnlineSub = nullptr;
		}

		return OnlineSub;
	}
};

void FOnlineSubsystemEOSPlusModule::StartupModule()
{
	PlusFactory = new FOnlineFactoryEOSPlus();

	// Create and register our singleton factory with the main online subsystem for easy access
	FOnlineSubsystemModule& OSS = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
	OSS.RegisterPlatformService(EOSPLUS_SUBSYSTEM, PlusFactory);
}

void FOnlineSubsystemEOSPlusModule::ShutdownModule()
{
	FOnlineSubsystemModule& OSS = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
	OSS.UnregisterPlatformService(EOSPLUS_SUBSYSTEM);

	delete PlusFactory;
	PlusFactory = nullptr;
}

#undef LOCTEXT_NAMESPACE
