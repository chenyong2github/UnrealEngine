// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerSession.h"
#include "VideoEncoder.h"
#include "VideoEncoderFactory.h"
#include "PixelStreamingEncoderFactory.h"
#include "PixelStreamingVideoEncoder.h"
#include "PixelStreamingSettings.h"
#include "WebRTCIncludes.h"
#include "Containers/UnrealString.h"
#include "PixelStreamingAudioSink.h"
#include "GenericPlatform/GenericPlatformMath.h"
#include "SignallingServerConnection.h"
#include "Async/Async.h"
#include "WebRtcObservers.h"
#include "PixelStreamingStats.h"
#include "PixelStreamingStatNames.h"

class QPConsumer : public IPixelStreamingStatsConsumer
{
public:
	QPConsumer(IPixelStreamingSessions* InSessions)
		: Sessions(InSessions) {}

	QPConsumer(QPConsumer&& Other)
		: Sessions(Other.Sessions) {}

	// Begin IPixelStreamingStatsConsumer
	void ConsumeStat(FPlayerId PlayerId, FName StatName, float StatValue) override { Sessions->SendLatestQP(PlayerId, (int)StatValue); }
	// End IPixelStreamingStatsConsumer
private:
	IPixelStreamingSessions* Sessions;
};

FPlayerSession::FPlayerSession(IPixelStreamingSessions* InSessions, FSignallingServerConnection* InSignallingServerConnection, FPlayerId InPlayerId)
	: SignallingServerConnection(InSignallingServerConnection)
	, PlayerId(InPlayerId)
	, DataChannelObserver(InSessions, InPlayerId)
	, WebRTCStatsCallback(new rtc::RefCountedObject<FPixelStreamingRTCStatsCollector>(InPlayerId))
	, QPReporter(MakeShared<QPConsumer>(InSessions))
{
	UE_LOG(PixelStreamer, Log, TEXT("%s: PlayerId=%s"), TEXT("Created FPlayerSession::FPlayerSession"), *PlayerId);

	// Listen for changes on QP for this peer and when there is a change send that QP over the data channel to the browser
	FPixelStreamingStats* Stats = FPixelStreamingStats::Get();
	if (Stats)
	{
		Stats->AddOnPeerStatChangedCallback(PlayerId,
			PixelStreamingStatNames::MeanQPPerSecond,
			TWeakPtr<IPixelStreamingStatsConsumer>(QPReporter));
	}
}

FPlayerSession::~FPlayerSession()
{
	UE_LOG(PixelStreamer, Log, TEXT("%s: PlayerId=%s"), TEXT("FPlayerSession::~FPlayerSession"), *PlayerId);

	this->DataChannelObserver.Unregister();
	this->DataChannel = nullptr;

	if (this->PeerConnection)
	{
		this->PeerConnection->Close();
		this->PeerConnection = nullptr;
	}

	FPixelStreamingStats* Stats = FPixelStreamingStats::Get();
	if (Stats)
	{
		Stats->RemovePeersStats(PlayerId);
	}
}

void FPlayerSession::PollWebRTCStats() const
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

FPixelStreamingDataChannelObserver& FPlayerSession::GetDataChannelObserver()
{
	return this->DataChannelObserver;
}

webrtc::PeerConnectionInterface& FPlayerSession::GetPeerConnection()
{
	check(PeerConnection);
	return *PeerConnection;
}

void FPlayerSession::SetPeerConnection(const rtc::scoped_refptr<webrtc::PeerConnectionInterface>& InPeerConnection)
{
	PeerConnection = InPeerConnection;
}

void FPlayerSession::SetDataChannel(const rtc::scoped_refptr<webrtc::DataChannelInterface>& InDataChannel)
{
	DataChannel = InDataChannel;
}

FPlayerId FPlayerSession::GetPlayerId() const
{
	return PlayerId;
}

void FPlayerSession::OnAnswer(FString Sdp)
{
	webrtc::SdpParseError Error;
	std::unique_ptr<webrtc::SessionDescriptionInterface> SessionDesc = webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, to_string(Sdp), &Error);
	if (!SessionDesc)
	{
		UE_LOG(PixelStreamer, Error, TEXT("Failed to parse answer's SDP\n%s"), *Sdp);
		return;
	}

	FSetSessionDescriptionObserver* SetRemoteDescriptionObserver = FSetSessionDescriptionObserver::Create(
		[]() // on success
		{
		},
		[](const FString& Error) // on failure
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to set remote description: %s"), *Error);
		});

	PeerConnection->SetRemoteDescription(SetRemoteDescriptionObserver, SessionDesc.release());
}

