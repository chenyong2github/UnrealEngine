// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WebRTCIncludes.h"
#include "SignallingServerConnection.h"
#include "PixelStreamingProtocolDefs.h"

namespace UE::PixelStreaming
{
	class FClientRTC : public FSignallingServerConnectionObserver, public webrtc::PeerConnectionObserver, public webrtc::DataChannelObserver
	{
	public:
		FClientRTC();
		virtual ~FClientRTC();

		enum class EState
		{
			Unknown,
			Connecting,
			ConnectedSignalling,
			ConnectedStreamer,
			Disconnected,
		};

		void Connect(const FString& Url);
		EState GetState() const { return State; }

		bool SendMessage(Protocol::EToStreamerMsg Type, const FString& Descriptor) const;

		DECLARE_EVENT_OneParam(FClientRTC, FConnectedEvent, FClientRTC&);
		FConnectedEvent OnConnected;

		DECLARE_EVENT_OneParam(FClientRTC, FDisconnectedEvent, FClientRTC&);
		FDisconnectedEvent OnDisconnected;

		DECLARE_EVENT_ThreeParams(FClientRTC, FDataMessageEvent, FClientRTC&, uint8, const FString&);
		FDataMessageEvent OnDataMessage;

	private:
		//
		// FSignallingServerConnectionObserver implementation.
		//
		virtual void OnSignallingServerConnected() override;
		virtual void OnSignallingServerDisconnected() override;
		virtual void OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config) override;
		virtual void OnSessionDescription(webrtc::SdpType Type, const FString& Sdp) override;
		virtual void OnRemoteIceCandidate(const FString& SdpMid, int SdpMLineIndex, const FString& Sdp) override;
		virtual void OnPeerDataChannels(int32 SendStreamId, int32 RecvStreamId) override;
		virtual void OnPlayerCount(uint32 Count) override {}

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
		// webrtc::DataChannelObserver implementation.
		//
		virtual void OnStateChange() override;
		virtual void OnMessage(const webrtc::DataBuffer& buffer) override;

		//
		// Internal methods
		//
		void SetRemoteDescription(const FString& Sdp);

		//
		// Internal variables
		//
		TUniquePtr<rtc::Thread> WebRtcSignallingThread;
		rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory;
		TUniquePtr<FSignallingServerConnection> SignallingServerConnection;
		webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig;
		rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection;
		rtc::scoped_refptr<webrtc::DataChannelInterface> DataChannel;
		EState State;
	};
} // namespace UE::PixelStreaming
