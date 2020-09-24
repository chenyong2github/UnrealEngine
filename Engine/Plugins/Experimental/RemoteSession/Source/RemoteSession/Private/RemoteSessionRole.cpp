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

const TCHAR* LexToString(ERemoteSessionChannelMode InMode)
{
	switch (InMode)
	{
	case ERemoteSessionChannelMode::Unknown:
		return TEXT("Unknown");
	case ERemoteSessionChannelMode::Read:
		return TEXT("Read");
	case ERemoteSessionChannelMode::Write:
		return TEXT("Write");
	default:
		check(false);
		return TEXT("Unknown");
	}
}

void LexFromString(ERemoteSessionChannelMode& Value, const TCHAR* String)
{
	Value = ERemoteSessionChannelMode::Unknown;

	for (int i = 0; i < (int)ERemoteSessionChannelMode::MaxValue; ++i)
	{
		if (FCString::Stricmp(LexToString((ERemoteSessionChannelMode)i), String) == 0)
		{
			Value = (ERemoteSessionChannelMode)i;
			return;
		}
	}
}

FRemoteSessionRole::FRemoteSessionRole()
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
	CurrentState = ConnectionState::Disconnected;
	PendingState = ConnectionState::Unknown;
	RemoteVersion = TEXT("");
	FBackChannelOSCMessage::SetLegacyMode(false);
}

void FRemoteSessionRole::CloseWithError(const FString& Message)
{
	FScopeLock Tmp(&CriticalSectionForMainThread);
	ErrorMessage = Message;
	Close();
}

