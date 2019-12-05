// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"
#include "SignallingServerConnection.h"
#include "ProtocolDefs.h"

#include "HAL/ThreadSafeBool.h"

class FRenderTarget;
class IFileHandle;
class FSocket;
struct ID3D11Device;
class FThread;

class FVideoCapturer;
class FPlayerSession;
class FVideoEncoderFactory;

class FStreamer:
	public FSignallingServerConnectionObserver
{
public:
	static bool CheckPlatformCompatibility();

	explicit FStreamer(const FString& SignallingServerUrl);
	~FStreamer() override;

	// data coming from the engine
	void OnFrameBufferReady(const FTexture2DRHIRef& FrameBuffer);

	void SendPlayerMessage(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor);

	void SendFreezeFrame(const TArray<uint8>& JpegBytes);
	void SendUnfreezeFrame();

private:
	// window procedure for WebRTC inter-thread communication
	void WebRtcSignallingThreadFunc();

	void ConnectToSignallingServer();

	// ISignallingServerConnectionObserver impl
	void OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config) override;
	void OnOffer(FPlayerId PlayerId, TUniquePtr<webrtc::SessionDescriptionInterface> Sdp) override;
	void OnRemoteIceCandidate(FPlayerId PlayerId, TUniquePtr<webrtc::IceCandidateInterface> Candidate) override;
	void OnPlayerDisconnected(FPlayerId PlayerId) override;
	void OnSignallingServerDisconnected() override;

	// own methods
	void CreatePlayerSession(FPlayerId PlayerId);
	void DeletePlayerSession(FPlayerId PlayerId);
	void DeleteAllPlayerSessions();
	FPlayerSession* GetPlayerSession(FPlayerId PlayerId);

	void AddStreams(FPlayerId PlayerId);

	void OnQualityOwnership(FPlayerId PlayerId);

	void SendResponse(const FString& Descriptor);
	void SendCachedFreezeFrameTo(FPlayerSession& Player);

	friend FPlayerSession;

private:
	FString SignallingServerUrl;

	FVideoCapturer* VideoCapturer = nullptr;
	FVideoEncoderFactory* VideoEncoderFactory = nullptr;
	std::unique_ptr<FVideoEncoderFactory> VideoEncoderFactoryStrong;

	// a single instance of the actual hardware encoder is used for multiple streams/players to provide multicasting functionality
	// for multi-session unicast implementation each instance of `FVideoEncoder` will have own instance of hardware encoder.
	FHWEncoderDetails HWEncoderDetails;

	TUniquePtr<FThread> WebRtcSignallingThread;
	DWORD WebRtcSignallingThreadId = 0;

	TUniquePtr<FSignallingServerConnection> SignallingServerConnection;
	double LastSignallingServerConnectionAttemptTimestamp = 0;

	TMap<FPlayerId, TUniquePtr<FPlayerSession>> Players;
	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory;
	webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig;

	bool bPlanB = false;

	// These are used only if using UnifiedPlan semantics
	rtc::scoped_refptr<webrtc::AudioTrackInterface> AudioTrack;
	rtc::scoped_refptr<webrtc::VideoTrackInterface> VideoTrack;
	// This is only used if using PlanB semantics
	TMap<FString, rtc::scoped_refptr<webrtc::MediaStreamInterface>> Streams;

	// When we send a freeze frame we retain the data to handle connection
	// scenarios.
	TArray<uint8> CachedJpegBytes;

	FThreadSafeBool bStreamingStarted = false;
};

