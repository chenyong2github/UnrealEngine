// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingProtocolDefs.h"
#include "WebRTCIncludes.h"

class PIXELSTREAMING_API FPixelStreamingPeerConnection : public webrtc::PeerConnectionObserver, public webrtc::DataChannelObserver
{
public:
	using FConfig = webrtc::PeerConnectionInterface::RTCConfiguration;
	static TUniquePtr<FPixelStreamingPeerConnection> Create(const FConfig& Config);
	static void Shutdown();
	~FPixelStreamingPeerConnection() = default;

	void SetSuccessCallback(const TFunction<void(const webrtc::SessionDescriptionInterface*)>& Callback);
	void SetFailureCallback(const TFunction<void(const FString&)>& Callback);
	void SetIceCandidateCallback(const TFunction<void(const webrtc::IceCandidateInterface*)>& Callback);

	void SetRemoteDescription(const FString& Sdp);
	void AddRemoteIceCandidate(const FString& SdpMid, int SdpMLineIndex, const FString& Sdp);
	void CreateDataChannels(int32 SendStreamId, int32 RecvStreamId);

	void SetVideoSink(rtc::VideoSinkInterface<webrtc::VideoFrame>* Sink);

	bool SendMessage(UE::PixelStreaming::Protocol::EToStreamerMsg Type, const FString& Descriptor) const;

protected:
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
	virtual void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> Transceiver) override;
	virtual void OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;

	//
	// webrtc::DataChannelObserver implementation.
	//
	virtual void OnStateChange() override;
	virtual void OnMessage(const webrtc::DataBuffer& buffer) override;

private:
	FPixelStreamingPeerConnection() = default;

	TFunction<void(const webrtc::SessionDescriptionInterface*)> SuccessCallback;
	TFunction<void(const FString&)> FailureCallback;
	TFunction<void(const webrtc::IceCandidateInterface*)> IceCandidateCallback;

	rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection;
	rtc::scoped_refptr<webrtc::DataChannelInterface> DataChannel;

	rtc::VideoSinkInterface<webrtc::VideoFrame>* VideoSink;

	static void CreatePeerConnectionFactory();
	static TUniquePtr<rtc::Thread> SignallingThread;
	static rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory;
};
