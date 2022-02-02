// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerSession.h"
#include "ToStringExtensions.h"
#include "VideoEncoderRTC.h"
#include "VideoEncoderFactory.h"
#include "EncoderFactory.h"
#include "VideoEncoder.h"
#include "Settings.h"
#include "WebRTCIncludes.h"
#include "Containers/UnrealString.h"
#include "AudioSink.h"
#include "GenericPlatform/GenericPlatformMath.h"
#include "SignallingServerConnection.h"
#include "Async/Async.h"
#include "SessionDescriptionObservers.h"
#include "Stats.h"
#include "PixelStreamingStatNames.h"

class QPConsumer : public IPixelStreamingStatsConsumer
{
public:
	QPConsumer(UE::PixelStreaming::IPixelStreamingSessions* InSessions)
		: Sessions(InSessions) {}

	QPConsumer(QPConsumer&& Other)
		: Sessions(Other.Sessions) {}

	// Begin IPixelStreamingStatsConsumer
	void ConsumeStat(FPixelStreamingPlayerId PlayerId, FName StatName, float StatValue) override { Sessions->SendLatestQP(PlayerId, (int)StatValue); }
	// End IPixelStreamingStatsConsumer
private:
	UE::PixelStreaming::IPixelStreamingSessions* Sessions;
};

UE::PixelStreaming::FPlayerSession::FPlayerSession(UE::PixelStreaming::IPixelStreamingSessions* InSessions, UE::PixelStreaming::FSignallingServerConnection* InSignallingServerConnection, FPixelStreamingPlayerId InPlayerId)
	: SignallingServerConnection(InSignallingServerConnection)
	, PlayerId(InPlayerId)
	, DataChannelObserver(InSessions, InPlayerId)
	, WebRTCStatsCallback(new rtc::RefCountedObject<UE::PixelStreaming::FRTCStatsCollector>(InPlayerId))
	, QPReporter(MakeShared<QPConsumer>(InSessions))
{
	UE_LOG(LogPixelStreaming, Log, TEXT("%s: PlayerId=%s"), TEXT("Created UE::PixelStreaming::FPlayerSession::FPlayerSession"), *PlayerId);

	// Listen for changes on QP for this peer and when there is a change send that QP over the data channel to the browser
	UE::PixelStreaming::FStats* Stats = UE::PixelStreaming::FStats::Get();
	if (Stats)
	{
		Stats->AddOnPeerStatChangedCallback(PlayerId,
			PixelStreamingStatNames::MeanQPPerSecond,
			TWeakPtr<IPixelStreamingStatsConsumer>(QPReporter));
	}
}

UE::PixelStreaming::FPlayerSession::~FPlayerSession()
{
	UE_LOG(LogPixelStreaming, Log, TEXT("%s: PlayerId=%s"), TEXT("UE::PixelStreaming::FPlayerSession::~FPlayerSession"), *PlayerId);

	DataChannelObserver.Unregister();
	DataChannel = nullptr;

	if (PeerConnection)
	{
		PeerConnection->Close();
		PeerConnection = nullptr;
	}

	UE::PixelStreaming::FStats* Stats = UE::PixelStreaming::FStats::Get();
	if (Stats)
	{
		Stats->RemovePeersStats(PlayerId);
	}
}

void UE::PixelStreaming::FPlayerSession::PollWebRTCStats() const
{
	check(PeerConnection);
	//PeerConnection->GetStats(WebRTCStatsCallback);
	std::vector<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> Transceivers = PeerConnection->GetTransceivers();
	for (rtc::scoped_refptr<webrtc::RtpTransceiverInterface> Transceiver : Transceivers)
	{
		if (Transceiver->media_type() == cricket::MediaType::MEDIA_TYPE_VIDEO)
		{
			PeerConnection->GetStats(Transceiver->sender(), WebRTCStatsCallback);
		}
	}
}

UE::PixelStreaming::FDataChannelObserver& UE::PixelStreaming::FPlayerSession::GetDataChannelObserver()
{
	return DataChannelObserver;
}

webrtc::PeerConnectionInterface& UE::PixelStreaming::FPlayerSession::GetPeerConnection()
{
	check(PeerConnection);
	return *PeerConnection;
}

