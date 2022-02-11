// Copyright Epic Games, Inc. All Rights Reserved.
#include "ClientRTC.h"
#include "SessionDescriptionObservers.h"
#include "ToStringExtensions.h"
#include "VideoDecoderFactory.h"
#include "Settings.h"
#include "Utils.h"
#include "Modules/ModuleManager.h"
#include "WebSocketsModule.h"
#include "PixelStreamingPrivate.h"
#include "AudioCapturer.h"
#include "WebRTCIncludes.h"
#include "AudioDeviceModule.h"

namespace
{
	using namespace UE::PixelStreaming;

	rtc::scoped_refptr<webrtc::AudioDeviceModule> CreateAudioDeviceModule()
	{
		bool bUseLegacyAudioDeviceModule = Settings::CVarPixelStreamingWebRTCUseLegacyAudioDevice.GetValueOnAnyThread();
		rtc::scoped_refptr<webrtc::AudioDeviceModule> AudioDeviceModule;
		if (bUseLegacyAudioDeviceModule)
		{
			AudioDeviceModule = new rtc::RefCountedObject<FAudioCapturer>();
		}
		else
		{
			AudioDeviceModule = new rtc::RefCountedObject<FAudioDeviceModule>();
		}
		return AudioDeviceModule;
	}
} // namespace

namespace UE::PixelStreaming
{
	FClientRTC::FClientRTC()
		:State(EState::Disconnected)
	{
		FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");
		rtc::InitializeSSL();

		WebRtcSignallingThread = MakeUnique<rtc::Thread>(rtc::SocketServer::CreateDefault());
		WebRtcSignallingThread->SetName("FPlayerSessionClientWebRTCThread", nullptr);
		WebRtcSignallingThread->Start();

		PeerConnectionFactory = webrtc::CreatePeerConnectionFactory(
			nullptr,													   // network_thread
			nullptr,													   // worker_thread
			WebRtcSignallingThread.Get(),								   // signaling_thread
			CreateAudioDeviceModule(),									   // default_adm
			webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>(), // audio_encoder_factory
			webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>(), // audio_decoder_factory
			nullptr,													   // video_encoder_factory
			std::make_unique<FVideoDecoderFactory>(),					   // video_decoder_factory
			nullptr,													   // audio_mixer
			nullptr);													   // audio_processing
		check(PeerConnectionFactory);

		SignallingServerConnection = MakeUnique<FSignallingServerConnection>(*this, TEXT("UE_INSTANCE"));
	}

	FClientRTC::~FClientRTC()
	{
	}

	void FClientRTC::Connect(const FString& Url)
	{
		SignallingServerConnection->Connect(Url);
		State = EState::Connecting;
	}

	bool FClientRTC::SendMessage(Protocol::EToStreamerMsg Type, const FString& Descriptor) const
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

	void FClientRTC::OnSignallingServerConnected()
	{
		State = EState::ConnectedSignalling;
	}

	void FClientRTC::OnSignallingServerDisconnected()
	{
		State = EState::Disconnected;
	}

