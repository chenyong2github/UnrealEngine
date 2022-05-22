// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingPeerConnection.h"
#include "VideoDecoderFactory.h"
#include "VideoEncoderFactorySimulcast.h"
#include "PixelStreamingSessionDescriptionObservers.h"
#include "PixelStreamingPrivate.h"
#include "Settings.h"
#include "AudioCapturer.h"
#include "PixelStreamingAudioDeviceModule.h"
#include "ToStringExtensions.h"

namespace
{
	inline size_t SerializeToBuffer(rtc::CopyOnWriteBuffer& Buffer, size_t Pos, const void* Data, size_t DataSize)
	{
		FMemory::Memcpy(&Buffer[Pos], reinterpret_cast<const uint8_t*>(Data), DataSize);
		return Pos + DataSize;
	}
} // namespace

TUniquePtr<rtc::Thread> FPixelStreamingPeerConnection::SignallingThread = nullptr;
rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> FPixelStreamingPeerConnection::PeerConnectionFactory = nullptr;

TUniquePtr<FPixelStreamingPeerConnection> FPixelStreamingPeerConnection::Create(const FConfig& Config)
{
	if (!PeerConnectionFactory)
	{
		CreatePeerConnectionFactory();
	}

	TUniquePtr<FPixelStreamingPeerConnection> NewPeerConnection = TUniquePtr<FPixelStreamingPeerConnection>(new FPixelStreamingPeerConnection());
	NewPeerConnection->PeerConnection = PeerConnectionFactory->CreatePeerConnection(Config, nullptr, nullptr, NewPeerConnection.Get());

	return NewPeerConnection;
}

void FPixelStreamingPeerConnection::Shutdown()
{
	PeerConnectionFactory = nullptr;
	if (SignallingThread)
	{
		SignallingThread->Stop();
	}
	SignallingThread = nullptr;
}

void FPixelStreamingPeerConnection::SetSuccessCallback(const TFunction<void(const webrtc::SessionDescriptionInterface*)>& Callback)
{
	SuccessCallback = Callback;
}

void FPixelStreamingPeerConnection::SetFailureCallback(const TFunction<void(const FString&)>& Callback)
{
	FailureCallback = Callback;
}

void FPixelStreamingPeerConnection::SetIceCandidateCallback(const TFunction<void(const webrtc::IceCandidateInterface*)>& Callback)
{
	IceCandidateCallback = Callback;
}

void FPixelStreamingPeerConnection::SetRemoteDescription(const FString& Sdp)
{
	FPixelStreamingSetSessionDescriptionObserver* SetLocalDescriptionObserver = FPixelStreamingSetSessionDescriptionObserver::Create(
		[this]() // on success
		{
			if (SuccessCallback)
			{
				SuccessCallback(PeerConnection->local_description());
			}
		}, FailureCallback);

	FPixelStreamingCreateSessionDescriptionObserver* CreateAnswerObserver = FPixelStreamingCreateSessionDescriptionObserver::Create(
		[this, SetLocalDescriptionObserver](webrtc::SessionDescriptionInterface* SDP) {
			PeerConnection->SetLocalDescription(SetLocalDescriptionObserver, SDP);
		}, FailureCallback);

	FPixelStreamingSetSessionDescriptionObserver* SetRemoteDescriptionObserver = FPixelStreamingSetSessionDescriptionObserver::Create(
		[this, CreateAnswerObserver]() {
			// Note: these offer to receive at superseded now we are use transceivers to setup our peer connection media
			int offer_to_receive_video = 1;
			int offer_to_receive_audio = 1;
			bool voice_activity_detection = false;
			bool ice_restart = true;
			bool use_rtp_mux = true;

			webrtc::PeerConnectionInterface::RTCOfferAnswerOptions AnswerOption{
				offer_to_receive_video,
				offer_to_receive_audio,
				voice_activity_detection,
				ice_restart,
				use_rtp_mux
			};

			PeerConnection->CreateAnswer(CreateAnswerObserver, AnswerOption);
		}, FailureCallback);

	webrtc::SdpParseError Error;
	std::unique_ptr<webrtc::SessionDescriptionInterface> SessionDesc = webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, UE::PixelStreaming::ToString(Sdp), &Error);
	if (!SessionDesc)
	{
		if (FailureCallback)
		{
			FailureCallback(FString::Printf(TEXT("Failed to create session description: %s"), Error.description.c_str()));
		}
	}

	PeerConnection->SetRemoteDescription(SetRemoteDescriptionObserver, SessionDesc.release());
}

