// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteSessionModule.h"
#include "Framework/Application/SlateApplication.h"
#include "BackChannel/Transport/IBackChannelTransport.h"
#include "RemoteSessionHost.h"
#include "RemoteSessionClient.h"

#include "Channels/RemoteSessionARCameraChannel.h"
#include "Channels/RemoteSessionARSystemChannel.h"
#include "Channels/RemoteSessionInputChannel.h"
#include "Channels/RemoteSessionFrameBufferChannel.h"
#include "Channels/RemoteSessionXRTrackingChannel.h"

#include "CoreGlobals.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"

#if WITH_EDITOR
	#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "FRemoteSessionModule"

#define REMOTE_SESSION_VERSION_STRING TEXT("1.1.0")
#define REMOTE_SESSION_LEGACY_VERSION_STRING TEXT("1.0.5")


FString IRemoteSessionModule::GetLocalVersion()
{
	return REMOTE_SESSION_VERSION_STRING;
}

FString IRemoteSessionModule::GetLastSupportedVersion()
{
	return REMOTE_SESSION_LEGACY_VERSION_STRING;
}

void FRemoteSessionModule::SetAutoStartWithPIE(bool bEnable)
{
	bAutoHostWithPIE = bEnable;
}

void FRemoteSessionModule::StartupModule()
{	
	if (PLATFORM_DESKTOP 
		&& IsRunningDedicatedServer() == false 
		&& IsRunningCommandlet() == false)
	{
#if WITH_EDITOR
		PostPieDelegate = FEditorDelegates::PostPIEStarted.AddRaw(this, &FRemoteSessionModule::OnPIEStarted);
		EndPieDelegate = FEditorDelegates::EndPIE.AddRaw(this, &FRemoteSessionModule::OnPIEEnded);
#endif
		GameStartDelegate = FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FRemoteSessionModule::OnPostInit);
	}
}


void FRemoteSessionModule::AddChannelFactory(const FStringView InChannelName, ERemoteSessionChannelMode InHostMode, TWeakPtr<IRemoteSessionChannelFactoryWorker> Worker)
{
	FRemoteSessionChannelRegistry::Get().RegisterChannelFactory(*FString(InChannelName.Len(), InChannelName.GetData()), InHostMode, Worker);
}

void FRemoteSessionModule::RemoveChannelFactory(TWeakPtr<IRemoteSessionChannelFactoryWorker> Worker)
{
	FRemoteSessionChannelRegistry::Get().RemoveChannelFactory(Worker);
}

void FRemoteSessionModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
#if WITH_EDITOR
	if (PostPieDelegate.IsValid())
	{
		FEditorDelegates::PostPIEStarted.Remove(PostPieDelegate);
	}

	if (EndPieDelegate.IsValid())
	{
		FEditorDelegates::EndPIE.Remove(EndPieDelegate);
	}
#endif

	if (GameStartDelegate.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(GameStartDelegate);
	}
}

void FRemoteSessionModule::OnPostInit()
{
	const URemoteSessionSettings* Settings = URemoteSessionSettings::StaticClass()->GetDefaultObject<URemoteSessionSettings>();

	bAutoHostWithPIE = Settings->bAutoHostWithPIE;
	bAutoHostWithGame = Settings->bAutoHostWithPIE;
	DefaultPort = Settings->HostPort;

	// port can be overriden on the command line
	FParse::Value(FCommandLine::Get(), TEXT("remote.port="), DefaultPort);

	TArray<FRemoteSessionChannelInfo> KnownChannels = FRemoteSessionChannelRegistry::Get().GetRegisteredFactories();

	// check channels
	for (const auto& Channel : Settings->WhitelistedChannels)
	{
		if (KnownChannels.ContainsByPredicate([Channel](const FRemoteSessionChannelInfo& Info) {
			return Info.Type == Channel;
			}))
		{
			UE_LOG(LogRemoteSession, Error, TEXT("Channel %s in the ini file whitelist is not a recognized channel."), *Channel);
		}
	}

	for (const auto& Channel : Settings->BlacklistedChannels)
	{
		if (KnownChannels.ContainsByPredicate([Channel](const FRemoteSessionChannelInfo& Info) {
			return Info.Type == Channel;
			}))
		{
			UE_LOG(LogRemoteSession, Error, TEXT("Channel %s in the ini file blacklist is not a recognized channel."), *Channel);
		}
	}

	bool IsHostGame = PLATFORM_DESKTOP
		&& GIsEditor == false
		&& IsRunningDedicatedServer() == false
		&& IsRunningCommandlet() == false;

	if (IsHostGame && bAutoHostWithGame)
	{
		InitHost();
	}
}

void FRemoteSessionModule::OnPIEStarted(bool bSimulating)
{
	if (bAutoHostWithPIE)
	{
		InitHost();
	}
}

void FRemoteSessionModule::OnPIEEnded(bool bSimulating)
{
	// always stop, in case it was started via the console
	StopHost();
}

TSharedPtr<IRemoteSessionRole> FRemoteSessionModule::CreateClient(const TCHAR* RemoteAddress)
{
	// todo - remove this and allow multiple clients (and hosts) to be created
	if (Client.IsValid())
	{
		StopClient(Client);
	}
	Client = MakeShareable(new FRemoteSessionClient(RemoteAddress));
	return Client;
}

