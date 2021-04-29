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
	virtual ~FStreamerConnection() override;

private: // FSignallingServerObserver impl
	virtual void OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config) override;
	virtual void OnAnswer(TUniquePtr<webrtc::SessionDescriptionInterface> Sdp) override;
	virtual void OnRemoteIceCandidate(TUniquePtr<webrtc::IceCandidateInterface> Candidate) override;
	virtual void OnSignallingServerDisconnected() override;
	virtual void OnPlayerCount(uint32 PlayerCount) override { /* NOOP */ }

private: // webrtc::PeerConnectionObserver impl
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

private: // webrtc::DataChannelObserver impl
	virtual void OnStateChange() override
	{}
	virtual void OnBufferedAmountChange(uint64_t PreviousAmount) override
	{}
	virtual void OnMessage(const webrtc::DataBuffer& Buffer) override;

private:
	void SignallingThreadFunc();

private:
	TUniquePtr<FThread> SignallingThread;
	
	#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
	DWORD SignallingThreadId = 0;
	#elif PLATFORM_LINUX
	FUnixPlatformTypes::DWORD SignallingThreadId = 0;
	#endif

	FString SignallingServerAddress;
	TUniquePtr<FSignallingServerConnection> SignallingServerConnection;

	TUniqueFunction<void()> OnDisconnection;

	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory;
	rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection;

	TUniquePtr<FAudioSink> AudioSink;
	TUniquePtr<FVideoSink> VideoSink;

	rtc::scoped_refptr<webrtc::DataChannelInterface> DataChannel;
};