void FPlayerSession::OnRemoteIceCandidate(const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp)
{
	webrtc::SdpParseError Error;
	std::unique_ptr<webrtc::IceCandidateInterface> Candidate(webrtc::CreateIceCandidate(SdpMid, SdpMLineIndex, Sdp, &Error));
	if (Candidate)
	{
		PeerConnection->AddIceCandidate(std::move(Candidate), [&](webrtc::RTCError error) {
			if (!error.ok())
			{
				UE_LOG(PixelStreamer, Error, TEXT("AddIceCandidate failed (%s): %S"), *PlayerId, error.message());
			}
		});
	}
	else
	{
		UE_LOG(PixelStreamer, Error, TEXT("Could not create ice candidate for player %s"), *this->PlayerId);
		UE_LOG(PixelStreamer, Error, TEXT("Bad sdp at line %s | Description: %s"), *FString(Error.line.c_str()), *FString(Error.description.c_str()));
	}
}

void FPlayerSession::DisconnectPlayer(const FString& Reason)
{
	if (bDisconnecting)
	{
		return; // already notified SignallingServer to disconnect this player
	}

	bDisconnecting = true;
	this->SignallingServerConnection->SendDisconnectPlayer(PlayerId, Reason);
}

FPixelStreamingAudioSink& FPlayerSession::GetAudioSink()
{
	return this->AudioSink;
}

bool FPlayerSession::SendMessage(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const
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

void FPlayerSession::SendQualityControlStatus(bool bIsQualityController) const
{
	if (!DataChannel)
	{
		return;
	}

	const uint8 MessageType = static_cast<uint8>(PixelStreamingProtocol::EToPlayerMsg::QualityControlOwnership);
	const uint8 ControlsQuality = bIsQualityController ? 1 : 0;

	rtc::CopyOnWriteBuffer Buffer(sizeof(MessageType) + sizeof(ControlsQuality));

	size_t Pos = 0;
	Pos = SerializeToBuffer(Buffer, Pos, &MessageType, sizeof(MessageType));
	Pos = SerializeToBuffer(Buffer, Pos, &ControlsQuality, sizeof(ControlsQuality));

	if (!DataChannel->Send(webrtc::DataBuffer(Buffer, true)))
	{
		UE_LOG(PixelStreamer, Error, TEXT("failed to send quality control status"));
	}
}

void FPlayerSession::SendFreezeFrame(const TArray64<uint8>& JpegBytes) const
{
	SendArbitraryData(this->DataChannel, static_cast<TArray<uint8>>(JpegBytes), static_cast<uint8>(PixelStreamingProtocol::EToPlayerMsg::FreezeFrame));
}

void FPlayerSession::SendFileData(const TArray<uint8>& ByteData, const FString& MimeType, const FString& FileExtension) const
{
	// Send the mime type first
	FPlayerSession::SendMessage(PixelStreamingProtocol::EToPlayerMsg::FileMimeType, MimeType);
	// Send the extension next
	FPlayerSession::SendMessage(PixelStreamingProtocol::EToPlayerMsg::FileExtension, FileExtension);
	// Send the contents of the file
	AsyncTask(ENamedThreads::AnyHiPriThreadHiPriTask, [&, ByteData]() {
		SendArbitraryData(this->DataChannel, ByteData, static_cast<uint8>(PixelStreamingProtocol::EToPlayerMsg::FileContents));
	});
}

void FPlayerSession::SendUnfreezeFrame() const
{
	if (!DataChannel)
	{
		return;
	}

	const uint8 MessageType = static_cast<uint8>(PixelStreamingProtocol::EToPlayerMsg::UnfreezeFrame);

	rtc::CopyOnWriteBuffer Buffer(sizeof(MessageType));

	size_t Pos = 0;
	Pos = SerializeToBuffer(Buffer, Pos, &MessageType, sizeof(MessageType));

	if (!DataChannel->Send(webrtc::DataBuffer(Buffer, true)))
	{
		UE_LOG(PixelStreamer, Error, TEXT("failed to send unfreeze frame"));
	}
}

//
// webrtc::PeerConnectionObserver implementation.
//

void FPlayerSession::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState NewState)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s, NewState=%s"), TEXT("FPlayerSession::OnSignalingChange"), *PlayerId, ToString(NewState));
}

// Called when a remote stream is added
void FPlayerSession::OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s, Stream=%s"), TEXT("FPlayerSession::OnAddStream"), *PlayerId, *ToString(Stream->id()));
}

void FPlayerSession::OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s, Stream=%s"), TEXT("FPlayerSession::OnRemoveStream"), *PlayerId, *ToString(Stream->id()));
}

void FPlayerSession::OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> InDataChannel)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s"), TEXT("FPlayerSession::OnDataChannel"), *PlayerId);
	DataChannel = InDataChannel;
	DataChannelObserver.Register(InDataChannel);
}