void UE::PixelStreaming::FPlayerSession::SetPeerConnection(const rtc::scoped_refptr<webrtc::PeerConnectionInterface>& InPeerConnection)
{
	PeerConnection = InPeerConnection;
}

void UE::PixelStreaming::FPlayerSession::SetDataChannel(const rtc::scoped_refptr<webrtc::DataChannelInterface>& InDataChannel)
{
	DataChannel = InDataChannel;
}

void UE::PixelStreaming::FPlayerSession::SetVideoSource(const rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>& InVideoSource)
{
	VideoSource = InVideoSource;
}

FPixelStreamingPlayerId UE::PixelStreaming::FPlayerSession::GetPlayerId() const
{
	return PlayerId;
}

void UE::PixelStreaming::FPlayerSession::AddSinkToAudioTrack()
{
	// Get remote audio track from transceiver and if it exists then add our audio sink to it.
	std::vector<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> Transceivers = PeerConnection->GetTransceivers();
	for (rtc::scoped_refptr<webrtc::RtpTransceiverInterface>& Transceiver : Transceivers)
	{
		if (Transceiver->media_type() == cricket::MediaType::MEDIA_TYPE_AUDIO)
		{
			rtc::scoped_refptr<webrtc::RtpReceiverInterface> Receiver = Transceiver->receiver();
			rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> Track = Receiver->track();
			if (Track)
			{
				webrtc::AudioTrackInterface* AudioTrack = static_cast<webrtc::AudioTrackInterface*>(Track.get());
				AudioTrack->AddSink(&AudioSink);
			}
		}
	}
}

void UE::PixelStreaming::FPlayerSession::OnAnswer(FString Sdp)
{
	webrtc::SdpParseError Error;
	std::unique_ptr<webrtc::SessionDescriptionInterface> SessionDesc = webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, to_string(Sdp), &Error);
	if (!SessionDesc)
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("Failed to parse answer's SDP\n%s"), *Sdp);
		return;
	}

	UE::PixelStreaming::FSetSessionDescriptionObserver* SetRemoteDescriptionObserver = UE::PixelStreaming::FSetSessionDescriptionObserver::Create(
		[this]() // on success
		{
			AddSinkToAudioTrack();
		},
		[](const FString& Error) // on failure
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to set remote description: %s"), *Error);
		});

	PeerConnection->SetRemoteDescription(SetRemoteDescriptionObserver, SessionDesc.release());
}

void UE::PixelStreaming::FPlayerSession::OnRemoteIceCandidate(const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp)
{
	webrtc::SdpParseError Error;
	std::unique_ptr<webrtc::IceCandidateInterface> Candidate(webrtc::CreateIceCandidate(SdpMid, SdpMLineIndex, Sdp, &Error));
	if (Candidate)
	{
		PeerConnection->AddIceCandidate(std::move(Candidate), [this](webrtc::RTCError error) {
			if (!error.ok())
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("AddIceCandidate failed (%s): %S"), *PlayerId, error.message());
			}
		});
	}
	else
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("Could not create ice candidate for player %s"), *PlayerId);
		UE_LOG(LogPixelStreaming, Error, TEXT("Bad sdp at line %s | Description: %s"), *FString(Error.line.c_str()), *FString(Error.description.c_str()));
	}
}

void UE::PixelStreaming::FPlayerSession::DisconnectPlayer(const FString& Reason)
{
	if (bDisconnecting)
	{
		return; // already notified SignallingServer to disconnect this player
	}

	bDisconnecting = true;
	SignallingServerConnection->SendDisconnectPlayer(PlayerId, Reason);
}

UE::PixelStreaming::FAudioSink& UE::PixelStreaming::FPlayerSession::GetAudioSink()
{
	return AudioSink;
}

bool UE::PixelStreaming::FPlayerSession::SendMessage(UE::PixelStreaming::Protocol::EToPlayerMsg Type, const FString& Descriptor) const
{
	if (!DataChannel)
	{
		return false;
	}

	const uint8 MessageType = static_cast<uint8>(Type);
	const size_t DescriptorSize = Descriptor.Len() * sizeof(TCHAR);

	rtc::CopyOnWriteBuffer Buffer(sizeof(MessageType) + DescriptorSize);

	size_t Pos = 0;
	Pos = SerializeToBuffer(Buffer, Pos, &MessageType, sizeof(MessageType));
	Pos = SerializeToBuffer(Buffer, Pos, *Descriptor, DescriptorSize);

	return DataChannel->Send(webrtc::DataBuffer(Buffer, true));
}

