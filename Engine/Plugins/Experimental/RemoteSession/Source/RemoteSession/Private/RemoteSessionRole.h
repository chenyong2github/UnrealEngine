// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRemoteSessionRole.h"
#include "BackChannel/Protocol/OSC/BackChannelOSCConnection.h"
#include "HAL/CriticalSection.h"

#include "Tickable.h"

class FBackChannelOSCConnection;
enum class ERemoteSessionChannelMode;

class FRemoteSessionRole : public IRemoteSessionUnmanagedRole, FRunnable
{
protected:
	enum class ConnectionState
	{
		Unknown,
		Disconnected,
		UnversionedConnection,
		EstablishingVersion,
		Connected
	};

	const TCHAR* LexToString(ConnectionState InState)
	{
		switch (InState)
		{
		case ConnectionState::Unknown:
			return TEXT("Unknown");
		case ConnectionState::Disconnected:
			return TEXT("Disconnected");
		case ConnectionState::UnversionedConnection:
			return TEXT("UnversionedConnection");
		case ConnectionState::EstablishingVersion:
			return TEXT("EstablishingVersion");
		case ConnectionState::Connected:
			return TEXT("Connected");
		default:
			check(false);
			return TEXT("Unknown");
		}
	}

	const TCHAR* kLegacyVersionEndPoint = TEXT("/Version");
	const TCHAR* kLegacyChannelSelectionEndPoint = TEXT("/ChannelSelection");

	const TCHAR* kHelloEndPoint = TEXT("/RS.Hello");
	const TCHAR* kChannelListEndPoint = TEXT("/RS.ChannelList");
	const TCHAR* kChangeChannelEndPoint = TEXT("/RS.ChangeChannel");

public:

	FRemoteSessionRole();
	virtual ~FRemoteSessionRole();

	virtual void Close() override;
	
	virtual void CloseWithError(const FString& Message) override;

	virtual bool IsConnected() const override;
	
	virtual bool HasError() const override;
	
	virtual FString GetErrorMessage() const override;

	virtual void Tick( float DeltaTime ) override;

	virtual void RegisterChannelChangeDelegate(FOnRemoteSessionChannelChange InDelegate) override;
	virtual void UnregisterChannelChangeDelegate(void* UserObject) override;

	virtual TSharedPtr<IRemoteSessionChannel> GetChannel(const TCHAR* Type) override;

	void			SetReceiveInBackground(bool bValue);

protected:

	void			StartBackgroundThread();
	void			StopBackgroundThread();

	uint32			Run();
	
	void			CreateOSCConnection(TSharedRef<IBackChannelSocketConnection> InConnection);
	
	void			SendLegacyVersionCheck();
	void 			OnReceiveLegacyVersion(IBackChannelPacket& Message);

	void			SendHello();
	void 			OnReceiveHello(IBackChannelPacket& Message);
		
	void 			CreateChannels(const TArray<FRemoteSessionChannelInfo>& Channels);
	void 			CreateChannel(const FRemoteSessionChannelInfo& Channel);	
	
	void	AddChannel(const TSharedPtr<IRemoteSessionChannel>& InChannel);
	void	ClearChannels();
	

	/* Queues the next state to be processed on the next tick. It's an error to call this when there is another state pending */
	void			SetPendingState(const ConnectionState InState);

	/*
		Called from the tick loop to perform any state changes. When called GetCurrentState() will return the current state. If the function 
		returnstrue  CurrentState will be set to IncomingState. If not the connection will be disconnected
	*/
	virtual bool	ProcessStateChange(const ConnectionState NewState, const ConnectionState OldState) = 0;

	/* Returns the current processed state */
	ConnectionState GetCurrentState(void) const { return CurrentState; }

	bool			IsStateCurrentOrPending(ConnectionState InState) const { return CurrentState == InState || PendingState == InState; }

	bool			IsLegacyConnection() const;

protected:

	mutable FCriticalSection			CriticalSectionForMainThread;

	TSharedPtr<IBackChannelSocketConnection>	Connection;

	TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> OSCConnection;

	TArray<FOnRemoteSessionChannelChange> ChangeDelegates;

private:

	FString					ErrorMessage;
	
	TArray<TSharedPtr<IRemoteSessionChannel>> Channels;
	FThreadSafeBool			ThreadExitRequested;
	FThreadSafeBool			ThreadRunning;

	ConnectionState			CurrentState = ConnectionState::Disconnected;
	ConnectionState			PendingState = ConnectionState::Unknown;


	FString					RemoteVersion;
};
