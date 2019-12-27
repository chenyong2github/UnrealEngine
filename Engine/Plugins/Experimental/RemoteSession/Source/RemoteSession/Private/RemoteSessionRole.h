// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RemoteSession/RemoteSessionRole.h"
#include "BackChannel/Protocol/OSC/BackChannelOSCConnection.h"
#include "HAL/CriticalSection.h"

#include "Tickable.h"

class FBackChannelOSCConnection;
enum class ERemoteSessionChannelMode;

class FRemoteSessionRole : public IRemoteSessionUnmanagedRole, FRunnable
{
public:

	FRemoteSessionRole();
	virtual ~FRemoteSessionRole();

	virtual void Close() override;
	
	virtual void CloseWithError(const FString& Message) override;

	virtual bool IsConnected() const override;
	
	virtual bool HasError() const override;
	
	virtual FString GetErrorMessage() const override;

	virtual void Tick( float DeltaTime ) override;

	virtual TSharedPtr<IRemoteSessionChannel> GetChannel(const TCHAR* Type) override;

	void			SetReceiveInBackground(bool bValue);

protected:

	void			StartBackgroundThread();
	void			StopBackgroundThread();

	uint32			Run();
	
	void			CreateOSCConnection(TSharedRef<IBackChannelConnection> InConnection);
	
	const TCHAR*	GetVersion() const;
	void			SendVersion();
	void 			OnVersionCheck(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch);
	void			OnCreateChannels(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch);
	
	virtual void	OnBindEndpoints();
	virtual void	OnCreateChannels();
	virtual void	OnChannelSelection(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch);
	
	void 			CreateChannels(const TArray<FRemoteSessionChannelInfo>& Channels);
	void 			CreateChannel(const FRemoteSessionChannelInfo& Channel);
	
	
	void	AddChannel(const TSharedPtr<IRemoteSessionChannel>& InChannel);
	void	ClearChannels();
	
	const TCHAR* GetChannelSelectionEndPoint() const
	{
		return TEXT("/ChannelSelection");
	}

protected:

	mutable FCriticalSection			CriticalSectionForMainThread;

	TSharedPtr<IBackChannelConnection>	Connection;

	TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> OSCConnection;

private:

	FString					ErrorMessage;
	
	TArray<TSharedPtr<IRemoteSessionChannel>> Channels;
	FThreadSafeBool			ThreadExitRequested;
	FThreadSafeBool			ThreadRunning;

	FThreadSafeBool			bShouldCreateChannels;
};
