// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"
#include "ProtocolDefs.h"
#include "HAL/ThreadSafeBool.h"
#include "PixelStreamingAudioSink.h"
#include "PlayerId.h"
#include "PixelStreamingDataChannelObserver.h"
#include "PixelStreamingStats.h"
#include "PixelStreamingRTCStatsCollector.h"
#include "IPixelStreamingStatsConsumer.h"

class FSignallingServerConnection;
class IPixelStreamingSessions;

class FPlayerSession : public webrtc::PeerConnectionObserver
{
public:
	FPlayerSession(IPixelStreamingSessions* InSessions, FSignallingServerConnection* InSignallingServerConnection, FPlayerId PlayerId);
	virtual ~FPlayerSession() override;

	webrtc::PeerConnectionInterface& GetPeerConnection();
	void SetPeerConnection(const rtc::scoped_refptr<webrtc::PeerConnectionInterface>& InPeerConnection);

	void SetDataChannel(const rtc::scoped_refptr<webrtc::DataChannelInterface>& InDataChannel);

	void OnAnswer(FString Sdp);
	void OnRemoteIceCandidate(const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp);
	void DisconnectPlayer(const FString& Reason);

	FPlayerId GetPlayerId() const;
	FPixelStreamingAudioSink& GetAudioSink();
	FPixelStreamingDataChannelObserver& GetDataChannelObserver();

	bool SendMessage(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const;
	void SendQualityControlStatus(bool bIsQualityController) const;
	void SendFreezeFrame(const TArray64<uint8>& JpegBytes) const;
	void SendFileData(const TArray<uint8>& ByteData, const FString& MimeType, const FString& FileExtension) const;
	void SendUnfreezeFrame() const;
	void SendVideoEncoderQP(double QP) const;
	void PollWebRTCStats() const;

private:
	void ModifyAudioTransceiverDirection();

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
	FPlayerId PlayerId;
	rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection;
	rtc::scoped_refptr<webrtc::DataChannelInterface> DataChannel;
	FPixelStreamingDataChannelObserver DataChannelObserver;
	FThreadSafeBool bDisconnecting = false;
	FPixelStreamingAudioSink AudioSink;
	rtc::scoped_refptr<webrtc::RTCStatsCollectorCallback> WebRTCStatsCallback;
	TSharedPtr<IPixelStreamingStatsConsumer> QPReporter;
};
