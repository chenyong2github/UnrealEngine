// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPlayerSession.h"
#include "DataChannelObserver.h"

namespace UE::PixelStreaming
{
	class FPlayerSessions;

	class FPlayerSessionDataOnly : public IPlayerSession
	{
	public:
		FPlayerSessionDataOnly(FPlayerSessions* InSessions,
			FPixelStreamingPlayerId InPlayerId,
			TSharedPtr<IPixelStreamingInputDevice> InInputDevice,
			rtc::scoped_refptr<webrtc::PeerConnectionInterface> InPeerConnection,
			int32 SendStreamId,
			int32 RecvStreamId);

		virtual ~FPlayerSessionDataOnly();

		virtual rtc::scoped_refptr<webrtc::PeerConnectionInterface> GetPeerConnection() override { return nullptr; }
		virtual void SetPeerConnection(const rtc::scoped_refptr<webrtc::PeerConnectionInterface>& InPeerConnection) override {}
		virtual void SetDataChannel(const rtc::scoped_refptr<webrtc::DataChannelInterface>& InDataChannel) override {}
		virtual void SetVideoSource(const rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>& InVideoSource) override {}
		virtual rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> GetVideoSource() const override { return nullptr; }

		virtual void OnAnswer(FString Sdp) override {}
		virtual void OnRemoteIceCandidate(const FString& SdpMid, int SdpMLineIndex, const FString& Sdp) override {}
		virtual void DisconnectPlayer(const FString& Reason) override {}

		virtual FName GetSessionType() const override { return Type; };
		virtual FPixelStreamingPlayerId GetPlayerId() const override { return PlayerId; }
		virtual IPixelStreamingAudioSink* GetAudioSink() override { return nullptr; }
		virtual TSharedPtr<FDataChannelObserver> GetDataChannelObserver() override;

		virtual bool SendMessage(Protocol::EToPlayerMsg Type, const FString& Descriptor) const override;
		virtual bool SendInputControlStatus(bool bIsInputController) const override;
		virtual bool SendQualityControlStatus(bool bIsQualityController) const override { return false; }
		virtual bool SendFreezeFrame(const TArray64<uint8>& JpegBytes) const override { return false; }
		virtual bool SendFileData(const TArray<uint8>& ByteData, const FString& MimeType, const FString& FileExtension) const override { return false; }
		virtual bool SendUnfreezeFrame() const override { return false; }
		virtual bool SendArbitraryData(const TArray<uint8>& DataBytes, const uint8 MessageType) const override { return false; }
		virtual bool SendVideoEncoderQP(double QP) const override { return false; }
		virtual void PollWebRTCStats() const override {}
		virtual void OnDataChannelClosed() const override;

	protected:
		//
		// webrtc::PeerConnectionObserver implementation.
		//
		virtual void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState NewState) override {}
		virtual void OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream) override {}
		virtual void OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream) override {}
		virtual void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> Channel) override {}
		virtual void OnRenegotiationNeeded() override {}
		virtual void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState NewState) override {}
		virtual void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState NewState) override {}
		virtual void OnIceCandidate(const webrtc::IceCandidateInterface* Candidate) override {}
		virtual void OnIceCandidatesRemoved(const std::vector<cricket::Candidate>& candidates) override {}
		virtual void OnIceConnectionReceivingChange(bool Receiving) override {}
		virtual void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override {}
		virtual void OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {}

		virtual void SetupDataChannels(FPlayerSessions* InSessions, rtc::scoped_refptr<webrtc::PeerConnectionInterface> InPeerConnection, int32 SendStreamId, int32 RecvStreamId, TSharedPtr<IPixelStreamingInputDevice> InInputDevice);

	public:
		inline static const FName Type = FName(TEXT("DataOnly"));

	private:
		FPixelStreamingPlayerId PlayerId;
		bool bUsingBidirectionalDataChannel;
		rtc::scoped_refptr<webrtc::DataChannelInterface> SendDataChannel;
		rtc::scoped_refptr<webrtc::DataChannelInterface> RecvDataChannel;
		rtc::scoped_refptr<webrtc::DataChannelInterface> BidiDataChannel;
		TSharedPtr<FDataChannelObserver> SendDataChannelObserver;
		TSharedPtr<FDataChannelObserver> RecvDataChannelObserver;
		TSharedPtr<FDataChannelObserver> BidiDataChannelObserver;
	};
} // namespace UE::PixelStreaming
