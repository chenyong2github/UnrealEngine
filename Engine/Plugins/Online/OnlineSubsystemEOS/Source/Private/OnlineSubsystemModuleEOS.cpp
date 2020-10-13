// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemEOSModule.h"
#include "OnlineSubsystemModule.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemEOS.h"

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
}

void FOnlineSubsystemEOSModule::ShutdownModule()
{
	FOnlineSubsystemModule& OSS = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
	OSS.UnregisterPlatformService(EOS_SUBSYSTEM);

	delete EOSFactory;
	EOSFactory = nullptr;
}
