// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRTCIncludes.h"
#include "PixelStreamingProtocolDefs.h"
#include "PixelStreamingPlayerId.h"
#include "VideoSourceBase.h"
#include "Templates/SharedPointer.h"

class IPixelStreamingAudioSink;

namespace UE::PixelStreaming
{
	class FDataChannelObserver;

	class IPlayerSession : public webrtc::PeerConnectionObserver
	{
	public:
		virtual ~IPlayerSession() {}

		virtual rtc::scoped_refptr<webrtc::PeerConnectionInterface> GetPeerConnection() = 0;
		virtual void SetPeerConnection(const rtc::scoped_refptr<webrtc::PeerConnectionInterface>& InPeerConnection) = 0;
		virtual void SetDataChannel(const rtc::scoped_refptr<webrtc::DataChannelInterface>& InDataChannel) = 0;
		virtual void SetVideoSource(const rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>& InVideoSource) = 0;
		virtual rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> GetVideoSource() const = 0;

		virtual void OnAnswer(FString Sdp) = 0;
		virtual void OnRemoteIceCandidate(const FString& SdpMid, int SdpMLineIndex, const FString& Sdp) = 0;
		virtual void DisconnectPlayer(const FString& Reason) = 0;

		virtual FName GetSessionType() const = 0;
		virtual FPixelStreamingPlayerId GetPlayerId() const = 0;
		virtual IPixelStreamingAudioSink* GetAudioSink() = 0;
		virtual TSharedPtr<FDataChannelObserver> GetDataChannelObserver() = 0;

		virtual bool SendMessage(Protocol::EToPlayerMsg Type, const FString& Descriptor) const = 0;
		virtual bool SendQualityControlStatus(bool bIsQualityController) const = 0;
		virtual bool SendInputControlStatus(bool bIsInputController) const = 0;
		virtual bool SendFreezeFrame(const TArray64<uint8>& JpegBytes) const = 0;
		virtual bool SendFileData(const TArray<uint8>& ByteData, const FString& MimeType, const FString& FileExtension) const = 0;
		virtual bool SendUnfreezeFrame() const = 0;
		virtual bool SendArbitraryData(const TArray<uint8>& DataBytes, const uint8 MessageType) const = 0;
		virtual bool SendVideoEncoderQP(double QP) const = 0;
		virtual void PollWebRTCStats() const = 0;
		virtual void OnDataChannelClosed() const = 0;
	};
} // namespace UE::PixelStreaming
