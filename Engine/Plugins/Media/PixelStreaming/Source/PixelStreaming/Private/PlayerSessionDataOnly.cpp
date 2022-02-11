// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerSessionDataOnly.h"
#include "PixelStreamingDelegates.h"
#include "PixelStreamingPrivate.h"
#include "Utils.h"

namespace UE::PixelStreaming
{
	FPlayerSessionDataOnly::FPlayerSessionDataOnly(FPlayerSessions* InSessions,
		FSignallingServerConnection* InSignallingServerConnection,
		FPixelStreamingPlayerId InPlayerId,
		webrtc::PeerConnectionInterface* InPeerConnection,
		int32 SendStreamId,
		int32 RecvStreamId)
		: SignallingServerConnection(InSignallingServerConnection)
		, PlayerId(InPlayerId)
		, PeerConnection(InPeerConnection)
		, DataChannelObserver(InSessions, InPlayerId)
	{
		webrtc::DataChannelInit SendDataChannelConfig;
		SendDataChannelConfig.reliable = true;
		SendDataChannelConfig.ordered = true;
		SendDataChannelConfig.negotiated = true;
		SendDataChannelConfig.id = SendStreamId;

		SendDataChannel = PeerConnection->CreateDataChannel("datachannel", &SendDataChannelConfig);

		if (SendStreamId != RecvStreamId)
		{
			webrtc::DataChannelInit RecvDataChannelConfig;
			RecvDataChannelConfig.reliable = true;
			RecvDataChannelConfig.ordered = true;
			RecvDataChannelConfig.negotiated = true;
			RecvDataChannelConfig.id = RecvStreamId;
			RecvDataChannel = PeerConnection->CreateDataChannel("datachannel", &RecvDataChannelConfig);
		}
		else
		{
			RecvDataChannel = SendDataChannel;
		}
		RecvDataChannel->RegisterObserver(&DataChannelObserver);
	}

	FPlayerSessionDataOnly::~FPlayerSessionDataOnly()
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("%s: PlayerId=%s"), TEXT("FPlayerSessionDataOnly::~FPlayerSessionDataOnly"), *PlayerId);

		RecvDataChannel->UnregisterObserver();
		SendDataChannel = nullptr;
		RecvDataChannel = nullptr;
	}

	bool FPlayerSessionDataOnly::SendMessage(Protocol::EToPlayerMsg Type, const FString& Descriptor) const
	{
		if (!SendDataChannel)
		{
			return false;
		}

		const uint8 MessageType = static_cast<uint8>(Type);
		const size_t DescriptorSize = Descriptor.Len() * sizeof(TCHAR);

		rtc::CopyOnWriteBuffer Buffer(sizeof(MessageType) + DescriptorSize);

		size_t Pos = 0;
		Pos = SerializeToBuffer(Buffer, Pos, &MessageType, sizeof(MessageType));
		Pos = SerializeToBuffer(Buffer, Pos, *Descriptor, DescriptorSize);

		return SendDataChannel->Send(webrtc::DataBuffer(Buffer, true));
	}

	void FPlayerSessionDataOnly::OnDataChannelClosed() const
	{
	}
} // namespace UE::PixelStreaming
