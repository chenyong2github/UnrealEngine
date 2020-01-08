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


void FRemoteSessionModule::SetAutoStartWithPIE(bool bEnable)
{
	bAutoHostWithPIE = bEnable;
}

void FRemoteSessionModule::StartupModule()
{
	// set defaults
	DefaultPort = IRemoteSessionModule::kDefaultPort;
	bAutoHostWithPIE = true;
	bAutoHostWithGame = true;

	ReadIniSettings();

	// Add the default channel factory
	BuiltInFactory.Add(MakeShared<FRemoteSessionARCameraChannelFactoryWorker>());
	BuiltInFactory.Add(MakeShared<FRemoteSessionARSystemChannelFactoryWorker>());
	BuiltInFactory.Add(MakeShared<FRemoteSessionFrameBufferChannelFactoryWorker>()); // for deprecation before 2.24
	BuiltInFactory.Add(MakeShared<FRemoteSessionImageChannelFactoryWorker>());
	BuiltInFactory.Add(MakeShared<FRemoteSessionInputChannelFactoryWorker>());
	BuiltInFactory.Add(MakeShared<FRemoteSessionXRTrackingChannelFactoryWorker>());
	for (TSharedPtr<IRemoteSessionChannelFactoryWorker> Channel : BuiltInFactory)
	{
		FactoryWorkers.Add(Channel);
	}

	if (PLATFORM_DESKTOP 
		&& IsRunningDedicatedServer() == false 
		&& IsRunningCommandlet() == false)
	{
#if WITH_EDITOR
		PostPieDelegate = FEditorDelegates::PostPIEStarted.AddRaw(this, &FRemoteSessionModule::OnPIEStarted);
		EndPieDelegate = FEditorDelegates::EndPIE.AddRaw(this, &FRemoteSessionModule::OnPIEEnded);
#endif
		GameStartDelegate = FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FRemoteSessionModule::OnGameStarted);
	}
}

void FRemoteSessionModule::ReadIniSettings()
{
	GConfig->GetBool(TEXT("RemoteSession"), TEXT("bAutoHostWithGame"), bAutoHostWithGame, GEngineIni);
	GConfig->GetBool(TEXT("RemoteSession"), TEXT("bAutoHostWithPIE"), bAutoHostWithPIE, GEngineIni);
	GConfig->GetInt(TEXT("RemoteSession"), TEXT("HostPort"), DefaultPort, GEngineIni);

	// Query the list of channels from the hosts ini file.
	TArray<FString> ReadIniSupportedChannels;
	GConfig->GetArray(TEXT("RemoteSession"), TEXT("Channels"), ReadIniSupportedChannels, GEngineIni);

	if (ReadIniSupportedChannels.Num() == 0)
	{
		// Default to Input receive and framebuffer send
		ReadIniSupportedChannels.Add(FString::Printf(TEXT("(Name=%s,Mode=Read)"), FRemoteSessionInputChannel::StaticType()));
		ReadIniSupportedChannels.Add(FString::Printf(TEXT("(Name=%s,Mode=Write)"), FRemoteSessionFrameBufferChannelFactoryWorker::StaticType()));
		UE_LOG(LogRemoteSession, Log, TEXT("No channels specified. Defaulting to Input and Framebuffer."));
	}

	FParse::Value(FCommandLine::Get(), TEXT("remote.port="), DefaultPort);

	IniSupportedChannels.Reset();

	for (FString& Channel : ReadIniSupportedChannels)
	{
		FString ChannelName, Mode;

		Channel.TrimStartAndEndInline();

		if (Channel[0] == TEXT('('))
		{
			const TCHAR* ChannelArgs = (*Channel) + 1;

			FParse::Value(ChannelArgs, TEXT("Name="), ChannelName);
			FParse::Value(ChannelArgs, TEXT("Mode="), Mode);
		}

		if (ChannelName.Len() && Mode.Len())
		{
			ERemoteSessionChannelMode ChannelMode = Mode == TEXT("Read") ? ERemoteSessionChannelMode::Read : ERemoteSessionChannelMode::Write;
			IniSupportedChannels.Add(*ChannelName, ChannelMode);

			UE_LOG(LogRemoteSession, Log, TEXT("Will request channel %s in mode %s."), *ChannelName, *Mode);
		}
		else
		{
			UE_LOG(LogRemoteSession, Error, TEXT("Unrecognized channel syntax '%s'. Should be ChannelType,r or ChannelType,s"), *Channel);
		}
	}

	{
		TArray<FString> IniChannelRedirectsString;
		GConfig->GetArray(TEXT("RemoteSession"), TEXT("ChannelRedirects"), IniChannelRedirectsString, GEngineIni);

		for (FString& Channel : IniChannelRedirectsString)
		{
			Channel.TrimStartAndEndInline();
			if (Channel[0] == TEXT('('))
			{
				const TCHAR* ChannelArgs = (*Channel) + 1;

				FString OldName, NewName;
				FParse::Value(ChannelArgs, TEXT("OldName="), OldName);
				FParse::Value(ChannelArgs, TEXT("NewName="), NewName);

				if (OldName.Len() && NewName.Len())
				{
					ChannelRedirects.Emplace(MoveTemp(OldName), MoveTemp(NewName));
				}
			}
		}
	}
}

