// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SignallingServerConnection.h"
#include "ProtocolDefs.h"

#include "RHI.h"
#include "HAL/Thread.h"


class FVideoCapturer;
class FPlayerSession;
class FPixelStreamingVideoEncoderFactory;

class FStreamer : public FSignallingServerConnectionObserver
{
public:
	static bool CheckPlatformCompatibility();

	explicit FStreamer(const FString& SignallingServerUrl, const FString& StreamerId);
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

	int GetNumPlayers() const;
	void GetPlayerSessions(TArray<FPlayerSession*>& OutPlayerSessions);
	FPlayerSession* GetPlayerSession(FPlayerId PlayerId);
	FPlayerSession* GetUnlistenedPlayerSession();

private:
	webrtc::AudioProcessing* SetupAudioProcessingModule();

	// Procedure for WebRTC inter-thread communication
	void StartWebRtcSignallingThread();
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
	void AddStreams(FPlayerId PlayerId);
	void SetupVideoTrack(FPlayerSession* Session, FString const VideoStreamId, FString const VideoTrackLabel);
	void SetupAudioTrack(FPlayerSession* Session, FString const AudioStreamId, FString const AudioTrackLabel);

private:
	FString SignallingServerUrl;
	FString StreamerId;

	TUniquePtr<rtc::Thread> WebRtcSignallingThread;

	TUniquePtr<FSignallingServerConnection> SignallingServerConnection;
	double LastSignallingServerConnectionAttemptTimestamp = 0;

	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory;
	webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig;

	FPixelStreamingVideoEncoderFactory* VideoEncoderFactory;
	rtc::scoped_refptr<FVideoCapturer> VideoSource;
	rtc::scoped_refptr<webrtc::AudioSourceInterface> AudioSource;
	cricket::AudioOptions AudioSourceOptions;

	TMap<FPlayerId, TUniquePtr<FPlayerSession>> Players;
	// `Players` is modified only in WebRTC signalling thread, but can be accessed (along with contained `FPlayerSession` instances) 
	// from other threads. All `Players` modifications in WebRTC thread and all accesses in other threads should be locked.
	FCriticalSection PlayersCS;
	
	// When we send a freeze frame we retain the data to handle connection
	// scenarios.
	TArray64<uint8> CachedJpegBytes;

	FThreadSafeBool bStreamingStarted = false;
};

