// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"
#include "ProtocolDefs.h"
#include "HAL/ThreadSafeBool.h"
#include "AudioSink.h"
#include "PixelStreamingPlayerId.h"
#include "DataChannelObserver.h"
#include "Stats.h"
#include "RTCStatsCollector.h"
#include "IPixelStreamingStatsConsumer.h"

namespace UE
{
	namespace PixelStreaming
	{
		class FSignallingServerConnection;
		class IPixelStreamingSessions;

		class FPlayerSession : public webrtc::PeerConnectionObserver
		{
		public:
			FPlayerSession(IPixelStreamingSessions* InSessions, FSignallingServerConnection* InSignallingServerConnection, FPixelStreamingPlayerId PlayerId);
			virtual ~FPlayerSession() override;

			webrtc::PeerConnectionInterface& GetPeerConnection();
			void SetPeerConnection(const rtc::scoped_refptr<webrtc::PeerConnectionInterface>& InPeerConnection);
			void SetDataChannel(const rtc::scoped_refptr<webrtc::DataChannelInterface>& InDataChannel);
			void SetVideoSource(const rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>& InVideoSource);

			void OnAnswer(FString Sdp);
			void OnRemoteIceCandidate(const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp);
			void DisconnectPlayer(const FString& Reason);

			FPixelStreamingPlayerId GetPlayerId() const;
			FAudioSink& GetAudioSink();
			FDataChannelObserver& GetDataChannelObserver();

			bool SendMessage(Protocol::EToPlayerMsg Type, const FString& Descriptor) const;
			void SendQualityControlStatus(bool bIsQualityController) const;
			void SendFreezeFrame(const TArray64<uint8>& JpegBytes) const;
			void SendFileData(const TArray<uint8>& ByteData, const FString& MimeType, const FString& FileExtension) const;
			void SendUnfreezeFrame() const;
			void SendArbitraryData(const TArray<uint8>& DataBytes, const uint8 MessageType) const;
			void SendVideoEncoderQP(double QP) const;
			void PollWebRTCStats() const;

		private:
			void ModifyAudioTransceiverDirection();
			void AddSinkToAudioTrack();

			//
			// webrtc::PeerConnectionObserver implementation.
			//
			virtual void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState NewState) override;
			virtual void OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream) override;
			virtual void OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream) override;
			virtual void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> Channel) override;
			virtual void OnRenegotiationNeeded() override;
			virtual void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState NewState) override;
			virtual void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState NewState) override;
			virtual void OnIceCandidate(const webrtc::IceCandidateInterface* Candidate) override;
			virtual void OnIceCandidatesRemoved(const std::vector<cricket::Candidate>& candidates) override;
			virtual void OnIceConnectionReceivingChange(bool Receiving) override;
			virtual void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;
			virtual void OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;

		private:
			size_t SerializeToBuffer(rtc::CopyOnWriteBuffer& Buffer, size_t Pos, const void* Data, size_t DataSize) const;

			FSignallingServerConnection* SignallingServerConnection;
			FPixelStreamingPlayerId PlayerId;
			rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection;
			rtc::scoped_refptr<webrtc::DataChannelInterface> DataChannel;
			FDataChannelObserver DataChannelObserver;
			FThreadSafeBool bDisconnecting = false;
			FAudioSink AudioSink;
			rtc::scoped_refptr<webrtc::RTCStatsCollectorCallback> WebRTCStatsCallback;
			TSharedPtr<IPixelStreamingStatsConsumer> QPReporter;
			rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> VideoSource;
		};
	} // namespace PixelStreaming
} // namespace UE
