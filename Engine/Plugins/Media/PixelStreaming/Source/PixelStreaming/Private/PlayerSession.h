// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPlayerSession.h"
#include "DataChannelObserver.h"
#include "HAL/ThreadSafeBool.h"
#include "AudioSink.h"

class IPixelStreamingSessions;
class IPixelStreamingStatsConsumer;

namespace UE::PixelStreaming
{
	class FPlayerSessions;
	class FSignallingServerConnection;

	class FPlayerSession : public IPlayerSession
	{
	public:
		FPlayerSession(FPlayerSessions* InSessions, FSignallingServerConnection* InSignallingServerConnection, FPixelStreamingPlayerId PlayerId);
		virtual ~FPlayerSession();

		virtual webrtc::PeerConnectionInterface& GetPeerConnection() override;
		virtual void SetPeerConnection(const rtc::scoped_refptr<webrtc::PeerConnectionInterface>& InPeerConnection) override;
		virtual void SetDataChannel(const rtc::scoped_refptr<webrtc::DataChannelInterface>& InDataChannel) override;
		virtual void SetVideoSource(const rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>& InVideoSource) override;

		virtual void OnAnswer(FString Sdp) override;
		virtual void OnRemoteIceCandidate(const FString& SdpMid, int SdpMLineIndex, const FString& Sdp) override;
		virtual void DisconnectPlayer(const FString& Reason) override;

		virtual FPixelStreamingPlayerId GetPlayerId() const override;
		virtual IPixelStreamingAudioSink* GetAudioSink() override;
		virtual FDataChannelObserver* GetDataChannelObserver() override;

		virtual bool SendMessage(Protocol::EToPlayerMsg Type, const FString& Descriptor) const override;
		virtual void SendQualityControlStatus(bool bIsQualityController) const override;
		virtual void SendFreezeFrame(const TArray64<uint8>& JpegBytes) const override;
		virtual void SendFileData(const TArray<uint8>& ByteData, const FString& MimeType, const FString& FileExtension) const override;
		virtual void SendUnfreezeFrame() const override;
		virtual void SendArbitraryData(const TArray<uint8>& DataBytes, const uint8 MessageType) const override;
		virtual void SendVideoEncoderQP(double QP) const override;
		virtual void PollWebRTCStats() const override;
		virtual void OnDataChannelClosed() const override;

	private:
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

	protected:
		FPlayerSessions* PlayerSessions;
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
} // namespace UE::PixelStreaming
