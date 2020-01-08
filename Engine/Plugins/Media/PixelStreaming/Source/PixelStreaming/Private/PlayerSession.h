// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"
#include "ProtocolDefs.h"
#include "HAL/ThreadSafeBool.h"

class FStreamer;
class FVideoEncoder;
class FInputDevice;

class FPlayerSession
    : public webrtc::PeerConnectionObserver
    , public webrtc::DataChannelObserver
{
public:
	FPlayerSession(FStreamer& Outer, FPlayerId PlayerId, bool bOriginalQualityController);
	~FPlayerSession() override;

	void SetVideoEncoder(FVideoEncoder* InVideoEncoder);

	webrtc::PeerConnectionInterface& GetPeerConnection();
	void SetPeerConnection(const rtc::scoped_refptr<webrtc::PeerConnectionInterface>& InPeerConnection);
	
	FPlayerId GetPlayerId() const;

	void OnOffer(TUniquePtr<webrtc::SessionDescriptionInterface> Sdp);
	void OnRemoteIceCandidate(TUniquePtr<webrtc::IceCandidateInterface> Candidate);
	void DisconnectPlayer(const FString& Reason);

	bool IsOriginalQualityController() const;
	bool IsQualityController() const;
	void SetQualityController(bool bControlsQuality);

	void SendMessage(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor);

	void SendFreezeFrame(const TArray<uint8>& JpegBytes);
	void SendUnfreezeFrame();

private:
	//
	// webrtc::PeerConnectionObserver implementation.
	//
	void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState NewState) override;
	void OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream) override;
	void OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream) override;
	void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> Channel) override;
	void OnRenegotiationNeeded() override;
	void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState NewState) override;
	void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState NewState) override;
	void OnIceCandidate(const webrtc::IceCandidateInterface* Candidate) override;
	void OnIceCandidatesRemoved(const std::vector<cricket::Candidate>& candidates) override;
	void OnIceConnectionReceivingChange(bool Receiving) override;
	void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;
	void OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;

	//
	// werbrtc::DataChannelObserver implementation.
	//
	void OnStateChange() override;
	void OnBufferedAmountChange(uint64 PreviousAmount) override;
	void OnMessage(const webrtc::DataBuffer& Buffer) override;

private:
	FStreamer& Streamer;
	FPlayerId PlayerId;
	bool bOriginalQualityController;
	TAtomic<FVideoEncoder*> VideoEncoder{ nullptr };
	rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection;
	rtc::scoped_refptr<webrtc::DataChannelInterface> DataChannel;
	FThreadSafeBool bDisconnecting = false;
	FInputDevice& InputDevice;
};
