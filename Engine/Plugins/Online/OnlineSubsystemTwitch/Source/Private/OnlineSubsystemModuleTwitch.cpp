// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemTwitchPrivate.h"

IMPLEMENT_MODULE(FOnlineSubsystemTwitchModule, OnlineSubsystemTwitch);

/**
 * Class responsible for creating instance(s) of the subsystem
 */
class FOnlineFactoryTwitch : public IOnlineFactory
{
public:

	FOnlineFactoryTwitch() = default;
	virtual ~FOnlineFactoryTwitch() = default;

	virtual IOnlineSubsystemPtr CreateSubsystem(FName InstanceName)
	{
		static bool bDeprecationLogged = false;
		if (!bDeprecationLogged)
		{
			UE_LOG(LogOnline, Warning, TEXT("OnlineSubsystemTwitch has been deprecated"));
			bDeprecationLogged = true;
		}
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		FOnlineSubsystemTwitchPtr OnlineSub = MakeShared<FOnlineSubsystemTwitch, ESPMode::ThreadSafe>(InstanceName);
		if (OnlineSub->IsEnabled())
		{
			if (!OnlineSub->Init())
			{
				UE_LOG_ONLINE(Warning, TEXT("Twitch API failed to initialize instance %s!"), *InstanceName.ToString());
				OnlineSub->Shutdown();
				OnlineSub = nullptr;
			}

			return OnlineSub;
		}
		else
		{
			static bool bHasAlerted = false;
			if (!bHasAlerted)
			{
				// Alert once with a warning for visibility (should be at the beginning)
				UE_LOG_ONLINE(Log, TEXT("Twitch API disabled."));
				bHasAlerted = true;
			}
			OnlineSub->Shutdown();
			OnlineSub.Reset();
		}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

		return nullptr;
	}
};

void FOnlineSubsystemTwitchModule::StartupModule()
{
	UE_LOG_ONLINE(Verbose, TEXT("Twitch Startup!"));

	TwitchFactory = MakeUnique<FOnlineFactoryTwitch>();

	// Create and register our singleton factory with the main online subsystem for easy access
	FOnlineSubsystemModule& OSS = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	OSS.RegisterPlatformService(TWITCH_SUBSYSTEM, TwitchFactory.Get());
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FOnlineSubsystemTwitchModule::ShutdownModule()
{
	UE_LOG_ONLINE(Verbose, TEXT("Twitch Shutdown!"));

	FOnlineSubsystemModule& OSS = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	OSS.UnregisterPlatformService(TWITCH_SUBSYSTEM);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	TwitchFactory.Reset();
}
