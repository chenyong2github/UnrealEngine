// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SignallingServerConnection.h"
#include "ProtocolDefs.h"
#include "Templates/SharedPointer.h"
#include "RHI.h"
#include "HAL/Thread.h"
#include "IPixelStreamingSessions.h"
#include "ThreadSafePlayerSessions.h"
#include "PixelStreamingFramePump.h"
#include "PixelStreamingFrameSource.h"
#include "PixelStreamingFramePump.h"

class FVideoCapturer;
class FPlayerSession;
class FPixelStreamingVideoEncoderFactory;
class FStreamer;
class FSetSessionDescriptionObserver;

class FStreamer : public FSignallingServerConnectionObserver, public IPixelStreamingSessions
{
public:
	static bool CheckPlatformCompatibility();

	explicit FStreamer(const FString& SignallingServerUrl, const FString& StreamerId);
	virtual ~FStreamer() override;

	void SendPlayerMessage(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor);
	void SendFreezeFrame(const TArray64<uint8>& JpegBytes);
	void SendUnfreezeFrame();
	void SendFileData(TArray<uint8>& ByteData, FString& MimeType, FString& FileExtension);
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
	virtual void PollWebRTCStats() const override;
	// End IPixelStreamingSessions interface.

	void AddAnyStatChangedCallback(TWeakPtr<IPixelStreamingStatsConsumer> Callback);
	void RemoveAnyStatChangedCallback(TWeakPtr<IPixelStreamingStatsConsumer> Callback);
	void SendCachedFreezeFrameTo(FPlayerId PlayerId) const;

private:
	webrtc::PeerConnectionInterface* CreateSession(FPlayerId PlayerId, int Flags);
	webrtc::AudioProcessing* SetupAudioProcessingModule();

	// Procedure for WebRTC inter-thread communication
	void StartWebRtcSignallingThread();
	void ConnectToSignallingServer();

	// ISignallingServerConnectionObserver impl
	virtual void OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config) override;
	virtual void OnSessionDescription(FPlayerId PlayerId, webrtc::SdpType Type, const FString& Sdp) override;
	virtual void OnRemoteIceCandidate(FPlayerId PlayerId, const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp) override;
	virtual void OnPlayerConnected(FPlayerId PlayerId, int Flags) override;
	virtual void OnPlayerDisconnected(FPlayerId PlayerId) override;
	virtual void OnSignallingServerDisconnected() override;

	// own methods
	void OnOffer(FPlayerId PlayerId, const FString& Sdp);
	void ModifyAudioTransceiverDirection(webrtc::PeerConnectionInterface* PeerConnection);
	void DeletePlayerSession(FPlayerId PlayerId);
	void DeleteAllPlayerSessions();
	void AddStreams(FPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection, int Flags);
	void SetupVideoTrack(FPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection, FString const VideoStreamId, FString const VideoTrackLabel, int Flags);
	void SetupAudioTrack(webrtc::PeerConnectionInterface* PeerConnection, const FString AudioStreamId, const FString AudioTrackLabel);
	void SendAnswer(FPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection, TUniquePtr<webrtc::SessionDescriptionInterface> Sdp);
	void OnDataChannelOpen(FPlayerId PlayerId, webrtc::DataChannelInterface* DataChannel);
	void OnQualityControllerChanged(FPlayerId PlayerId);
	void PostPlayerDeleted(FPlayerId PlayerId);
	void SetLocalDescription(webrtc::PeerConnectionInterface* PeerConnection, FSetSessionDescriptionObserver* Observer, webrtc::SessionDescriptionInterface* SDP);

private:
	FString SignallingServerUrl;
	FString StreamerId;

	TUniquePtr<rtc::Thread> WebRtcSignallingThread;

	TUniquePtr<FSignallingServerConnection> SignallingServerConnection;
	double LastSignallingServerConnectionAttemptTimestamp = 0;

	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory;
	webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig;

	FPixelStreamingVideoEncoderFactory* VideoEncoderFactory;

	FPixelStreamingFrameSource FrameSource;
	FPixelStreamingFramePump FramePump;

	rtc::scoped_refptr<webrtc::AudioSourceInterface> AudioSource;
	cricket::AudioOptions AudioSourceOptions;

	// When we send a freeze frame we retain the data so we send freeze frame to new peers if they join during a freeze frame.
	TArray64<uint8> CachedJpegBytes;

	FThreadSafeBool bStreamingStarted = false;

	FThreadSafePlayerSessions PlayerSessions;
	FPixelStreamingStats Stats;

	TUniquePtr<webrtc::SessionDescriptionInterface> SFULocalDescription;
	TUniquePtr<webrtc::SessionDescriptionInterface> SFURemoteDescription;
};
