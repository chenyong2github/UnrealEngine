// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SignallingServerConnection.h"
#include "ProtocolDefs.h"
#include "Templates/SharedPointer.h"
#include "RHI.h"
#include "HAL/Thread.h"
#include "IPixelStreamingSessions.h"
#include "ThreadSafePlayerSessions.h"
#include "FixedFPSPump.h"
#include "GPUFencePoller.h"

namespace UE
{
	namespace PixelStreaming
	{
		class FPlayerSession;
		class FVideoEncoderFactory;
		class FSimulcastEncoderFactory;
		class FSetSessionDescriptionObserver;

		class FStreamer : public FSignallingServerConnectionObserver, public IPixelStreamingSessions
		{
		public:
			static bool IsPlatformCompatible();

			explicit FStreamer(const FString& SignallingServerUrl, const FString& StreamerId);
			virtual ~FStreamer() override;

			void SendPlayerMessage(Protocol::EToPlayerMsg Type, const FString& Descriptor);
			void SendFreezeFrame(const TArray64<uint8>& JpegBytes);
			void SendUnfreezeFrame();
			void SendFileData(TArray<uint8>& ByteData, FString& MimeType, FString& FileExtension);
			void KickPlayer(FPixelStreamingPlayerId PlayerId);
			void ForceKeyFrame();

			bool IsStreaming() const { return bStreamingStarted; }
			void SetStreamingStarted(bool bStarted) { bStreamingStarted = bStarted; }
			FSignallingServerConnection* GetSignallingServerConnection() const { return SignallingServerConnection.Get(); }
			FVideoEncoderFactory* GetP2PVideoEncoderFactory() const { return P2PVideoEncoderFactory; }
			FFixedFPSPump& GetPumpThread() { return PumpThread; }
			FGPUFencePoller& GetFencePollerThread() { return FencePollerThread; }

			// data coming from the engine
			void OnFrameBufferReady(const FTexture2DRHIRef& FrameBuffer);

			// Begin IPixelStreamingSessions interface.
			virtual int GetNumPlayers() const override;
			virtual IPixelStreamingAudioSink* GetAudioSink(FPixelStreamingPlayerId PlayerId) const override;
			virtual IPixelStreamingAudioSink* GetUnlistenedAudioSink() const override;
			virtual bool IsQualityController(FPixelStreamingPlayerId PlayerId) const override;
			virtual void SetQualityController(FPixelStreamingPlayerId PlayerId) override;
			virtual bool SendMessage(FPixelStreamingPlayerId PlayerId, Protocol::EToPlayerMsg Type, const FString& Descriptor) const override;
			virtual void SendLatestQP(FPixelStreamingPlayerId PlayerId, int LatestQP) const override;
			virtual void SendFreezeFrameTo(FPixelStreamingPlayerId PlayerId, const TArray64<uint8>& JpegBytes) const override;
			virtual void PollWebRTCStats() const override;
			// End IPixelStreamingSessions interface.

			void SendCachedFreezeFrameTo(FPixelStreamingPlayerId PlayerId) const;

		private:
			webrtc::PeerConnectionInterface* CreateSession(FPixelStreamingPlayerId PlayerId, int Flags);
			webrtc::AudioProcessing* SetupAudioProcessingModule();

			// Procedure for WebRTC inter-thread communication
			void StartWebRtcSignallingThread();
			void ConnectToSignallingServer();

			// ISignallingServerConnectionObserver impl
			virtual void OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config) override;
			virtual void OnSessionDescription(FPixelStreamingPlayerId PlayerId, webrtc::SdpType Type, const FString& Sdp) override;
			virtual void OnRemoteIceCandidate(FPixelStreamingPlayerId PlayerId, const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp) override;
			virtual void OnPlayerConnected(FPixelStreamingPlayerId PlayerId, int Flags) override;
			virtual void OnPlayerDisconnected(FPixelStreamingPlayerId PlayerId) override;
			virtual void OnSignallingServerDisconnected() override;

			// own methods
			void OnOffer(FPixelStreamingPlayerId PlayerId, const FString& Sdp);
			void ModifyTransceivers(std::vector<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> Transceivers, int Flags);
			void MungeRemoteSDP(cricket::SessionDescription* SessionDescription);
			void DeletePlayerSession(FPixelStreamingPlayerId PlayerId);
			void DeleteAllPlayerSessions();
			void AddStreams(FPixelStreamingPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection, int Flags);
			void SetupVideoTrack(FPixelStreamingPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection, FString const VideoStreamId, FString const VideoTrackLabel, int Flags);
			void SetupAudioTrack(FPixelStreamingPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection, const FString AudioStreamId, const FString AudioTrackLabel, int Flags);
			std::vector<webrtc::RtpEncodingParameters> CreateRTPEncodingParams(int Flags);
			void SendAnswer(FPixelStreamingPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection, TUniquePtr<webrtc::SessionDescriptionInterface> Sdp);
			void OnDataChannelOpen(FPixelStreamingPlayerId PlayerId, webrtc::DataChannelInterface* DataChannel);
			void OnQualityControllerChanged(FPixelStreamingPlayerId PlayerId);
			void PostPlayerDeleted(FPixelStreamingPlayerId PlayerId, bool WasQualityController);
			void SetLocalDescription(webrtc::PeerConnectionInterface* PeerConnection, FSetSessionDescriptionObserver* Observer, webrtc::SessionDescriptionInterface* SDP);
			FString GetAudioStreamID() const;
			FString GetVideoStreamID() const;
			rtc::scoped_refptr<webrtc::AudioDeviceModule> CreateAudioDeviceModule() const;

		private:
			FString SignallingServerUrl;
			FString StreamerId;
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

			FThreadSafePlayerSessions PlayerSessions;
			FStats Stats;

			FFixedFPSPump PumpThread;
			FGPUFencePoller FencePollerThread;

			TUniquePtr<webrtc::SessionDescriptionInterface> SFULocalDescription;
			TUniquePtr<webrtc::SessionDescriptionInterface> SFURemoteDescription;
		};
	} // namespace PixelStreaming
} // namespace UE