void UE::PixelStreaming::FPlayerSession::SendQualityControlStatus(bool bIsQualityController) const
{
	UE::PixelStreaming::FStats::Get()->StorePeerStat(PlayerId, UE::PixelStreaming::FStatData(PixelStreamingStatNames::QualityController, bIsQualityController ? 1.0 : 0.0, 0));

	if (!DataChannel)
	{
		return;
	}

	const uint8 MessageType = static_cast<uint8>(UE::PixelStreaming::Protocol::EToPlayerMsg::QualityControlOwnership);
	const uint8 ControlsQuality = bIsQualityController ? 1 : 0;

	rtc::CopyOnWriteBuffer Buffer(sizeof(MessageType) + sizeof(ControlsQuality));

	size_t Pos = 0;
	Pos = SerializeToBuffer(Buffer, Pos, &MessageType, sizeof(MessageType));
	Pos = SerializeToBuffer(Buffer, Pos, &ControlsQuality, sizeof(ControlsQuality));

	if (!DataChannel->Send(webrtc::DataBuffer(Buffer, true)))
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("failed to send quality control status"));
	}
}

void UE::PixelStreaming::FPlayerSession::SendFreezeFrame(const TArray64<uint8>& JpegBytes) const
{
	UE::PixelStreaming::FPlayerSession::SendArbitraryData(static_cast<TArray<uint8>>(JpegBytes), static_cast<uint8>(UE::PixelStreaming::Protocol::EToPlayerMsg::FreezeFrame));
}

void UE::PixelStreaming::FPlayerSession::SendFileData(const TArray<uint8>& ByteData, const FString& MimeType, const FString& FileExtension) const
{
	// Send the mime type first
	UE::PixelStreaming::FPlayerSession::SendMessage(UE::PixelStreaming::Protocol::EToPlayerMsg::FileMimeType, MimeType);
	// Send the extension next
	UE::PixelStreaming::FPlayerSession::SendMessage(UE::PixelStreaming::Protocol::EToPlayerMsg::FileExtension, FileExtension);
	// Send the contents of the file
	AsyncTask(ENamedThreads::AnyHiPriThreadHiPriTask, [this, ByteData]() {
		UE::PixelStreaming::FPlayerSession::SendArbitraryData(ByteData, static_cast<uint8>(UE::PixelStreaming::Protocol::EToPlayerMsg::FileContents));
	});
}

void UE::PixelStreaming::FPlayerSession::SendUnfreezeFrame() const
{
	if (!DataChannel)
	{
		return;
	}

	const uint8 MessageType = static_cast<uint8>(UE::PixelStreaming::Protocol::EToPlayerMsg::UnfreezeFrame);

	rtc::CopyOnWriteBuffer Buffer(sizeof(MessageType));

	size_t Pos = 0;
	Pos = SerializeToBuffer(Buffer, Pos, &MessageType, sizeof(MessageType));

	if (!DataChannel->Send(webrtc::DataBuffer(Buffer, true)))
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("failed to send unfreeze frame"));
	}
}

