// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"
#include "SignallingServerConnection.h"

#include "Templates/UniquePtr.h"
#include "Templates/SharedPointer.h"
#include "Templates/Function.h"

class FThread;
class IMediaTextureSample;
class IMediaAudioSample;

class FVideoSink;
class FAudioSink;

class FStreamerConnection final : 
	public FSignallingServerConnectionObserver,
	public webrtc::PeerConnectionObserver,
	public webrtc::DataChannelObserver
{
public:
	explicit FStreamerConnection(const FString& SignallingServerAddress, TUniqueFunction<void()>&& OnDisconnection, TUniqueFunction<void(const TSharedRef<IMediaAudioSample, ESPMode::ThreadSafe>& Sample)>&& OnAudioSample, TUniqueFunction<void(const TSharedRef<IMediaTextureSample, ESPMode::ThreadSafe>& Sample)>&& OnVideoFrame);
	~FStreamerConnection() override;

private: // FSignallingServerObserver impl
	void OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config) override;
	void OnAnswer(TUniquePtr<webrtc::SessionDescriptionInterface> Sdp) override;
	void OnRemoteIceCandidate(TUniquePtr<webrtc::IceCandidateInterface> Candidate) override;
	void OnSignallingServerDisconnected() override;
	void OnPlayerCount(uint32 PlayerCount) override { /* NOOP */ }

private: // webrtc::PeerConnectionObserver impl
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

private: // webrtc::DataChannelObserver impl
	void OnStateChange() override
	{}
	void OnBufferedAmountChange(uint64 PreviousAmount) override
	{}
	void OnMessage(const webrtc::DataBuffer& Buffer) override;

private:
	void SignallingThreadFunc();

private:
	TUniquePtr<FThread> SignallingThread;
	DWORD SignallingThreadId = 0;
	FString SignallingServerAddress;
	TUniquePtr<FSignallingServerConnection> SignallingServerConnection;

	TUniqueFunction<void()> OnDisconnection;

	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory;
	rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection;

	TUniquePtr<FAudioSink> AudioSink;
	TUniquePtr<FVideoSink> VideoSink;

	rtc::scoped_refptr<webrtc::DataChannelInterface> DataChannel;
};