void FPlayerSession::OnRenegotiationNeeded()
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s"), TEXT("FPlayerSession::OnRenegotiationNeeded"), *PlayerId);
}

void FPlayerSession::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState NewState)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s, NewState=%s"), TEXT("FPlayerSession::OnIceConnectionChange"), *PlayerId, ToString(NewState));
}

void FPlayerSession::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState NewState)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s, NewState=%s"), TEXT("FPlayerSession::OnIceGatheringChange"), *PlayerId, ToString(NewState));
}

void FPlayerSession::OnIceCandidate(const webrtc::IceCandidateInterface* Candidate)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s"), TEXT("FPlayerSession::OnIceCandidate"), *PlayerId);

	this->SignallingServerConnection->SendIceCandidate(PlayerId, *Candidate);
}

void FPlayerSession::OnIceCandidatesRemoved(const std::vector<cricket::Candidate>& candidates)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s"), TEXT("FPlayerSession::OnIceCandidatesRemoved"), *PlayerId);
}

void FPlayerSession::OnIceConnectionReceivingChange(bool Receiving)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s, Receiving=%d"), TEXT("FPlayerSession::OnIceConnectionReceivingChange"), *PlayerId, *reinterpret_cast<int8*>(&Receiving));
}

void FPlayerSession::OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s"), TEXT("FPlayerSession::OnTrack"), *PlayerId);

	// print out track type
	auto mediaType = transceiver->media_type();
	switch (mediaType)
	{
		case cricket::MediaType::MEDIA_TYPE_AUDIO:
			UE_LOG(PixelStreamer, Log, TEXT("Track was type: audio"));
			break;
		case cricket::MediaType::MEDIA_TYPE_VIDEO:
			UE_LOG(PixelStreamer, Log, TEXT("Track was type: video"));
			break;
		case cricket::MediaType::MEDIA_TYPE_DATA:
			UE_LOG(PixelStreamer, Log, TEXT("Track was type: data"));
			break;
		default:
			UE_LOG(PixelStreamer, Log, TEXT("Track was an unsupported type"));
			break;
	}

	// print out track direction
	webrtc::RtpTransceiverDirection direction = transceiver->direction();
	switch (direction)
	{
		case webrtc::RtpTransceiverDirection::kSendRecv:
			UE_LOG(PixelStreamer, Log, TEXT("Track direction: send+recv"));
			break;
		case webrtc::RtpTransceiverDirection::kSendOnly:
			UE_LOG(PixelStreamer, Log, TEXT("Track direction: send only"));
			break;
		case webrtc::RtpTransceiverDirection::kRecvOnly:
			UE_LOG(PixelStreamer, Log, TEXT("Track direction: recv only"));
			break;
		case webrtc::RtpTransceiverDirection::kInactive:
			UE_LOG(PixelStreamer, Log, TEXT("Track direction: inactive"));
			break;
		case webrtc::RtpTransceiverDirection::kStopped:
			UE_LOG(PixelStreamer, Log, TEXT("Track direction: stopped"));
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
	UE_LOG(PixelStreamer, Log, TEXT("MediaStreamTrack id: %s | Is enabled: %s | State: %s"), *FString(MediaStreamTrack->id().c_str()), *TrackEnabledStr, *TrackStateStr);

	webrtc::AudioTrackInterface* AudioTrack = static_cast<webrtc::AudioTrackInterface*>(MediaStreamTrack.get());
	webrtc::AudioSourceInterface* AudioSource = AudioTrack->GetSource();
	FString AudioSourceStateStr = AudioSource->state() == webrtc::MediaSourceInterface::SourceState::kLive ? FString("Live") : FString("Not live");
	FString AudioSourceRemoteStr = AudioSource->remote() ? FString("Remote") : FString("Local");
	UE_LOG(PixelStreamer, Log, TEXT("AudioSource | State: %s | Locality: %s"), *AudioSourceStateStr, *AudioSourceRemoteStr);
	AudioSource->AddSink(&this->AudioSink);
}

void FPlayerSession::OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s"), TEXT("FPlayerSession::OnRemoveTrack"), *PlayerId);
}

size_t FPlayerSession::SerializeToBuffer(rtc::CopyOnWriteBuffer& Buffer, size_t Pos, const void* Data, size_t DataSize) const
{
	FMemory::Memcpy(&Buffer[Pos], reinterpret_cast<const uint8_t*>(Data), DataSize);
	return Pos + DataSize;
}

void FPlayerSession::SendVideoEncoderQP(double QP) const
{
	if (!SendMessage(PixelStreamingProtocol::EToPlayerMsg::VideoEncoderAvgQP, FString::FromInt(QP)))
	{
		UE_LOG(PixelStreamer, Verbose, TEXT("Failed to send video encoder QP to peer."));
	}
}