void UE::PixelStreaming::FPlayerSession::SendArbitraryData(const TArray<uint8>& DataBytes, const uint8 MessageType) const
{
	if (!DataChannel)
	{
		return;
	}

	// int32 results in a maximum 4GB file (4,294,967,296 bytes)
	const int32 DataSize = DataBytes.Num();

	// Maximum size of a single buffer should be 16KB as this is spec compliant message length for a single data channel transmission
	const int32 MaxBufferBytes = 16 * 1024;
	const int32 MessageHeader = sizeof(MessageType) + sizeof(DataSize);
	const int32 MaxDataBytesPerMsg = MaxBufferBytes - MessageHeader;

	int32 BytesTransmitted = 0;

	while (BytesTransmitted < DataSize)
	{
		int32 RemainingBytes = DataSize - BytesTransmitted;
		int32 BytesToTransmit = FGenericPlatformMath::Min(MaxDataBytesPerMsg, RemainingBytes);

		rtc::CopyOnWriteBuffer Buffer(MessageHeader + BytesToTransmit);

		size_t Pos = 0;

		// Write message type
		Pos = SerializeToBuffer(Buffer, Pos, &MessageType, sizeof(MessageType));
		// Write size of payload
		Pos = SerializeToBuffer(Buffer, Pos, &DataSize, sizeof(DataSize));
		// Write the data bytes payload
		Pos = SerializeToBuffer(Buffer, Pos, DataBytes.GetData() + BytesTransmitted, BytesToTransmit);

		uint64_t BufferBefore = DataChannel->buffered_amount();
		while (BufferBefore + BytesToTransmit >= 16 * 1024 * 1024) // 16MB (WebRTC Data Channel buffer size)
		{
			FPlatformProcess::Sleep(0.000001f); // sleep 1 microsecond
			BufferBefore = DataChannel->buffered_amount();
		}

		if (!DataChannel->Send(webrtc::DataBuffer(Buffer, true)))
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to send data channel packet"));
			return;
		}

		// Increment the number of bytes transmitted
		BytesTransmitted += BytesToTransmit;
	}
}

//
// webrtc::PeerConnectionObserver implementation.
//

void UE::PixelStreaming::FPlayerSession::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState NewState)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("%s : PlayerId=%s, NewState=%s"), TEXT("UE::PixelStreaming::FPlayerSession::OnSignalingChange"), *PlayerId, ToString(NewState));
}

// Called when a remote stream is added
void UE::PixelStreaming::FPlayerSession::OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("%s : PlayerId=%s, Stream=%s"), TEXT("UE::PixelStreaming::FPlayerSession::OnAddStream"), *PlayerId, *ToString(Stream->id()));
}

void UE::PixelStreaming::FPlayerSession::OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("%s : PlayerId=%s, Stream=%s"), TEXT("UE::PixelStreaming::FPlayerSession::OnRemoveStream"), *PlayerId, *ToString(Stream->id()));
}

void UE::PixelStreaming::FPlayerSession::OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> InDataChannel)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("%s : PlayerId=%s"), TEXT("UE::PixelStreaming::FPlayerSession::OnDataChannel"), *PlayerId);
	DataChannel = InDataChannel;
	DataChannelObserver.Register(InDataChannel);
}

void UE::PixelStreaming::FPlayerSession::OnRenegotiationNeeded()
{
	UE_LOG(LogPixelStreaming, Log, TEXT("%s : PlayerId=%s"), TEXT("UE::PixelStreaming::FPlayerSession::OnRenegotiationNeeded"), *PlayerId);
}

void UE::PixelStreaming::FPlayerSession::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState NewState)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("%s : PlayerId=%s, NewState=%s"), TEXT("UE::PixelStreaming::FPlayerSession::OnIceConnectionChange"), *PlayerId, ToString(NewState));
}

void UE::PixelStreaming::FPlayerSession::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState NewState)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("%s : PlayerId=%s, NewState=%s"), TEXT("UE::PixelStreaming::FPlayerSession::OnIceGatheringChange"), *PlayerId, ToString(NewState));
}

void UE::PixelStreaming::FPlayerSession::OnIceCandidate(const webrtc::IceCandidateInterface* Candidate)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("%s : PlayerId=%s"), TEXT("UE::PixelStreaming::FPlayerSession::OnIceCandidate"), *PlayerId);

	SignallingServerConnection->SendIceCandidate(PlayerId, *Candidate);
}

void UE::PixelStreaming::FPlayerSession::OnIceCandidatesRemoved(const std::vector<cricket::Candidate>& candidates)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("%s : PlayerId=%s"), TEXT("UE::PixelStreaming::FPlayerSession::OnIceCandidatesRemoved"), *PlayerId);
}

void UE::PixelStreaming::FPlayerSession::OnIceConnectionReceivingChange(bool Receiving)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("%s : PlayerId=%s, Receiving=%d"), TEXT("UE::PixelStreaming::FPlayerSession::OnIceConnectionReceivingChange"), *PlayerId, *reinterpret_cast<int8*>(&Receiving));
}

