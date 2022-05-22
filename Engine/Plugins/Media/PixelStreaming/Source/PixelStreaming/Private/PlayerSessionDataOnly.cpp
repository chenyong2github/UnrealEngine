// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerSessionDataOnly.h"
#include "PixelStreamingDelegates.h"
#include "PixelStreamingPrivate.h"
#include "Utils.h"
#include "PlayerSession.h"

namespace UE::PixelStreaming
{
	FPlayerSessionDataOnly::FPlayerSessionDataOnly(FPlayerSessions* InSessions, FPixelStreamingPlayerId InPlayerId, TSharedPtr<IPixelStreamingInputDevice> InInputDevice, rtc::scoped_refptr<webrtc::PeerConnectionInterface> InPeerConnection, int32 SendStreamId, int32 RecvStreamId)
		: PlayerId(InPlayerId)
		, bUsingBidirectionalDataChannel(SendStreamId == RecvStreamId)
	{
		SetupDataChannels(InSessions, InPeerConnection, SendStreamId, RecvStreamId, InInputDevice);
	}

	FPlayerSessionDataOnly::~FPlayerSessionDataOnly()
	{
	}

	void FPlayerSessionDataOnly::SetupDataChannels(FPlayerSessions* InSessions, rtc::scoped_refptr<webrtc::PeerConnectionInterface> InPeerConnection, int32 SendStreamId, int32 RecvStreamId, TSharedPtr<IPixelStreamingInputDevice> InInputDevice)
	{
		bUsingBidirectionalDataChannel = SendStreamId == RecvStreamId;
		if (!bUsingBidirectionalDataChannel)
		{
			webrtc::DataChannelInit SendDataChannelConfig;
			SendDataChannelConfig.reliable = true;
			SendDataChannelConfig.ordered = true;
			SendDataChannelConfig.negotiated = true;
			SendDataChannelConfig.id = SendStreamId;
			SendDataChannel = InPeerConnection->CreateDataChannel("send-datachannel", &SendDataChannelConfig);
			SendDataChannelObserver = MakeShared<FDataChannelObserver>(InSessions, PlayerId, EDataChannelDirection::SendOnly, InInputDevice);
			SendDataChannelObserver->Register(SendDataChannel);

			webrtc::DataChannelInit RecvDataChannelConfig;
			RecvDataChannelConfig.reliable = true;
			RecvDataChannelConfig.ordered = true;
			RecvDataChannelConfig.negotiated = true;
			RecvDataChannelConfig.id = RecvStreamId;
			RecvDataChannel = InPeerConnection->CreateDataChannel("recv-datachannel", &RecvDataChannelConfig);
			RecvDataChannelObserver = MakeShared<FDataChannelObserver>(InSessions, PlayerId, EDataChannelDirection::RecvOnly, InInputDevice);
			RecvDataChannelObserver->Register(RecvDataChannel);
		}
		else
		{
			webrtc::DataChannelInit DataChannelConfig;
			DataChannelConfig.reliable = true;
			DataChannelConfig.ordered = true;
			DataChannelConfig.negotiated = true;
			DataChannelConfig.id = SendStreamId;
			BidiDataChannel = InPeerConnection->CreateDataChannel("datachannel", &DataChannelConfig);
			BidiDataChannelObserver = MakeShared<FDataChannelObserver>(InSessions, PlayerId, EDataChannelDirection::Bidirectional, InInputDevice);
			BidiDataChannelObserver->Register(BidiDataChannel);
		}
	}

	TSharedPtr<FDataChannelObserver> FPlayerSessionDataOnly::GetDataChannelObserver()
	{
		if (bUsingBidirectionalDataChannel)
		{
			return BidiDataChannelObserver;
		}
		else
		{
			return SendDataChannelObserver;
		}
	}

	bool FPlayerSessionDataOnly::SendMessage(Protocol::EToPlayerMsg InMessageType, const FString& Descriptor) const
	{
		return FPlayerSession::SendMessage(bUsingBidirectionalDataChannel ? BidiDataChannel : SendDataChannel, InMessageType, Descriptor);
	}

	bool FPlayerSessionDataOnly::SendInputControlStatus(bool bIsInputController) const
	{
		return FPlayerSession::SendInputControlStatus(bUsingBidirectionalDataChannel ? BidiDataChannel : SendDataChannel, PlayerId, bIsInputController);
	}

	void FPlayerSessionDataOnly::OnDataChannelClosed() const
	{
	}
} // namespace UE::PixelStreaming
