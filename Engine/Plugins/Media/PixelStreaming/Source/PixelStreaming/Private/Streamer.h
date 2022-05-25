// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingStreamer.h"
#include "IPixelStreamingSignallingConnectionObserver.h"
#include "PixelStreamingProtocolDefs.h"
#include "RHI.h"
#include "PlayerSessions.h"
#include "Stats.h"
#include "StreamerInputDevices.h"
#include "PixelStreamingSessionDescriptionObservers.h"
#include "Dom/JsonObject.h"

namespace UE::PixelStreaming
{
	class FPlayerSession;
	class FVideoEncoderFactorySimple;
	class FVideoEncoderFactorySimulcast;
	class FSetSessionDescriptionObserver;
	class FPoller;
	class IPlayerSession;
	class FVideoSourceGroup;

	class FStreamer : public IPixelStreamingStreamer, public IPixelStreamingSignallingConnectionObserver
	{
	public:
		FStreamer(const FString& StreamerId);
		virtual ~FStreamer();

		/** IPixelStreamingStreamer implementation */
		virtual void SetStreamFPS(int32 InFramesPerSecond) override;
		virtual void SetVideoInput(TSharedPtr<FPixelStreamingVideoInput> Input) override;
		virtual void SetTargetViewport(TSharedPtr<FSceneViewport> InTargetViewport) override;
		virtual void SetSignallingServerURL(const FString& InSignallingServerURL) override;
		virtual void StartStreaming() override;
		virtual void StopStreaming() override;
		virtual bool IsStreaming() const override { return bStreamingStarted; }
		virtual void SendPlayerMessage(Protocol::EToPlayerMsg Type, const FString& Descriptor) override;
		virtual void SetInputDevice(TSharedPtr<IPixelStreamingInputDevice> InInputDevice) override { InputDevice = InInputDevice; }
		virtual void FreezeStream(UTexture2D* Texture) override;
		virtual void UnfreezeStream() override;
		virtual void SendFileData(TArray<uint8>& ByteData, FString& MimeType, FString& FileExtension) override;
		virtual void KickPlayer(FPixelStreamingPlayerId PlayerId) override;
		IPixelStreamingAudioSink* GetPeerAudioSink(FPixelStreamingPlayerId PlayerId) override;
		IPixelStreamingAudioSink* GetUnlistenedAudioSink() override;
		FStreamingStartedEvent& OnStreamingStarted() override;
		FStreamingStoppedEvent& OnStreamingStopped() override;
		/** End IPixelStreamingStreamer implementation */
		
		/** Own methods */
		void ForceKeyFrame();
		void PushFrame();
		FPlayerSessions& GetPlayerSessions() { return PlayerSessions; }
		// TODO(Luke) hook this back up so that the Engine can change how the interface is working browser side
		void AddPlayerConfig(TSharedRef<FJsonObject>& JsonObject);		
		/** End Own methods */

	private:
		/** ISignallingServerConnectionObserver implementation */
		virtual void OnSignallingConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config) override;
		virtual void OnSignallingSessionDescription(FPixelStreamingPlayerId PlayerId, webrtc::SdpType Type, const FString& Sdp) override;
		virtual void OnSignallingRemoteIceCandidate(FPixelStreamingPlayerId PlayerId, const FString& SdpMid, int SdpMLineIndex, const FString& Sdp) override;
		virtual void OnSignallingPlayerConnected(FPixelStreamingPlayerId PlayerId, int Flags) override;
		virtual void OnSignallingPlayerDisconnected(FPixelStreamingPlayerId PlayerId) override;
		virtual void OnSignallingSFUPeerDataChannels(FPixelStreamingPlayerId SFUId, FPixelStreamingPlayerId PlayerId, int32 SendStreamId, int32 RecvStreamId) override;
		virtual void OnSignallingConnected() override;
		virtual void OnSignallingDisconnected(int32 StatusCode, const FString& Reason, bool bWasClean) override;
		virtual void OnSignallingError(const FString& ErrorMsg) override;
		/** End ISignallingServerConnectionObserver implementation */

