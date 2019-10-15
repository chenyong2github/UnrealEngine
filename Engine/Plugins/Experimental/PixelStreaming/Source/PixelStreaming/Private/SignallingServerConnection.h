// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"

#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "Containers/UnrealString.h"
#include "Containers/Map.h"
#include "DOM/JsonObject.h"
#include "Delegates/IDelegateInstance.h"

class IWebSocket;

// callback interface for `FSignallingServerConnection`
class FSignallingServerConnectionObserver
{
public:
	virtual ~FSignallingServerConnectionObserver()
	{}

	virtual void OnSignallingServerDisconnected() = 0;
	virtual void OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config) = 0;

	// Streamer-only
	virtual void OnOffer(uint32 PlayerId, TUniquePtr<webrtc::SessionDescriptionInterface> Sdp)
	{ unimplemented(); }
	virtual void OnRemoteIceCandidate(uint32 PlayerId, TUniquePtr<webrtc::IceCandidateInterface> Candidate)
	{ unimplemented(); }
	virtual void OnPlayerDisconnected(uint32 PlayerId)
	{ unimplemented(); }

	// Player-only
	virtual void OnAnswer(TUniquePtr<webrtc::SessionDescriptionInterface> Sdp)
	{ unimplemented(); }
	virtual void OnRemoteIceCandidate(TUniquePtr<webrtc::IceCandidateInterface> Candidate)
	{ unimplemented(); }
	virtual void OnPlayerCount(uint32 PlayerId)
	{ unimplemented(); }
};

class FSignallingServerConnection final
{
public:
	explicit FSignallingServerConnection(const FString& Url, FSignallingServerConnectionObserver& Observer);
	~FSignallingServerConnection();

	void SendOffer(const webrtc::SessionDescriptionInterface& SDP);
	void SendAnswer(uint32 PlayerId, const webrtc::SessionDescriptionInterface& SDP);
	void SendIceCandidate(const webrtc::IceCandidateInterface& IceCandidate);
	void SendIceCandidate(uint32 PlayerId, const webrtc::IceCandidateInterface& IceCandidate);
	void SendDisconnectPlayer(uint32 PlayerId, const FString& Reason);

private:
	void OnConnected();
	void OnConnectionError(const FString& Error);
	void OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void OnMessage(const FString& Msg);

	using FJsonObjectPtr = TSharedPtr<FJsonObject>;

	void OnConfig(const FJsonObjectPtr& Json);
	void OnSessionDescription(const FJsonObjectPtr& Json);
	void OnStreamerIceCandidate(const FJsonObjectPtr& Json);
	void OnPlayerIceCandidate(const FJsonObjectPtr& Json);
	void OnPlayerCount(const FJsonObjectPtr& Json);
	void OnPlayerDisconnected(const FJsonObjectPtr& Json);

private:
	FSignallingServerConnectionObserver& Observer;

	FDelegateHandle OnConnectedHandle;
	FDelegateHandle OnConnectionErrorHandle;
	FDelegateHandle OnClosedHandle;
	FDelegateHandle OnMessageHandle;

	TSharedPtr<IWebSocket> WS;
};
