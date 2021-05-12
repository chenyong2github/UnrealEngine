// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SignallingServerConnection.h"
#include "ProtocolDefs.h"

class FVideoCapturer;
class FPlayerSession;
class FPixelStreamingVideoEncoderFactory;

class FStreamer : public FSignallingServerConnectionObserver
{
public:
	static bool CheckPlatformCompatibility();

	explicit FStreamer(const FString& SignallingServerUrl);
	virtual ~FStreamer() override;

	void SendPlayerMessage(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor);
	void SendFreezeFrame(const TArray64<uint8>& JpegBytes);
	void SendCachedFreezeFrameTo(FPlayerSession& Player); 
	void SendUnfreezeFrame();

	void SetStreamingStarted(bool started) { bStreamingStarted = started; }
	FSignallingServerConnection* GetSignallingServerConnection() const { return SignallingServerConnection.Get(); }
	FPixelStreamingVideoEncoderFactory* GetVideoEncoderFactory() const { return VideoEncoderFactory; }

	void OnQualityOwnership(FPlayerId PlayerId);

	// data coming from the engine
	void OnFrameBufferReady(const FTexture2DRHIRef& FrameBuffer);

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
	void SendVideoEncoderQP();

private:
	FString SignallingServerUrl;

	#if PLATFORM_WINDOWS
	DWORD WebRtcSignallingThreadId = 0;
	TUniquePtr<FThread> WebRtcSignallingThread;
	#elif PLATFORM_LINUX
	TUniquePtr<rtc::Thread> WebRtcSignallingThread;
	#endif

	TUniquePtr<FSignallingServerConnection> SignallingServerConnection;
	double LastSignallingServerConnectionAttemptTimestamp = 0;

	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory;
	webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig;

	FPixelStreamingVideoEncoderFactory* VideoEncoderFactory;
	rtc::scoped_refptr<FVideoCapturer> VideoCapturer;
	rtc::scoped_refptr<webrtc::AudioSourceInterface> AudioSource;

	TMap<FPlayerId, TUniquePtr<FPlayerSession>> Players;
	// `Players` is modified only in WebRTC signalling thread, but can be accessed (along with contained `FPlayerSession` instances) 
	// from other threads. All `Players` modifications in WebRTC thread and all accesses in other threads should be locked.
	FCriticalSection PlayersCS;
	
	// When we send a freeze frame we retain the data to handle connection
	// scenarios.
	TArray64<uint8> CachedJpegBytes;

	FThreadSafeBool bStreamingStarted = false;
};