	void FClientRTC::OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config)
	{
		if (!PeerConnection)
		{
			PeerConnectionConfig = Config;
			PeerConnection = PeerConnectionFactory->CreatePeerConnection(PeerConnectionConfig, webrtc::PeerConnectionDependencies{ this });
			check(PeerConnection);
		}
	}

	void FClientRTC::OnSessionDescription(webrtc::SdpType Type, const FString& Sdp)
	{
		if (Type == webrtc::SdpType::kOffer)
		{
			SetRemoteDescription(Sdp);
		}
	}

	void FClientRTC::OnRemoteIceCandidate(const FString& SdpMid, int SdpMLineIndex, const FString& Sdp)
	{
		webrtc::SdpParseError Error;
		std::unique_ptr<webrtc::IceCandidateInterface> Candidate(webrtc::CreateIceCandidate(ToString(SdpMid), SdpMLineIndex, ToString(Sdp), &Error));
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

	void FClientRTC::OnPeerDataChannels(int32 SendStreamId, int32 RecvStreamId)
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

	void FClientRTC::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState NewState)
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("OnSignalingChange (%s)"), ToString(NewState));
	}

	void FClientRTC::OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream)
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("OnAddStream"));
	}

	void FClientRTC::OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream)
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("OnRemoveStream"));
	}

	void FClientRTC::OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> Channel)
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("OnDataChannel"));

		if (DataChannel)
		{
			UE_LOG(LogPixelStreaming, Warning, TEXT("Replacing datachannel"));
		}

		DataChannel = Channel;
		DataChannel->RegisterObserver(this);
	}

	void FClientRTC::OnRenegotiationNeeded()
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("OnRenegotiationNeeded"));
	}

	void FClientRTC::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState NewState)
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("OnIceConnectionChange (%s)"), ToString(NewState));

		if (NewState == webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionConnected)
		{
			State = EState::ConnectedStreamer;
			OnConnected.Broadcast(*this);
		}
		else if (NewState == webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionDisconnected)
		{
			State = EState::Disconnected;
			OnDisconnected.Broadcast(*this);
		}
	}

	void FClientRTC::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState NewState)
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("OnIceGatheringChange (%s)"), ToString(NewState));
	}

	void FClientRTC::OnIceCandidate(const webrtc::IceCandidateInterface* Candidate)
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("OnIceCandidate"));
		SignallingServerConnection->SendIceCandidate(*Candidate);
	}

	void FClientRTC::OnIceCandidatesRemoved(const std::vector<cricket::Candidate>& candidates)
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("OnIceCandidatesRemoved"));
	}

	void FClientRTC::OnIceConnectionReceivingChange(bool Receiving)
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("OnIceConnectionReceivingChange"));
	}

	void FClientRTC::OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("OnTrack"));
	}

	void FClientRTC::OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver)
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("OnRemoveTrack"));
	}

	void FClientRTC::OnStateChange()
	{
	}

	void FClientRTC::OnMessage(const webrtc::DataBuffer& buffer)
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("OnMessage"));

		const uint8 MsgType = static_cast<uint8>(buffer.data.data()[0]);
		const size_t DescriptorSize = (buffer.data.size() - 1) / sizeof(TCHAR);
		const TCHAR* DescPtr = reinterpret_cast<const TCHAR*>(buffer.data.data() + 1);
		const FString Descriptor(DescriptorSize, DescPtr);
		OnDataMessage.Broadcast(*this, MsgType, Descriptor);
	}

	void FClientRTC::SetRemoteDescription(const FString& Sdp)
	{
		FSetSessionDescriptionObserver* SetLocalDescriptionObserver = FSetSessionDescriptionObserver::Create(
			[this]() // on success
			{
				SignallingServerConnection->SendAnswer(*PeerConnection->local_description());
			},
			[](const FString& Error) // on failure
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to set local description: %s"), *Error);
			});

		FCreateSessionDescriptionObserver* CreateAnswerObserver = FCreateSessionDescriptionObserver::Create(
			[this, SetLocalDescriptionObserver](webrtc::SessionDescriptionInterface* SDP) {
				PeerConnection->SetLocalDescription(SetLocalDescriptionObserver, SDP);
			},
			[](const FString& Error) // on failure
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to create answer: %s"), *Error);
			});

		FSetSessionDescriptionObserver* SetRemoteDescriptionObserver = FSetSessionDescriptionObserver::Create(
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
			},
			[](const FString& Error) // on failure
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to set remote description: %s"), *Error);
			});

		webrtc::SdpParseError Error;
		std::unique_ptr<webrtc::SessionDescriptionInterface> SessionDesc = webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, ToString(Sdp), &Error);
		if (!SessionDesc)
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to create session description: %s"), Error.description.c_str());
		}

		PeerConnection->SetRemoteDescription(SetRemoteDescriptionObserver, SessionDesc.release());
	}
} // namespace UE::PixelStreaming