void FRemoteSessionRole::Tick(float DeltaTime)
{
	if (PendingState != ConnectionState::Unknown)
	{
		UE_LOG(LogRemoteSession, Log, TEXT("Processing change from %s to %s"), LexToString(CurrentState), LexToString(PendingState));
		// pass this in so code can know where we came from if needed
		ConnectionState OldState = CurrentState;
		CurrentState = PendingState;
		PendingState = ConnectionState::Unknown;
		if (ProcessStateChange(CurrentState, OldState))
		{
			UE_LOG(LogRemoteSession, Log, TEXT("Changed state to %s"), LexToString(CurrentState));

			if (CurrentState == ConnectionState::Connected)
			{
				UE_LOG(LogRemoteSession, Log, TEXT("Starting OSC receive thread for future messages"));
				OSCConnection->StartReceiveThread();
			}
		}
		else
		{
			CloseWithError(FString::Printf(TEXT("State change failed! Closing connection"), CurrentState, OldState));
		}
	}

	bool DidHaveConnection = OSCConnection.IsValid() && !OSCConnection->IsConnected();

	bool HaveLowLevelConnection = OSCConnection.IsValid() && OSCConnection->IsConnected();

	if (HaveLowLevelConnection)
	{
		if (GetCurrentState() == ConnectionState::Disconnected)
		{
			SetPendingState(ConnectionState::UnversionedConnection);
		}
		else
		{
			if (ThreadRunning == false && OSCConnection->IsThreaded() == false)
			{
				OSCConnection->ReceivePackets();
			}

			if (GetCurrentState() == ConnectionState::Connected)
			{
				for (auto& Channel : Channels)
				{
					Channel->Tick(DeltaTime);
				}
			}
		}
	}
	else if (DidHaveConnection)
	{
		UE_LOG(LogRemoteSession, Warning, TEXT("Connection %s has disconnected."), *OSCConnection->GetDescription());
		OSCConnection = nullptr;
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

void FRemoteSessionRole::CreateOSCConnection(TSharedRef<IBackChannelSocketConnection> InConnection)
{
	OSCConnection = MakeShareable(new FBackChannelOSCConnection(InConnection));

	SetPendingState(ConnectionState::UnversionedConnection);

	const URemoteSessionSettings* Settings = GetDefault<URemoteSessionSettings>();
	OSCConnection->SetConnectionTimeout(Settings->ConnectionTimeout, Settings->ConnectionTimeoutWhenDebugging);
}

bool FRemoteSessionRole::IsLegacyConnection() const
{
	return RemoteVersion == IRemoteSessionModule::GetLastSupportedVersion();
}

void FRemoteSessionRole::SendLegacyVersionCheck()
{
	if (ensureMsgf(GetCurrentState() == ConnectionState::UnversionedConnection,
		TEXT("Can only send version check in an unversioned state. Current State is %s"),
		LexToString(GetCurrentState())))
	{
		TBackChannelSharedPtr<IBackChannelPacket> Packet = OSCConnection->CreatePacket();

		Packet->SetPath(kLegacyVersionEndPoint);
		Packet->Write(TEXT("Version"), IRemoteSessionModule::GetLastSupportedVersion());

		// now ask the client to start these channels
		//FBackChannelOSCMessage Msg(TEXT("/Version"));
		//Msg.Write(TEXT("Version"), FString(GetVersion()));

		OSCConnection->SendPacket(Packet);
	}
}

void FRemoteSessionRole::OnReceiveLegacyVersion(IBackChannelPacket& Message)
{
	if (IsStateCurrentOrPending(ConnectionState::Connected))
	{
		UE_LOG(LogRemoteSession, Log, TEXT("Already connected with new protocol. Ignoring legacy connection message."));
		return;
	}

	FString VersionString;
	Message.Read(TEXT("Version"), VersionString);

	FString VersionErrorMessage;

	if (VersionString.Len() == 0)
	{
		VersionErrorMessage = TEXT("FRemoteSessionRole: Failed to read version string");
	}

	if (VersionString != IRemoteSessionModule::GetLocalVersion())
	{
		if (VersionString == IRemoteSessionModule::GetLastSupportedVersion())
		{
			UE_LOG(LogRemoteSession, Warning, TEXT("Detected legacy version %s. Setting compatibility options."), *VersionString);
			RemoteVersion = VersionString;
		}
		else
		{
			VersionErrorMessage = FString::Printf(TEXT("FRemoteSessionRole: Version mismatch. Local=%s, Remote=%s"), *IRemoteSessionModule::GetLocalVersion(), *VersionString);
		}
	}
	else
	{
		// this path should not be possible..
		if (ensureMsgf(false, TEXT("Received new protocol version through legacy handshake!")))
		{
			RemoteVersion = VersionString;
		}
	}

	if (VersionErrorMessage.Len() > 0)
	{
		UE_LOG(LogRemoteSession, Error, TEXT("%s"), *VersionErrorMessage);
		UE_LOG(LogRemoteSession, Log, TEXT("FRemoteSessionRole: Closing connection due to version mismatch"));
		CloseWithError(VersionErrorMessage);
	}
	else
	{
		SetPendingState(ConnectionState::Connected);
		FBackChannelOSCMessage::SetLegacyMode(true);
	}
}

void FRemoteSessionRole::SendHello()
{
	if (ensureMsgf(GetCurrentState() == ConnectionState::UnversionedConnection,
		TEXT("Can only send version check in an unversioned state. Current State is %s"),
		LexToString(GetCurrentState())))
	{
		TBackChannelSharedPtr<IBackChannelPacket> Packet = OSCConnection->CreatePacket();

		Packet->SetPath(kHelloEndPoint);
		Packet->Write(TEXT("Version"), IRemoteSessionModule::GetLocalVersion());
		OSCConnection->SendPacket(Packet);
	}
}

void FRemoteSessionRole::OnReceiveHello(IBackChannelPacket& Message)
{
	if (IsStateCurrentOrPending(ConnectionState::Connected) || IsLegacyConnection())
	{
		UE_LOG(LogRemoteSession, Log, TEXT("Received Hello to established legacy connection. Switching to new protocol"));
		RemoteVersion = TEXT("");
		FBackChannelOSCMessage::SetLegacyMode(false);
	}

	// Read the version
	FString VersionString;
	FString VersionErrorMessage;

	Message.Read(TEXT("Version"), VersionString);

	if (VersionString.Len() == 0)
	{
		VersionErrorMessage = TEXT("FRemoteSessionRole: Failed to read version string");
	}

	// No compatibility checks beyond this new version yet
	if (VersionString != IRemoteSessionModule::GetLocalVersion())
	{
		VersionErrorMessage = FString::Printf(TEXT("FRemoteSessionRole: Version mismatch. Local=%s, Remote=%s"), *IRemoteSessionModule::GetLocalVersion(), *VersionString);
	}
	else
	{
		RemoteVersion = VersionString;
	}

	if (VersionErrorMessage.Len() > 0)
	{
		UE_LOG(LogRemoteSession, Error, TEXT("%s"), *VersionErrorMessage);
		UE_LOG(LogRemoteSession, Log, TEXT("FRemoteSessionRole: Closing connection due to version mismatch"));
		CloseWithError(VersionErrorMessage);
	}
	else
	{
		SetPendingState(ConnectionState::Connected);
	}
}

void FRemoteSessionRole::RegisterChannelChangeDelegate(FOnRemoteSessionChannelChange InDelegate)
{
	ChangeDelegates.Add(InDelegate);
}

void FRemoteSessionRole::UnregisterChannelChangeDelegate(void* UserObject)
{
	ChangeDelegates.RemoveAll([UserObject](FOnRemoteSessionChannelChange& Delegate) {
		return Delegate.IsBoundToObject(UserObject);
	});
}


void FRemoteSessionRole::CreateChannel(const FRemoteSessionChannelInfo& InChannel)
{
	TSharedPtr<IRemoteSessionChannel> NewChannel;
	IRemoteSessionModule& RemoteSession = FModuleManager::GetModuleChecked<IRemoteSessionModule>("RemoteSession");

	NewChannel = FRemoteSessionChannelRegistry::Get().CreateChannel(*InChannel.Type, InChannel.Mode, OSCConnection);

	if (NewChannel.IsValid())
	{
		UE_LOG(LogRemoteSession, Log, TEXT("Created Channel %s with mode %d"), *InChannel.Type, (int32)InChannel.Mode);
		Channels.Add(NewChannel);

		for (auto& Delegate : ChangeDelegates)
		{
			Delegate.ExecuteIfBound(this, NewChannel, ERemoteSessionChannelChange::Created);
		}
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
		const TCHAR* ItemType = Item->GetType();
		return FCString::Stricmp(ItemType, InType) == 0;
	});

	if (FoundChannel)
	{
		Channel = *FoundChannel;
	}

	return Channel;
}

/* Queues the next state to be processed on the next tick. It's an error to call this when there is another state pending */
void FRemoteSessionRole::SetPendingState(const ConnectionState InState)
{
	checkf(PendingState == ConnectionState::Unknown, TEXT("PendingState must be unknown when SetPendingState is called"));
	PendingState = InState;
}