void FPixelStreamingPeerConnection::AddRemoteIceCandidate(const FString& SdpMid, int SdpMLineIndex, const FString& Sdp)
{
	webrtc::SdpParseError Error;
	std::unique_ptr<webrtc::IceCandidateInterface> Candidate(webrtc::CreateIceCandidate(UE::PixelStreaming::ToString(SdpMid), SdpMLineIndex, UE::PixelStreaming::ToString(Sdp), &Error));
	if (!Candidate)
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("Failed to create ICE candicate: %s"), Error.description.c_str());
		return;
	}

	PeerConnection->AddIceCandidate(std::move(Candidate), [](webrtc::RTCError error) {
		if (!error.ok())
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("AddIceCandidate failed: %S"), error.message());
		}
	});
}

void FPixelStreamingPeerConnection::CreateDataChannels(int32 SendStreamId, int32 RecvStreamId)
{
	// called when we manually negotiate a datachannel connection via the signalling server
	UE_LOG(LogPixelStreaming, Log, TEXT("OnPeerDataChannels"));
	webrtc::DataChannelInit SendConfig;
	SendConfig.negotiated = true;
	SendConfig.id = SendStreamId;
	rtc::scoped_refptr<webrtc::DataChannelInterface> SendDataChannel = PeerConnection->CreateDataChannel("datachannel", &SendConfig);
	DataChannel = SendDataChannel;

	if (SendStreamId != RecvStreamId)
	{
		webrtc::DataChannelInit RecvConfig;
		RecvConfig.negotiated = true;
		RecvConfig.id = RecvStreamId;
		rtc::scoped_refptr<webrtc::DataChannelInterface> RecvDataChannel = PeerConnection->CreateDataChannel("datachannel", &RecvConfig);
		RecvDataChannel->RegisterObserver(this);
	}
	else
	{
		SendDataChannel->RegisterObserver(this);
	}
}

void FPixelStreamingPeerConnection::SetVideoSink(rtc::VideoSinkInterface<webrtc::VideoFrame>* Sink)
{
	VideoSink = Sink;
}

bool FPixelStreamingPeerConnection::SendMessage(UE::PixelStreaming::Protocol::EToStreamerMsg Type, const FString& Descriptor) const
{
	if (!DataChannel)
	{
		return false;
	}

	const uint8 MessageType = static_cast<uint8>(Type);
	const size_t DescriptorSize = Descriptor.Len() * sizeof(TCHAR);

	rtc::CopyOnWriteBuffer MsgBuffer(sizeof(MessageType) + DescriptorSize);

	size_t Pos = 0;
	Pos = SerializeToBuffer(MsgBuffer, Pos, &MessageType, sizeof(MessageType));
	Pos = SerializeToBuffer(MsgBuffer, Pos, *Descriptor, DescriptorSize);

	return DataChannel->Send(webrtc::DataBuffer(MsgBuffer, true));
}

void FPixelStreamingPeerConnection::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState NewState)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("OnSignalingChange (%s)"), UE::PixelStreaming::ToString(NewState));
}

void FPixelStreamingPeerConnection::OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("OnAddStream"));
}

void FPixelStreamingPeerConnection::OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("OnRemoveStream"));
}

void FPixelStreamingPeerConnection::OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> Channel)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("OnDataChannel"));

	if (DataChannel)
	{
		UE_LOG(LogPixelStreaming, Warning, TEXT("Replacing datachannel"));
	}

	DataChannel = Channel;
	DataChannel->RegisterObserver(this);

	// OnDataChannelOpen.Broadcast(*this);
}

void FPixelStreamingPeerConnection::OnRenegotiationNeeded()
{
	UE_LOG(LogPixelStreaming, Log, TEXT("OnRenegotiationNeeded"));
}

