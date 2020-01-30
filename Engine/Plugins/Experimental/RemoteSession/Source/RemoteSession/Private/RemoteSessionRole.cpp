// Copyright Epic Games, Inc. All Rights Reserved.


#include "RemoteSessionRole.h"
#include "RemoteSession.h"
#include "BackChannel/Protocol/OSC/BackChannelOSCMessage.h"
#include "Channels/RemoteSessionInputChannel.h"
#include "Channels/RemoteSessionXRTrackingChannel.h"
#include "Channels/RemoteSessionARCameraChannel.h"
#include "Channels/RemoteSessionARSystemChannel.h"
#include "Channels/RemoteSessionFrameBufferChannel.h"

#include "ARBlueprintLibrary.h"
#include "Async/Async.h"
#include "GeneralProjectSettings.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY(LogRemoteSession);

FRemoteSessionRole::FRemoteSessionRole()
	: bShouldCreateChannels(false)
{
}

FRemoteSessionRole::~FRemoteSessionRole()
{
	Close();
}

void FRemoteSessionRole::Close()
{
	// order is specific since OSC uses the connection, and
	// dispatches to channels
	StopBackgroundThread();
	OSCConnection = nullptr;
	Connection = nullptr;
	ClearChannels();
}

void FRemoteSessionRole::CloseWithError(const FString& Message)
{
	FScopeLock Tmp(&CriticalSectionForMainThread);
	ErrorMessage = Message;
	Close();
}

void FRemoteSessionRole::Tick(float DeltaTime)
{
	if (OSCConnection.IsValid())
	{
		if (OSCConnection->IsConnected())
		{
			if (ThreadRunning == false && OSCConnection->IsThreaded() == false)
			{
				OSCConnection->ReceivePackets();
			}

			if (bShouldCreateChannels)
			{
				bShouldCreateChannels = false;
				UE_LOG(LogRemoteSession, Log, TEXT("FRemoteSessionRole: Binding endpoints and creating channels"));
				OnBindEndpoints();
				OnCreateChannels();
			}

			for (auto& Channel : Channels)
			{
				Channel->Tick(DeltaTime);
			}
		}
		else
		{
			UE_LOG(LogRemoteSession, Warning, TEXT("Connection %s has disconnected."), *OSCConnection->GetDescription());
			OSCConnection = nullptr;
		}
	}
}

void FRemoteSessionRole::SetReceiveInBackground(bool bValue)
{
	if (bValue && !ThreadRunning)
	{
		StartBackgroundThread();
	}
	else if (!bValue && ThreadRunning)
	{
		StopBackgroundThread();
	}
}

void FRemoteSessionRole::StartBackgroundThread()
{
	check(ThreadRunning == false);
	ThreadExitRequested = false;
	ThreadRunning = true;

	FRunnableThread* Thread = FRunnableThread::Create(this, TEXT("RemoteSessionClientThread"), 
		1024 * 1024, 
		TPri_AboveNormal);
}

bool FRemoteSessionRole::IsConnected() const
{
	// just check this is valid, when it's actually disconnected we do some error
	// handling and clean this up
	return OSCConnection.IsValid();
}

bool FRemoteSessionRole::HasError() const
{
	FScopeLock Tmp(&CriticalSectionForMainThread);
	return ErrorMessage.Len() > 0;
}

FString FRemoteSessionRole::GetErrorMessage() const
{
	FScopeLock Tmp(&CriticalSectionForMainThread);
	return ErrorMessage;
}

uint32 FRemoteSessionRole::Run()
{
	/* Not used and likely to be removed! */
	double LastTick = FPlatformTime::Seconds();

	while (ThreadExitRequested == false)
	{
		const double DeltaTime = FPlatformTime::Seconds() - LastTick;

		if (OSCConnection.IsValid() == false || OSCConnection->IsConnected() == false)
		{
			FPlatformProcess::SleepNoStats(0);
			continue;
		}

		OSCConnection->ReceivePackets(1);
		LastTick = FPlatformTime::Seconds();
	}

	ThreadRunning = false;
	return 0;
}

void FRemoteSessionRole::StopBackgroundThread()
{
	if (ThreadRunning == false)
	{
		return;
	}

	ThreadExitRequested = true;

	while (ThreadRunning)
	{
		FPlatformProcess::SleepNoStats(0);
	}
}

