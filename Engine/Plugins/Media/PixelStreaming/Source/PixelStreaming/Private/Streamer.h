// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SignallingServerConnection.h"
#include "PixelStreamingProtocolDefs.h"
#include "RHI.h"
#include "PlayerSessions.h"
#include "FixedFPSPump.h"
#include "GPUFencePoller.h"
#include "Stats.h"

namespace UE::PixelStreaming
{
	class FPlayerSession;
	class FVideoEncoderFactory;
	class FSimulcastEncoderFactory;
	class FSetSessionDescriptionObserver;
	class FGPUFencePoller;
	class IPlayerSession;

	class FStreamer : public FSignallingServerConnectionObserver
	{
	public:
		static bool IsPlatformCompatible();

		FStreamer(const FString& StreamerId);
		virtual ~FStreamer();

		bool StartStreaming(const FString& SignallingServerUrl);
		void StopStreaming();

		bool IsStreaming() const { return bStreamingStarted; }
		FPlayerSessions& GetPlayerSessions() { return PlayerSessions; }

		void SendPlayerMessage(Protocol::EToPlayerMsg Type, const FString& Descriptor);
		void SendFreezeFrame(const TArray64<uint8>& JpegBytes);
		void SendUnfreezeFrame();
		void SendFileData(TArray<uint8>& ByteData, FString& MimeType, FString& FileExtension);
		void KickPlayer(FPixelStreamingPlayerId PlayerId);
		void ForceKeyFrame();

	private:
		TSharedPtr<IPlayerSession> CreateSession(FPixelStreamingPlayerId PlayerId, int Flags);
		void SendCachedFreezeFrameTo(FPixelStreamingPlayerId PlayerId) const;

		webrtc::AudioProcessing* SetupAudioProcessingModule();

		// Procedure for WebRTC inter-thread communication
		void StartWebRtcSignallingThread();
		void ConnectToSignallingServer(const FString& Url);
		void DisconnectFromSignallingServer();

		// ISignallingServerConnectionObserver impl
		virtual void OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config) override;
		virtual void OnSessionDescription(FPixelStreamingPlayerId PlayerId, webrtc::SdpType Type, const FString& Sdp) override;
		virtual void OnRemoteIceCandidate(FPixelStreamingPlayerId PlayerId, const FString& SdpMid, int SdpMLineIndex, const FString& Sdp) override;
		virtual void OnPlayerConnected(FPixelStreamingPlayerId PlayerId, int Flags) override;
		virtual void OnPlayerDisconnected(FPixelStreamingPlayerId PlayerId) override;
		virtual void OnStreamerDataChannels(FPixelStreamingPlayerId SFUId, FPixelStreamingPlayerId PlayerId, int32 SendStreamId, int32 RecvStreamId) override;
		virtual void OnSignallingServerConnected() override;
		virtual void OnSignallingServerDisconnected() override;

		// own methods
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
		void OnQualityControllerChanged(FPixelStreamingPlayerId PlayerId);
		void PostPlayerDeleted(FPixelStreamingPlayerId PlayerId, bool bWasQualityController);
		void SetLocalDescription(webrtc::PeerConnectionInterface* PeerConnection, FSetSessionDescriptionObserver* Observer, webrtc::SessionDescriptionInterface* SDP);
		FString GetAudioStreamID() const;
		FString GetVideoStreamID() const;
		rtc::scoped_refptr<webrtc::AudioDeviceModule> CreateAudioDeviceModule() const;

	private:
		FString StreamerId;
		FString PreviousSignallingServerURL;
		const FString AudioTrackLabel = TEXT("pixelstreaming_audio_track_label");
		const FString VideoTrackLabel = TEXT("pixelstreaming_video_track_label");

		TUniquePtr<rtc::Thread> WebRtcSignallingThread;

		TUniquePtr<FSignallingServerConnection> SignallingServerConnection;
		double LastSignallingServerConnectionAttemptTimestamp = 0;

		webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig;

		/* P2P Peer Connections use these exclusively */
		rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> P2PPeerConnectionFactory;
		FVideoEncoderFactory* P2PVideoEncoderFactory;
		/* P2P Peer Connections use these exclusively */

		/* SFU Peer Connections use these exclusively */
		rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> SFUPeerConnectionFactory;
		FSimulcastEncoderFactory* SFUVideoEncoderFactory;
		/* SFU Peer Connections use these exclusively */

		rtc::scoped_refptr<webrtc::AudioSourceInterface> AudioSource;
		cricket::AudioOptions AudioSourceOptions;

		// When we send a freeze frame we retain the data so we send freeze frame to new peers if they join during a freeze frame.
		TArray64<uint8> CachedJpegBytes;

		FThreadSafeBool bStreamingStarted = false;

		FPlayerSessions PlayerSessions;
		FStats Stats;

		FFixedFPSPump PumpThread;
		FGPUFencePoller FencePollerThread;

		TUniquePtr<webrtc::SessionDescriptionInterface> SFULocalDescription;
		TUniquePtr<webrtc::SessionDescriptionInterface> SFURemoteDescription;
	};
} // namespace UE::PixelStreaming
