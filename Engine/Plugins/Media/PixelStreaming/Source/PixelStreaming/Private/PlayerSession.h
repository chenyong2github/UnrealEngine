// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"
#include "ProtocolDefs.h"
#include "HAL/ThreadSafeBool.h"

class FStreamer;
class FInputDevice;
class FPixelStreamingVideoEncoder;

class FPlayerSession
    : public webrtc::PeerConnectionObserver
    , public webrtc::DataChannelObserver
{
public:
	FPlayerSession(FStreamer& Outer, FPlayerId PlayerId, bool bOriginalQualityController);
	virtual ~FPlayerSession() override;

	void SetVideoEncoder(FPixelStreamingVideoEncoder* InVideoEncoder);

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

	void SendFreezeFrame(const TArray64<uint8>& JpegBytes);
	void SendUnfreezeFrame();

	void OnNewSecondarySession();

private:
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

	//
	// werbrtc::DataChannelObserver implementation.
	//
	virtual void OnStateChange() override;
	virtual void OnBufferedAmountChange(uint64_t PreviousAmount) override;
	virtual void OnMessage(const webrtc::DataBuffer& Buffer) override;

private:
	FStreamer& Streamer;
	FPlayerId PlayerId;
	bool bOriginalQualityController;
	FPixelStreamingVideoEncoder* VideoEncoder = nullptr;
	rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection;
	rtc::scoped_refptr<webrtc::DataChannelInterface> DataChannel;
	FThreadSafeBool bDisconnecting = false;
	FInputDevice& InputDevice;
};