void FRemoteSessionRole::CreateOSCConnection(TSharedRef<IBackChannelConnection> InConnection)
{
	OSCConnection = MakeShareable(new FBackChannelOSCConnection(InConnection));
	
	auto Delegate = FBackChannelDispatchDelegate::FDelegate::CreateRaw(this, &FRemoteSessionRole::OnVersionCheck);
	OSCConnection->AddMessageHandler(TEXT("/Version"),Delegate);
	
	Delegate = FBackChannelDispatchDelegate::FDelegate::CreateRaw(this, &FRemoteSessionRole::OnCreateChannels);
	OSCConnection->AddMessageHandler(GetChannelSelectionEndPoint(), Delegate);

	OSCConnection->StartReceiveThread();
	
	SendVersion();
}

const TCHAR* FRemoteSessionRole::GetVersion() const
{
	return REMOTE_SESSION_VERSION_STRING;
}

void FRemoteSessionRole::SendVersion()
{
	// now ask the client to start these channels
	FBackChannelOSCMessage Msg(TEXT("/Version"));
	Msg.Write(GetVersion());
	OSCConnection->SendPacket(Msg);
}

void FRemoteSessionRole::OnVersionCheck(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch)
{
	FString VersionString;
	
	Message.Read(VersionString);
	
	FString VersionErrorMessage;
	
	if (VersionString.Len() == 0)
	{
		VersionErrorMessage = TEXT("FRemoteSessionRole: Failed to read version string");
	}
	
	if (VersionString != GetVersion())
	{
		VersionErrorMessage = FString::Printf(TEXT("FRemoteSessionRole: Version mismatch. Local=%s, Remote=%s"), *GetVersion(), *VersionString);
	}
	
	if (VersionErrorMessage.Len() > 0)
	{
		UE_LOG(LogRemoteSession, Error, TEXT("%s"), *VersionErrorMessage);
		UE_LOG(LogRemoteSession, Log, TEXT("FRemoteSessionRole: Closing connection due to version mismatch"));
		CloseWithError(VersionErrorMessage);
	}
	else
	{
		bShouldCreateChannels = true;
	}
}

void FRemoteSessionRole::OnCreateChannels(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch)
{
	OnChannelSelection(Message, Dispatch);
}

void FRemoteSessionRole::OnChannelSelection(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch)
{
}

void FRemoteSessionRole::OnBindEndpoints()
{
}

void FRemoteSessionRole::OnCreateChannels()
{
}

void FRemoteSessionRole::CreateChannel(const FRemoteSessionChannelInfo& InChannel)
{
	TSharedPtr<IRemoteSessionChannel> NewChannel;
	IRemoteSessionModule& RemoteSession = FModuleManager::GetModuleChecked<IRemoteSessionModule>("RemoteSession");
	if (TSharedPtr<IRemoteSessionChannelFactoryWorker> Worker = RemoteSession.FindChannelFactoryWorker(*InChannel.Type))
	{
		NewChannel = Worker->Construct(InChannel.Mode, OSCConnection);
	}

	if (NewChannel.IsValid())
	{
		UE_LOG(LogRemoteSession, Log, TEXT("Created Channel %s with mode %d"), *InChannel.Type, (int32)InChannel.Mode);
		Channels.Add(NewChannel);
		InChannel.OnCreated.ExecuteIfBound(NewChannel, InChannel.Type, InChannel.Mode);
	}
	else
	{
		UE_LOG(LogRemoteSession, Error, TEXT("Requested Channel %s was not recognized"), *InChannel.Type);
	}
}

void FRemoteSessionRole::CreateChannels(const TArray<FRemoteSessionChannelInfo>& InChannels)
{
	ClearChannels();
	
	for (const FRemoteSessionChannelInfo& Channel : InChannels)
	{
		CreateChannel(Channel);
	}
}

void FRemoteSessionRole::AddChannel(const TSharedPtr<IRemoteSessionChannel>& InChannel)
{
	Channels.Add(InChannel);
}

void FRemoteSessionRole::ClearChannels()
{
	Channels.Empty();
}

TSharedPtr<IRemoteSessionChannel> FRemoteSessionRole::GetChannel(const TCHAR* InType)
{
	TSharedPtr<IRemoteSessionChannel> Channel;

	TSharedPtr<IRemoteSessionChannel>* FoundChannel = Channels.FindByPredicate([InType](const auto& Item) {
		return Item->GetType() == InType;
	});

	if (FoundChannel)
	{
		Channel = *FoundChannel;
	}

	return Channel;
}
