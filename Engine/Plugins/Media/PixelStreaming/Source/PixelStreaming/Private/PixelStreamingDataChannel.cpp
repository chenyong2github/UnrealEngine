// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingDataChannel.h"
#include "PixelStreamingPeerConnection.h"
#include "PixelStreamingPrivate.h"
#include "Utils.h"

TSharedPtr<FPixelStreamingDataChannel> FPixelStreamingDataChannel::Create(rtc::scoped_refptr<webrtc::DataChannelInterface> InChannel)
{
	return TSharedPtr<FPixelStreamingDataChannel>(new FPixelStreamingDataChannel(InChannel));
}

TSharedPtr<FPixelStreamingDataChannel> FPixelStreamingDataChannel::Create(FPixelStreamingPeerConnection& Connection, int32 SendStreamId, int32 RecvStreamId)
{
	return TSharedPtr<FPixelStreamingDataChannel>(new FPixelStreamingDataChannel(Connection, SendStreamId, RecvStreamId));
}

FPixelStreamingDataChannel::FPixelStreamingDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> InChannel)
	: SendChannel(InChannel)
	, RecvChannel(InChannel)
{
	checkf(RecvChannel, TEXT("Channel cannot be null"));
	RecvChannel->RegisterObserver(this);
}

FPixelStreamingDataChannel::FPixelStreamingDataChannel(FPixelStreamingPeerConnection& Connection, int32 SendStreamId, int32 RecvStreamId)
{
	rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection = Connection.PeerConnection;

	webrtc::DataChannelInit SendConfig;
	SendConfig.negotiated = true;
	SendConfig.id = SendStreamId;
	RecvChannel = SendChannel = PeerConnection->CreateDataChannel((SendStreamId == RecvStreamId) ? "datachannel" : "senddatachannel", &SendConfig);

	if (SendStreamId != RecvStreamId)
	{
		webrtc::DataChannelInit RecvConfig;
		RecvConfig.negotiated = true;
		RecvConfig.id = RecvStreamId;
		RecvChannel = PeerConnection->CreateDataChannel("recvdatachannel", &RecvConfig);
	}

	checkf(SendChannel, TEXT("Send channel cannot be null"));
	checkf(RecvChannel, TEXT("Recv channel cannot be null"));
	RecvChannel->RegisterObserver(this);
}

FPixelStreamingDataChannel::~FPixelStreamingDataChannel()
{
	RecvChannel->UnregisterObserver();
}

bool FPixelStreamingDataChannel::SendArbitraryData(uint8 Type, const TArray64<uint8>& DataBytes) const
{
	using namespace UE::PixelStreaming;

	if (!SendChannel)
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("Cannot send arbitrary data when data channel is null."));
		return false;
	}

	// int32 results in a maximum 4GB file (4,294,967,296 bytes)
	const int32 DataSize = DataBytes.Num();

	// Maximum size of a single buffer should be 16KB as this is spec compliant message length for a single data channel transmission
	const int32 MaxBufferBytes = 16 * 1024;
	const int32 MessageHeader = sizeof(Type) + sizeof(DataSize);
	const int32 MaxDataBytesPerMsg = MaxBufferBytes - MessageHeader;

	int32 BytesTransmitted = 0;

	while (BytesTransmitted < DataSize)
	{
		int32 RemainingBytes = DataSize - BytesTransmitted;
		int32 BytesToTransmit = FGenericPlatformMath::Min(MaxDataBytesPerMsg, RemainingBytes);

		rtc::CopyOnWriteBuffer Buffer(MessageHeader + BytesToTransmit);

		size_t Pos = 0;

		// Write message type
		Pos = SerializeToBuffer(Buffer, Pos, &Type, sizeof(Type));

		// Write size of payload
		Pos = SerializeToBuffer(Buffer, Pos, &DataSize, sizeof(DataSize));

		// Write the data bytes payload
		Pos = SerializeToBuffer(Buffer, Pos, DataBytes.GetData() + BytesTransmitted, BytesToTransmit);

		uint64_t BufferBefore = SendChannel->buffered_amount();
		while (BufferBefore + BytesToTransmit >= 16 * 1024 * 1024) // 16MB (WebRTC Data Channel buffer size)
		{
			// As per UE docs a Sleep of 0.0 simply lets other threads take CPU cycles while this is happening.
			FPlatformProcess::Sleep(0.0);
			BufferBefore = SendChannel->buffered_amount();
		}

		if (!SendChannel->Send(webrtc::DataBuffer(Buffer, true)))
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to send data channel packet"));
			return false;
		}

		// Increment the number of bytes transmitted
		BytesTransmitted += BytesToTransmit;
	}
	return true;
}

void FPixelStreamingDataChannel::OnStateChange()
{
	// Dispatch this callback to the game thread since we don't want to delay
	// the signalling thread or block it with mutexes etc.
	TWeakPtr<FPixelStreamingDataChannel> WeakChannel = AsShared();
	webrtc::DataChannelInterface::DataState NewState = RecvChannel->state();
	AsyncTask(ENamedThreads::GameThread, [WeakChannel, NewState]() {
		if (TSharedPtr<FPixelStreamingDataChannel> DataChannel = WeakChannel.Pin())
		{
			switch (NewState)
			{
				case webrtc::DataChannelInterface::DataState::kOpen:
				{
					DataChannel->OnOpen.Broadcast(*DataChannel);
					break;
				}
				case webrtc::DataChannelInterface::DataState::kConnecting:
				case webrtc::DataChannelInterface::DataState::kClosing:
					break;
				case webrtc::DataChannelInterface::DataState::kClosed:
				{
					DataChannel->OnClosed.Broadcast(*DataChannel);
					break;
				}
			}
		}
	});
}

void FPixelStreamingDataChannel::OnMessage(const webrtc::DataBuffer& Buffer)
{
	// Dispatch this callback to the game thread since we don't want to delay
	// the signalling thread or block it with mutexes etc.
	AsyncTask(ENamedThreads::GameThread, [this, Buffer = Buffer]() {
		const uint8 MsgType = static_cast<uint8>(Buffer.data.data()[0]);
		OnMessageReceived.Broadcast(MsgType, Buffer);
	});
}