void FRemoteSessionModule::AddChannelFactory(TWeakPtr<IRemoteSessionChannelFactoryWorker> Worker)
{
	FactoryWorkers.AddUnique(Worker);
}

void FRemoteSessionModule::RemoveChannelFactory(TWeakPtr<IRemoteSessionChannelFactoryWorker> Worker)
{
	FactoryWorkers.RemoveSingleSwap(Worker);
}

TSharedPtr<IRemoteSessionChannelFactoryWorker> FRemoteSessionModule::FindChannelFactoryWorker(const TCHAR* Type)
{
	for (TWeakPtr<IRemoteSessionChannelFactoryWorker> Worker : FactoryWorkers)
	{
		if (TSharedPtr<IRemoteSessionChannelFactoryWorker> Pinned = Worker.Pin())
		{
			if (FPlatformString::Stricmp(Pinned->GetType(), Type) == 0)
			{
				return Pinned;
			}
		}
	}
	return TSharedPtr<IRemoteSessionChannelFactoryWorker>();
}

void FRemoteSessionModule::SetSupportedChannels(TMap<FString, ERemoteSessionChannelMode>& InSupportedChannels)
{
	for (const auto& KP : InSupportedChannels)
	{
		AddSupportedChannel(KP.Key, KP.Value, FOnRemoteSessionChannelCreated());
	}
}

void FRemoteSessionModule::AddSupportedChannel(FString InType, ERemoteSessionChannelMode InMode)
{
	AddSupportedChannel(MoveTemp(InType), InMode, FOnRemoteSessionChannelCreated());
}

void FRemoteSessionModule::AddSupportedChannel(FString InType, ERemoteSessionChannelMode InMode, FOnRemoteSessionChannelCreated InOnCreated)
{
	if (!ProgramaticallySupportedChannels.ContainsByPredicate([&InType](const FRemoteSessionChannelInfo& Info) { return Info.Type == InType; }))
	{
		ProgramaticallySupportedChannels.Emplace(MoveTemp(InType), InMode, InOnCreated);
	}
}

void FRemoteSessionModule::ShutdownModule()
{
	BuiltInFactory.Reset();

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

void FRemoteSessionModule::OnGameStarted()
{
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

	TArray<FRemoteSessionChannelInfo> SupportedChannels = ProgramaticallySupportedChannels;
	for (const auto& KP : IniSupportedChannels)
	{
		if (!SupportedChannels.ContainsByPredicate([&KP](const FRemoteSessionChannelInfo& Other) { return KP.Key == Other.Type; }))
		{
			SupportedChannels.Emplace(KP.Key, KP.Value, FOnRemoteSessionChannelCreated());
		}
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
	bool bAllowInShipping = false;
	GConfig->GetBool(TEXT("RemoteSession"), TEXT("bAllowInShipping"), bAllowInShipping, GEngineIni);
	if (bAllowInShipping == false)
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
