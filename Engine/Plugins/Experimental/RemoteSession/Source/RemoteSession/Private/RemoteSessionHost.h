// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RemoteSessionRole.h"

class IBackChannelSocketConnection;
class FRecordingMessageHandler;
class FFrameGrabber;
class IImageWrapper;
class FRemoteSessionInputChannel;


class FRemoteSessionHost : public FRemoteSessionRole, public TSharedFromThis<FRemoteSessionHost>
{
public:

	FRemoteSessionHost(TArray<FRemoteSessionChannelInfo> SupportedChannels);
	~FRemoteSessionHost();

	virtual void Close() override;

	bool StartListening(const uint16 Port);

	void SetScreenSharing(const bool bEnabled);

	virtual void Tick(float DeltaTime) override;

protected:

	bool 			ProcessStateChange(const ConnectionState NewState, const ConnectionState OldState) override;


	virtual void 	BindEndpoints(TBackChannelSharedPtr<IBackChannelConnection> InConnection);

	void			SendChannelListToConnection();
	
	bool			ProcessIncomingConnection(TSharedRef<IBackChannelSocketConnection> NewConnection);

	void			OnChangeChannel(IBackChannelPacket& Message);


	TSharedPtr<IBackChannelSocketConnection> Listener;

	TArray<FRemoteSessionChannelInfo> SupportedChannels;

	/** Saved information about the editor and viewport we possessed, so we can restore it after exiting VR mode */
	float SavedEditorDragTriggerDistance;

	/** Host's TCP port */
	uint16 HostTCPPort;

	/** True if the host TCP socket is connected*/
	bool IsListenerConnected;
};