		/** Own methods */
		void SendFreezeFrame(TArray<FColor> RawData, const FIntRect& Rect);
		void SendCachedFreezeFrameTo(FPixelStreamingPlayerId PlayerId) const;
		TSharedPtr<IPlayerSession> CreateSession(FPixelStreamingPlayerId PlayerId, int Flags);
		void OnOffer(FPixelStreamingPlayerId PlayerId, const FString& Sdp);
		void ModifyTransceivers(std::vector<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> Transceivers, int Flags);
		void MungeRemoteSDP(cricket::SessionDescription* SessionDescription);
		void MungeLocalSDP(cricket::SessionDescription* SessionDescription);
		void DeletePlayerSession(FPixelStreamingPlayerId PlayerId);
		void DeleteAllPlayerSessions();
		void AddStreams(TSharedPtr<IPlayerSession> Session, int Flags);
		void SetupVideoTrack(TSharedPtr<IPlayerSession> Session, FString const VideoStreamId, FString const VideoTrackLabel, int Flags);
		void SetupAudioTrack(TSharedPtr<IPlayerSession> Session, const FString AudioStreamId, const FString AudioTrackLabel, int Flags);
		std::vector<webrtc::RtpEncodingParameters> CreateRTPEncodingParams(int Flags);
		void SendAnswer(TSharedPtr<IPlayerSession> Session, TUniquePtr<webrtc::SessionDescriptionInterface> Sdp);
		void OnDataChannelOpen(FPixelStreamingPlayerId PlayerId, webrtc::DataChannelInterface* DataChannel);
		void SetLocalDescription(webrtc::PeerConnectionInterface* PeerConnection, FPixelStreamingSetSessionDescriptionObserver* Observer, webrtc::SessionDescriptionInterface* SDP);
		FString GetAudioStreamID() const;
		FString GetVideoStreamID() const;
		rtc::scoped_refptr<webrtc::AudioDeviceModule> CreateAudioDeviceModule() const;
		rtc::scoped_refptr<webrtc::AudioProcessing> SetupAudioProcessingModule();
		// Procedure for WebRTC inter-thread communication
		void StartWebRtcSignallingThread();
		void StopWebRtcSignallingThread();
		/** End Own methods */

	private:
		bool bCaptureNextBackBufferAndStream = false;

		FString StreamerId;
		FString CurrentSignallingServerURL;
		const FString AudioTrackLabel = TEXT("pixelstreaming_audio_track_label");
		const FString VideoTrackLabel = TEXT("pixelstreaming_video_track_label");

		TUniquePtr<rtc::Thread> WebRtcSignallingThread;
		TSharedPtr<IPixelStreamingInputDevice> InputDevice;

		TUniquePtr<FPixelStreamingSignallingConnection> SignallingServerConnection;
		double LastSignallingServerConnectionAttemptTimestamp = 0;

		webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig;

		/* P2P Peer Connections use these exclusively */
		rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> P2PPeerConnectionFactory;
		FVideoEncoderFactorySimple* P2PVideoEncoderFactory;
		/* P2P Peer Connections use these exclusively */

		/* SFU Peer Connections use these exclusively */
		rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> SFUPeerConnectionFactory;
		FVideoEncoderFactorySimulcast* SFUVideoEncoderFactory;
		/* SFU Peer Connections use these exclusively */

		rtc::scoped_refptr<webrtc::AudioSourceInterface> AudioSource;
		cricket::AudioOptions AudioSourceOptions;

		// When we send a freeze frame we retain the data so we send freeze frame to new peers if they join during a freeze frame.
		TArray64<uint8> CachedJpegBytes;

		bool bStreamingStarted = false;

		FPlayerSessions PlayerSessions;

		TUniquePtr<webrtc::SessionDescriptionInterface> SFULocalDescription;
		TUniquePtr<webrtc::SessionDescriptionInterface> SFURemoteDescription;

		TUniquePtr<FVideoSourceGroup> VideoSourceGroup;

		FStreamingStartedEvent StreamingStartedEvent;
		FStreamingStoppedEvent StreamingStoppedEvent;
	};
} // namespace UE::PixelStreaming
