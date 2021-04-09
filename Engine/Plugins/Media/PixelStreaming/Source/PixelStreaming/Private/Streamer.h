// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"
#include "SignallingServerConnection.h"
#include "ProtocolDefs.h"

#include "HAL/ThreadSafeBool.h"

class FRenderTarget;
class IFileHandle;
class FSocket;
class FThread;

class FVideoCapturer;
class FPlayerSession;
class FPixelStreamingVideoEncoderFactory;

namespace AVEncoder
{
class FVideoEncoderFactory;
}

class FStreamer:
	public FSignallingServerConnectionObserver
{
public:
	static bool CheckPlatformCompatibility();

	explicit FStreamer(const FString& SignallingServerUrl);
	virtual ~FStreamer() override;

	// data coming from the engine
	void OnFrameBufferReady(const FTexture2DRHIRef& FrameBuffer);

	void SendPlayerMessage(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor);

	void SendFreezeFrame(const TArray64<uint8>& JpegBytes);
	void SendUnfreezeFrame();

private:
	// window procedure for WebRTC inter-thread communication
	void WebRtcSignallingThreadFunc();

	void ConnectToSignallingServer();

	// ISignallingServerConnectionObserver impl
	virtual void OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config) override;
	virtual void OnOffer(FPlayerId PlayerId, TUniquePtr<webrtc::SessionDescriptionInterface> Sdp) override;
	virtual void OnRemoteIceCandidate(FPlayerId PlayerId, TUniquePtr<webrtc::IceCandidateInterface> Candidate) override;
	virtual void OnPlayerDisconnected(FPlayerId PlayerId) override;
	virtual void OnSignallingServerDisconnected() override;

	// own methods
	void CreatePlayerSession(FPlayerId PlayerId);
	void DeletePlayerSession(FPlayerId PlayerId);
	void DeleteAllPlayerSessions();
	FPlayerSession* GetPlayerSession(FPlayerId PlayerId);

	void AddStreams(FPlayerId PlayerId);

	void OnQualityOwnership(FPlayerId PlayerId);

	void SendResponse(const FString& Descriptor);
	void SendCachedFreezeFrameTo(FPlayerSession& Player);
	void SendVideoEncoderQP();

	friend FPlayerSession;

private:
	FString SignallingServerUrl;

	rtc::scoped_refptr<FVideoCapturer> VideoCapturer;
	FPixelStreamingVideoEncoderFactory* VideoEncoderFactory = nullptr;
	std::unique_ptr<webrtc::VideoEncoderFactory> VideoEncoderFactoryStrong;
	
	#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
	DWORD WebRtcSignallingThreadId = 0;
	TUniquePtr<FThread> WebRtcSignallingThread;
	#elif PLATFORM_LINUX
	TUniquePtr<rtc::Thread> WebRtcSignallingThread;
	#endif

	TUniquePtr<FSignallingServerConnection> SignallingServerConnection;
	double LastSignallingServerConnectionAttemptTimestamp = 0;

	TMap<FPlayerId, TUniquePtr<FPlayerSession>> Players;
	// `Players` is modified only in WebRTC signalling thread, but can be accessed (along with contained `FPlayerSession` instances) 
	// from other threads. All `Players` modifications in WebRTC thread and all accesses in other threads should be locked.
	FCriticalSection PlayersCS;
	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory;
	webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig;

	// These are used only if using UnifiedPlan semantics
	//rtc::scoped_refptr<webrtc::AudioTrackInterface> AudioTrack = nullptr;
	//rtc::scoped_refptr<webrtc::VideoTrackInterface> VideoTrack = nullptr;

	rtc::scoped_refptr<webrtc::AudioSourceInterface> AudioSource;

	// When we send a freeze frame we retain the data to handle connection
	// scenarios.
	TArray64<uint8> CachedJpegBytes;

	FThreadSafeBool bStreamingStarted = false;
};

