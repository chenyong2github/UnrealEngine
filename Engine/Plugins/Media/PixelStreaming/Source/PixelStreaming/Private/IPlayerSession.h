// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingProtocolDefs.h"
#include "PixelStreamingPlayerId.h"

class IPixelStreamingAudioSink;

namespace UE::PixelStreaming
{
	class FSignallingServerConnection;
	class FDataChannelObserver;

	class IPlayerSession : public webrtc::PeerConnectionObserver
	{
	public:
		virtual ~IPlayerSession() {}

		virtual webrtc::PeerConnectionInterface& GetPeerConnection() = 0;
		virtual void SetPeerConnection(const rtc::scoped_refptr<webrtc::PeerConnectionInterface>& InPeerConnection) = 0;
		virtual void SetDataChannel(const rtc::scoped_refptr<webrtc::DataChannelInterface>& InDataChannel) = 0;
		virtual void SetVideoSource(const rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>& InVideoSource) = 0;

		virtual void OnAnswer(FString Sdp) = 0;
		virtual void OnRemoteIceCandidate(const FString& SdpMid, int SdpMLineIndex, const FString& Sdp) = 0;
		virtual void DisconnectPlayer(const FString& Reason) = 0;

		virtual FPixelStreamingPlayerId GetPlayerId() const = 0;
		virtual IPixelStreamingAudioSink* GetAudioSink() = 0;
		virtual FDataChannelObserver* GetDataChannelObserver() = 0;

		virtual bool SendMessage(Protocol::EToPlayerMsg Type, const FString& Descriptor) const = 0;
		virtual void SendQualityControlStatus(bool bIsQualityController) const = 0;
		virtual void SendFreezeFrame(const TArray64<uint8>& JpegBytes) const = 0;
		virtual void SendFileData(const TArray<uint8>& ByteData, const FString& MimeType, const FString& FileExtension) const = 0;
		virtual void SendUnfreezeFrame() const = 0;
		virtual void SendArbitraryData(const TArray<uint8>& DataBytes, const uint8 MessageType) const = 0;
		virtual void SendVideoEncoderQP(double QP) const = 0;
		virtual void PollWebRTCStats() const = 0;
		virtual void OnDataChannelClosed() const = 0;
	};
} // namespace UE::PixelStreaming