void FRemoteSessionModule::StopClient(TSharedPtr<IRemoteSessionRole> InClient)
{
	if (InClient.IsValid())
	{
		TSharedPtr<FRemoteSessionClient> CastClient = StaticCastSharedPtr<FRemoteSessionClient>(InClient);
		CastClient->Close();
			
		if (CastClient == Client)
		{
			Client = nullptr;
		}
	}
}

void FRemoteSessionModule::InitHost(const int16 Port /*= 0*/)
{
	if (Host.IsValid())
	{
		Host = nullptr;
	}

	TArray<FRemoteSessionChannelInfo> SupportedChannels;

	const URemoteSessionSettings* Settings = URemoteSessionSettings::StaticClass()->GetDefaultObject<URemoteSessionSettings>();

	SupportedChannels = FRemoteSessionChannelRegistry::Get().GetRegisteredFactories();

	if (Settings->WhitelistedChannels.Num())
	{
		// Remove anything not in the whitelist
		SupportedChannels = SupportedChannels.FilterByPredicate([Settings](const FRemoteSessionChannelInfo& Info) {
			return Settings->WhitelistedChannels.Contains(Info.Type);
		});
	}


	if (Settings->BlacklistedChannels.Num())
	{
		// Remove anything in the blacklist
		SupportedChannels = SupportedChannels.FilterByPredicate([Settings](const FRemoteSessionChannelInfo& Info) {
			return Settings->BlacklistedChannels.Contains(Info.Type) == false;
		});
	}

	int16 SelectedPort = Port ? Port : (int16)DefaultPort;
	if (TSharedPtr<FRemoteSessionHost> NewHost = CreateHostInternal(MoveTemp(SupportedChannels), SelectedPort))
	{
		Host = NewHost;
		UE_LOG(LogRemoteSession, Log, TEXT("Started listening on port %d"), SelectedPort);
	}
	else
	{
		UE_LOG(LogRemoteSession, Error, TEXT("Failed to start host listening on port %d"), SelectedPort);
	}
}

bool FRemoteSessionModule::IsHostConnected() const
{
	return Host.IsValid() && Host->IsConnected();
}

TSharedPtr<IRemoteSessionRole> FRemoteSessionModule::GetHost() const
{
	return Host;
}

TSharedPtr<IRemoteSessionUnmanagedRole> FRemoteSessionModule::CreateHost(TArray<FRemoteSessionChannelInfo> SupportedChannels, int32 Port) const
{
	return CreateHostInternal(MoveTemp(SupportedChannels), Port);
}

TSharedPtr<FRemoteSessionHost> FRemoteSessionModule::CreateHostInternal(TArray<FRemoteSessionChannelInfo> SupportedChannels, int32 Port) const
{
#if UE_BUILD_SHIPPING

	const URemoteSessionSettings* Settings = URemoteSessionSettings::StaticClass()->GetDefaultObject<URemoteSessionSettings>();
	bool bAllowInShipping = false;
	if (Settings->bAllowInShipping == false)
	{
		UE_LOG(LogRemoteSession, Log, TEXT("RemoteSession is disabled. Shipping=1"));
		return TSharedPtr<FRemoteSessionHost>();
	}
#endif

	TSharedPtr<FRemoteSessionHost> NewHost = MakeShared<FRemoteSessionHost>(MoveTemp(SupportedChannels));
	if (NewHost->StartListening(Port))
	{
		return NewHost;
	}
	return TSharedPtr<FRemoteSessionHost>();
}

void FRemoteSessionModule::Tick(float DeltaTime)
{
	if (Client.IsValid())
	{
		Client->Tick(DeltaTime);
	}

	if (Host.IsValid())
	{
		Host->Tick(DeltaTime);
	}
}

IMPLEMENT_MODULE(FRemoteSessionModule, RemoteSession)

FAutoConsoleCommand GRemoteHostCommand(
	TEXT("remote.host"),
	TEXT("Starts a remote viewer host"),
	FConsoleCommandDelegate::CreateStatic(
		[]()
	{
		if (FRemoteSessionModule* Viewer = FModuleManager::LoadModulePtr<FRemoteSessionModule>("RemoteSession"))
		{
			Viewer->InitHost();
		}
	})
);

FAutoConsoleCommand GRemoteDisconnectCommand(
	TEXT("remote.disconnect"),
	TEXT("Disconnect remote viewer"),
	FConsoleCommandDelegate::CreateStatic(
		[]()
	{
		if (FRemoteSessionModule* Viewer = FModuleManager::LoadModulePtr<FRemoteSessionModule>("RemoteSession"))
		{
			//Viewer->StopClient();
			Viewer->StopHost();
		}
	})
);

FAutoConsoleCommand GRemoteAutoPIECommand(
	TEXT("remote.autopie"),
	TEXT("enables remote with pie"),
	FConsoleCommandDelegate::CreateStatic(
		[]()
	{
		if (FRemoteSessionModule* Viewer = FModuleManager::LoadModulePtr<FRemoteSessionModule>("RemoteSession"))
		{
			Viewer->SetAutoStartWithPIE(true);
		}
	})
);

#undef LOCTEXT_NAMESPACE