void FPixelStreamingPeerConnection::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState NewState)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("OnIceConnectionChange (%s)"), UE::PixelStreaming::ToString(NewState));

	if (NewState == webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionConnected)
	{
		//State = EState::ConnectedStreamer;
		// OnConnected.Broadcast(*this);
	}
	else if (NewState == webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionDisconnected)
	{
		//State = EState::Disconnected;
		// OnDisconnected.Broadcast(*this);
	}
}

void FPixelStreamingPeerConnection::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState NewState)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("OnIceGatheringChange (%s)"), UE::PixelStreaming::ToString(NewState));
}

void FPixelStreamingPeerConnection::OnIceCandidate(const webrtc::IceCandidateInterface* Candidate)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("OnIceCandidate"));
	if (IceCandidateCallback)
	{
		IceCandidateCallback(Candidate);
	}
}

void FPixelStreamingPeerConnection::OnIceCandidatesRemoved(const std::vector<cricket::Candidate>& candidates)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("OnIceCandidatesRemoved"));
}

void FPixelStreamingPeerConnection::OnIceConnectionReceivingChange(bool Receiving)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("OnIceConnectionReceivingChange"));
}

void FPixelStreamingPeerConnection::OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> Transceiver)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("OnTrack"));
	if (VideoSink && Transceiver->media_type() == cricket::MediaType::MEDIA_TYPE_VIDEO)
	{
		webrtc::VideoTrackInterface* VideoTrack = static_cast<webrtc::VideoTrackInterface*>(Transceiver->receiver()->track().get());
		VideoTrack->AddOrUpdateSink(VideoSink, rtc::VideoSinkWants());
	}
	return;
}

void FPixelStreamingPeerConnection::OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("OnRemoveTrack"));
}

void FPixelStreamingPeerConnection::OnStateChange()
{
	UE_LOG(LogPixelStreaming, Log, TEXT("OnStateChange"));
}

void FPixelStreamingPeerConnection::OnMessage(const webrtc::DataBuffer& buffer)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("OnMessage"));

	const uint8 MsgType = static_cast<uint8>(buffer.data.data()[0]);
	const size_t DescriptorSize = (buffer.data.size() - 1) / sizeof(TCHAR);
	const TCHAR* DescPtr = reinterpret_cast<const TCHAR*>(buffer.data.data() + 1);
	const FString Descriptor(DescriptorSize, DescPtr);
	// OnDataMessage.Broadcast(*this, MsgType, Descriptor);
}

void FPixelStreamingPeerConnection::CreatePeerConnectionFactory()
{
	using namespace UE::PixelStreaming;

	SignallingThread = MakeUnique<rtc::Thread>(rtc::SocketServer::CreateDefault());
	SignallingThread->SetName("FPixelStreamingPeerConnection SignallingThread", nullptr);
	SignallingThread->Start();

	bool bUseLegacyAudioDeviceModule = Settings::CVarPixelStreamingWebRTCUseLegacyAudioDevice.GetValueOnAnyThread();
	rtc::scoped_refptr<webrtc::AudioDeviceModule> AudioDeviceModule;
	if (bUseLegacyAudioDeviceModule)
	{
		AudioDeviceModule = new rtc::RefCountedObject<FAudioCapturer>();
	}
	else
	{
		AudioDeviceModule = new rtc::RefCountedObject<FPixelStreamingAudioDeviceModule>();
	}

	PeerConnectionFactory = webrtc::CreatePeerConnectionFactory(
		nullptr,													   // network_thread
		nullptr,													   // worker_thread
		SignallingThread.Get(),										   // signaling_thread
		AudioDeviceModule,											   // default_adm
		webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>(), // audio_encoder_factory
		webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>(), // audio_decoder_factory
		std::make_unique<FVideoEncoderFactorySimulcast>(),			   // video_encoder_factory
		std::make_unique<FVideoDecoderFactory>(),					   // video_decoder_factory
		nullptr,													   // audio_mixer
		nullptr);													   // audio_processing
	check(PeerConnectionFactory);
}