void UE::PixelStreaming::FPlayerSession::OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("%s : PlayerId=%s"), TEXT("UE::PixelStreaming::FPlayerSession::OnTrack"), *PlayerId);

	AddSinkToAudioTrack();

	// print out track type
	cricket::MediaType mediaType = transceiver->media_type();
	switch (mediaType)
	{
		case cricket::MediaType::MEDIA_TYPE_AUDIO:
			UE_LOG(LogPixelStreaming, Log, TEXT("Track was type: audio"));
			break;
		case cricket::MediaType::MEDIA_TYPE_VIDEO:
			UE_LOG(LogPixelStreaming, Log, TEXT("Track was type: video"));
			break;
		case cricket::MediaType::MEDIA_TYPE_DATA:
			UE_LOG(LogPixelStreaming, Log, TEXT("Track was type: data"));
			break;
		default:
			UE_LOG(LogPixelStreaming, Log, TEXT("Track was an unsupported type"));
			break;
	}

	// print out track direction
	webrtc::RtpTransceiverDirection Direction = transceiver->direction();
	switch (Direction)
	{
		case webrtc::RtpTransceiverDirection::kSendRecv:
			UE_LOG(LogPixelStreaming, Log, TEXT("Track direction: send+recv"));
			break;
		case webrtc::RtpTransceiverDirection::kSendOnly:
			UE_LOG(LogPixelStreaming, Log, TEXT("Track direction: send only"));
			break;
		case webrtc::RtpTransceiverDirection::kRecvOnly:
			UE_LOG(LogPixelStreaming, Log, TEXT("Track direction: recv only"));
			break;
		case webrtc::RtpTransceiverDirection::kInactive:
			UE_LOG(LogPixelStreaming, Log, TEXT("Track direction: inactive"));
			break;
		case webrtc::RtpTransceiverDirection::kStopped:
			UE_LOG(LogPixelStreaming, Log, TEXT("Track direction: stopped"));
			break;
	}

	rtc::scoped_refptr<webrtc::RtpReceiverInterface> Receiver = transceiver->receiver();
	if (mediaType != cricket::MediaType::MEDIA_TYPE_AUDIO)
	{
		return;
	}

	rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> MediaStreamTrack = Receiver->track();
	FString TrackEnabledStr = MediaStreamTrack->enabled() ? FString("Enabled") : FString("Disabled");
	FString TrackStateStr = MediaStreamTrack->state() == webrtc::MediaStreamTrackInterface::TrackState::kLive ? FString("Live") : FString("Ended");
	UE_LOG(LogPixelStreaming, Log, TEXT("MediaStreamTrack id: %s | Is enabled: %s | State: %s"), *FString(MediaStreamTrack->id().c_str()), *TrackEnabledStr, *TrackStateStr);

	webrtc::AudioTrackInterface* AudioTrackPtr = static_cast<webrtc::AudioTrackInterface*>(MediaStreamTrack.get());
	webrtc::AudioSourceInterface* AudioSourcePtr = AudioTrackPtr->GetSource();
	FString AudioSourceStateStr = AudioSourcePtr->state() == webrtc::MediaSourceInterface::SourceState::kLive ? FString("Live") : FString("Not live");
	FString AudioSourceRemoteStr = AudioSourcePtr->remote() ? FString("Remote") : FString("Local");
	UE_LOG(LogPixelStreaming, Log, TEXT("AudioSource | State: %s | Locality: %s"), *AudioSourceStateStr, *AudioSourceRemoteStr);
}

void UE::PixelStreaming::FPlayerSession::OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("%s : PlayerId=%s"), TEXT("UE::PixelStreaming::FPlayerSession::OnRemoveTrack"), *PlayerId);
}

size_t UE::PixelStreaming::FPlayerSession::SerializeToBuffer(rtc::CopyOnWriteBuffer& Buffer, size_t Pos, const void* Data, size_t DataSize) const
{
	FMemory::Memcpy(&Buffer[Pos], reinterpret_cast<const uint8_t*>(Data), DataSize);
	return Pos + DataSize;
}

void UE::PixelStreaming::FPlayerSession::SendVideoEncoderQP(double QP) const
{
	if (!SendMessage(UE::PixelStreaming::Protocol::EToPlayerMsg::VideoEncoderAvgQP, FString::FromInt(QP)))
	{
		UE_LOG(LogPixelStreaming, Verbose, TEXT("Failed to send video encoder QP to peer."));
	}
}
