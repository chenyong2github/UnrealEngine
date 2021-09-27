// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SignallingServerConnection.h"
#include "ProtocolDefs.h"
#include "Templates/SharedPointer.h"
#include "RHI.h"
#include "HAL/Thread.h"
#include "IPixelStreamingSessions.h"
#include "ThreadSafePlayerSessions.h"
#include "PixelStreamingVideoSources.h"


class FVideoCapturer;
class FPlayerSession;
class FPixelStreamingVideoEncoderFactory;

class FStreamer : public FSignallingServerConnectionObserver, public IPixelStreamingSessions
{
public:
	static bool CheckPlatformCompatibility();

	explicit FStreamer(const FString& SignallingServerUrl, const FString& StreamerId);
	virtual ~FStreamer() override;

	void SendPlayerMessage(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor);
	void SendFreezeFrame(const TArray64<uint8>& JpegBytes);
	void SendUnfreezeFrame();
	void ForceKeyFrame();

	bool IsStreaming() const { return bStreamingStarted; }
	void SetStreamingStarted(bool started) { bStreamingStarted = started; }
	FSignallingServerConnection* GetSignallingServerConnection() const { return SignallingServerConnection.Get(); }
	FPixelStreamingVideoEncoderFactory* GetVideoEncoderFactory() const { return VideoEncoderFactory; }

	// data coming from the engine
	void OnFrameBufferReady(const FTexture2DRHIRef& FrameBuffer);

	// Begin IPixelStreamingSessions interface.
	virtual int GetNumPlayers() const override;
	virtual IPixelStreamingAudioSink* GetAudioSink(FPlayerId PlayerId) const override;
	virtual IPixelStreamingAudioSink* GetUnlistenedAudioSink() const override;
	virtual bool IsQualityController(FPlayerId PlayerId) const override;
	virtual void SetQualityController(FPlayerId PlayerId) override;
	virtual bool SendMessage(FPlayerId PlayerId, PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const override;
	virtual void SendLatestQP(FPlayerId PlayerId, int LatestQP) const override;
	virtual void SendFreezeFrameTo(FPlayerId PlayerId, const TArray64<uint8>& JpegBytes) const override;
	// End IPixelStreamingSessions interface.

	void SendCachedFreezeFrameTo(FPlayerId PlayerId) const;

	void SendLatestQPAllPlayers() const;

private:

	webrtc::AudioProcessing* SetupAudioProcessingModule();

	// Procedure for WebRTC inter-thread communication
	void StartWebRtcSignallingThread();
	void ConnectToSignallingServer();

	// ISignallingServerConnectionObserver impl
	virtual void OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config) override;
	virtual void OnOffer(FPlayerId PlayerId, TUniquePtr<webrtc::SessionDescriptionInterface> Sdp) override;
	virtual void OnRemoteIceCandidate(FPlayerId PlayerId, const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp) override;
	virtual void OnPlayerDisconnected(FPlayerId PlayerId) override;
	virtual void OnSignallingServerDisconnected() override;

	// own methods
	void ModifyAudioTransceiverDirection(webrtc::PeerConnectionInterface* PeerConnection);
	void DeletePlayerSession(FPlayerId PlayerId);
	void DeleteAllPlayerSessions();
	void AddStreams(FPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection);
	void SetupVideoTrack(FPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection, FString const VideoStreamId, FString const VideoTrackLabel);
	void SetupAudioTrack(webrtc::PeerConnectionInterface* PeerConnection, FString const AudioStreamId, FString const AudioTrackLabel);
	void HandleOffer(FPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection, TUniquePtr<webrtc::SessionDescriptionInterface> Sdp);
	void OnDataChannelOpen(FPlayerId PlayerId, webrtc::DataChannelInterface* DataChannel);
	void OnQualityControllerChanged(FPlayerId PlayerId);
	void PostPlayerDeleted(FPlayerId PlayerId);

private:
	FString SignallingServerUrl;
	FString StreamerId;

	TUniquePtr<rtc::Thread> WebRtcSignallingThread;

	TUniquePtr<FSignallingServerConnection> SignallingServerConnection;
	double LastSignallingServerConnectionAttemptTimestamp = 0;

	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory;
	webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig;

	FPixelStreamingVideoEncoderFactory* VideoEncoderFactory;
	FPixelStreamingVideoSources VideoSources;
	rtc::scoped_refptr<webrtc::AudioSourceInterface> AudioSource;
	cricket::AudioOptions AudioSourceOptions;
	
	// When we send a freeze frame we retain the data so we send freeze frame to new peers if they join during a freeze frame.
	TArray64<uint8> CachedJpegBytes;

	FThreadSafeBool bStreamingStarted = false;

	FThreadSafePlayerSessions PlayerSessions;
};

