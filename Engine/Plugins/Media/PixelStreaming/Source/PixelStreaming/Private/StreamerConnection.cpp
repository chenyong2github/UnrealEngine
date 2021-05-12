// Copyright Epic Games, Inc. All Rights Reserved.

#include "StreamerConnection.h"
#include "WebRTCLogging.h"
#include "WebRtcObservers.h"
#include "AudioSink.h"
#include "VideoEncoder.h"

// HACK decoder doesnt currently work under linux
#if PLATFORM_WINDOWS
	#include "VideoDecoder.h"
	#include "api/video_codecs/builtin_video_encoder_factory.h"
	#include "api/video_codecs/builtin_video_decoder_factory.h"
#endif

#include "VideoSink.h"
#include "WebRTCLogging.h"

#include "Modules/ModuleManager.h"
#include "WebSocketsModule.h"
#include "HAL/Thread.h"

FStreamerConnection::FStreamerConnection(
	const FString& InSignallingServerAddress, 
	TUniqueFunction<void()>&& InOnDisconnection, 
	TUniqueFunction<void(const FAudioSampleRef& Sample)>&& OnAudioSample,
	TUniqueFunction<void(const FTextureSampleRef& Sample)>&& OnVideoFrame
)
	: SignallingServerAddress(InSignallingServerAddress)
	, OnDisconnection(MoveTemp(InOnDisconnection))
{
	RedirectWebRtcLogsToUnreal(rtc::LoggingSeverity::LS_VERBOSE);

	// required for communication with Signalling Server and must be called in the game thread, while it's used in signalling thread
	FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");

	AudioSink = MakeUnique<FAudioSink>(MoveTemp(OnAudioSample));
	VideoSink = MakeUnique<FVideoSink>(MoveTemp(OnVideoFrame));

	SignallingThread = MakeUnique<FThread>(TEXT("PixelStreamingPlayer Signalling Thread"), [this]() { SignallingThreadFunc(); });
}

FStreamerConnection::~FStreamerConnection()
{
	DataChannel = nullptr;
	PeerConnection = nullptr;

#if PLATFORM_WINDOWS
	// stop Signalling Thread
	check(SignallingThreadId != 0);
	PostThreadMessage(SignallingThreadId, WM_QUIT, 0, 0);
#elif PLATFORM_LINUX
	check(SignallingThreadId != 0);
#endif

	check(SignallingThread.IsValid());
	SignallingThread->Join();

	SignallingServerAddress.Empty();
}

void FStreamerConnection::SignallingThreadFunc()
{
	SignallingThreadId = FPlatformTLS::GetCurrentThreadId();

#if PLATFORM_WINDOWS

	// init WebRTC networking and inter-thread communication
	rtc::WinsockInitializer WSInitialiser;
	if (WSInitialiser.error())
	{
		UE_LOG(PixelPlayer, Error, TEXT("Failed to initialise Winsock"));
		return;
	}
	rtc::Win32SocketServer SocketServer;
	rtc::Win32Thread W32Thread(&SocketServer);
	rtc::ThreadManager::Instance()->SetCurrentThread(&W32Thread);

#elif PLATFORM_LINUX
	rtc::PhysicalSocketServer ss;
	rtc::Thread thread(&ss);
	rtc::ThreadManager::Instance()->SetCurrentThread(&thread);
#endif 

	rtc::InitializeSSL();

	// WebRTC assumes threads within which PeerConnectionFactory is created is the signalling thread
	// WebRTC requires to provide valid `Audio[Encoder/Decoder]Factory`'s even if we don't need audio and even if these factories 
	// don't claim support for any audio codecs.
	// It also requires support for at least one video encoder (we use `webrtc::InternalEncoderFactory`) even if we don't want to 
	// send video, because of `cricket::WebRtcVideoChannel` constructor that takes `flexfec_payload_type` from it
	
#if PLATFORM_WINDOWS
	// TODO replace built in Encoders with the new encoders
	PeerConnectionFactory = webrtc::CreatePeerConnectionFactory(
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>(),
		webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>(),
		webrtc::CreateBuiltinVideoEncoderFactory(),
		webrtc::CreateBuiltinVideoDecoderFactory(),
		nullptr,
		nullptr);
	check(PeerConnectionFactory);
#endif

	// now that everything is ready connect to SignallingServer
	SignallingServerConnection = MakeUnique<FSignallingServerConnection>(SignallingServerAddress, *this);

#if PLATFORM_WINDOWS
	// WebRTC window messaging loop
	MSG Msg;
	BOOL Gm;
	while ((Gm = ::GetMessageW(&Msg, NULL, 0, 0)) != 0 && Gm != -1)
	{
		::TranslateMessage(&Msg);
		::DispatchMessage(&Msg);
	}
#endif

	// cleanup
	SignallingServerConnection = nullptr;

	// WebRTC stuff created in this thread should be deleted here
	PeerConnectionFactory = nullptr;

	rtc::CleanupSSL();

	UE_LOG(PixelPlayer, Log, TEXT("Exiting WebRTC WndProc thread"));
}

void FStreamerConnection::OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config)
{
	verify(PeerConnection = PeerConnectionFactory->CreatePeerConnection(Config, webrtc::PeerConnectionDependencies{ this }));

	{
		// create Transceiver to receive video
		auto TransceiverOrError = PeerConnection->AddTransceiver(cricket::MediaType::MEDIA_TYPE_VIDEO);
		checkf(TransceiverOrError.ok(), TEXT("Failed to create WebRTC Video Transceiver: %s"), ANSI_TO_TCHAR(TransceiverOrError.error().message()));
		auto Transceiver = TransceiverOrError.MoveValue();
		Transceiver->SetDirection(webrtc::RtpTransceiverDirection::kRecvOnly);
	}
	{
		// create Transceiver to receive audio
		auto TransceiverOrError = PeerConnection->AddTransceiver(cricket::MediaType::MEDIA_TYPE_AUDIO);
		checkf(TransceiverOrError.ok(), TEXT("Failed to create WebRTC Audio Transceiver: %s"), ANSI_TO_TCHAR(TransceiverOrError.error().message()));
		auto Transceiver = TransceiverOrError.MoveValue();
		Transceiver->SetDirection(webrtc::RtpTransceiverDirection::kRecvOnly);
	}

	verify(DataChannel = PeerConnection->CreateDataChannel("default", nullptr));

	// below is async execution (with error handling) of:
	//		SDP = PeerConnection.CreateOffer();
	//		PeerConnection.SetLocalDescription(SDP);
	//		SignallingServerConnection.SendOffer(SDP);

	auto OnCreateOfferSuccess = [this](webrtc::SessionDescriptionInterface* SDP)
	{
		FSetSessionDescriptionObserver* SetLocalDescriptionObserver = FSetSessionDescriptionObserver::Create
		(
			[this, SDP]()
			{
				for (cricket::ContentInfo& ContentInfo : SDP->description()->contents())
				{
					if (ContentInfo.media_description()->type() == cricket::MEDIA_TYPE_VIDEO)
					{
						ContentInfo.media_description()->set_bandwidth(20000000); // 20Mbps
					}
				}
				SignallingServerConnection->SendOffer(*SDP);
			},
			[this](const FString& Error) // on failure
			{
				UE_LOG(PixelPlayer, Error, TEXT("Failed to SetLocalDescription: %s"), *Error);
				OnDisconnection();
			}
		);

		PeerConnection->SetLocalDescription(SetLocalDescriptionObserver, SDP);
	};

	FCreateSessionDescriptionObserver* CreateOfferObserver = FCreateSessionDescriptionObserver::Create
	(
		MoveTemp(OnCreateOfferSuccess),
		[this](const FString& Error) // on failure
		{
			UE_LOG(PixelPlayer, Error, TEXT("Failed to CreateOffer: %s"), *Error);
			OnDisconnection();
		}
	);

	PeerConnection->CreateOffer(CreateOfferObserver, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions{});

	// After this we expect to receive answer
}

void FStreamerConnection::OnAnswer(TUniquePtr<webrtc::SessionDescriptionInterface> Sdp)
{
	FSetSessionDescriptionObserver* SetRemoteDescriptionObserver = FSetSessionDescriptionObserver::Create
	(
		[this]() // on success
		{
			UE_LOG(PixelPlayer, Log, TEXT("SetRemoteDescription done"));
		},
		[this](const FString& Error) // on failure
		{
			UE_LOG(PixelPlayer, Error, TEXT("Failed to SetRemoteDescription: %s"), *Error)
		}
	);

	PeerConnection->SetRemoteDescription(SetRemoteDescriptionObserver, Sdp.Release());
}

void FStreamerConnection::OnRemoteIceCandidate(TUniquePtr<webrtc::IceCandidateInterface> Candidate)
{
	if (PeerConnection->AddIceCandidate(Candidate.Get()))
	{
		UE_LOG(PixelPlayer, Log, TEXT("AddIceCandidate done"));
	}
	else
	{
		UE_LOG(PixelPlayer, Error, TEXT("Failed to AddIceCandidate"));
	}
}

void FStreamerConnection::OnSignallingServerDisconnected()
{
	OnDisconnection();
}

//
// PeerConnectionObserver impl
//

void FStreamerConnection::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState NewState)
{
	UE_LOG(PixelPlayer, Log, TEXT("%s : NewState=%s"), TEXT("FStreamerConnection::OnSignalingChange"), ToString(NewState));
}

// Called when a remote stream is added
void FStreamerConnection::OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream)
{
	UE_LOG(PixelPlayer, Log, TEXT("%s : Stream=%s"), TEXT("FStreamerConnection::OnAddStream"), *ToString(Stream->id()));
}

void FStreamerConnection::OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream)
{
	checkf(false, TEXT("Unexpected %s : Stream=%s"), TEXT("FStreamerConnection::OnRemoveStream"), *ToString(Stream->id()));
}

void FStreamerConnection::OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> InDataChannel)
{
	checkf(false, TEXT("Unexpected %s"), TEXT("FStreamerConnection::OnDataChannel"));
}

void FStreamerConnection::OnRenegotiationNeeded()
{
	// happens even before initial negotiation so is expected
	UE_LOG(PixelPlayer, Log, TEXT("%s"), TEXT("FStreamerConnection::OnRenegotiationNeeded"));
}

void FStreamerConnection::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState NewState)
{
	UE_LOG(PixelPlayer, Log, TEXT("%s : NewState=%s"), TEXT("FStreamerConnection::OnIceConnectionChange"), ToString(NewState));
}

void FStreamerConnection::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState NewState)
{
	UE_LOG(PixelPlayer, Log, TEXT("%s : NewState=%s"), TEXT("FStreamerConnection::OnIceGatheringChange"), ToString(NewState));
}

void FStreamerConnection::OnIceCandidate(const webrtc::IceCandidateInterface* Candidate)
{
	SignallingServerConnection->SendIceCandidate(*Candidate);
}

void FStreamerConnection::OnIceCandidatesRemoved(const std::vector<cricket::Candidate>& Candidates)
{
	UE_LOG(PixelPlayer, Log, TEXT("%s"), TEXT("FStreamerConnection::OnIceCandidatesRemoved"));
	// NOTE(andriy): unimplemented
}

void FStreamerConnection::OnIceConnectionReceivingChange(bool Receiving)
{
	UE_LOG(PixelPlayer, Log, TEXT("%s : Receiving=%d"), TEXT("FStreamerConnection::OnIceConnectionReceivingChange"), Receiving);
}

void FStreamerConnection::OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> Transceiver)
{
	UE_LOG(PixelPlayer, Log, TEXT("%s"), TEXT("FStreamerConnection::OnTrack"));
	checkf(Transceiver->media_type() == cricket::MEDIA_TYPE_VIDEO || Transceiver->media_type() == cricket::MEDIA_TYPE_AUDIO, TEXT("%s"), *ToString(cricket::MediaTypeToString(Transceiver->media_type())));

	if (Transceiver->media_type() == cricket::MEDIA_TYPE_VIDEO)
	{
		webrtc::VideoTrackInterface& VideoTrack = static_cast<webrtc::VideoTrackInterface&>(*Transceiver->receiver()->track());
		VideoTrack.AddOrUpdateSink(VideoSink.Get(), rtc::VideoSinkWants());
	}
	else if (Transceiver->media_type() == cricket::MEDIA_TYPE_AUDIO)
	{
		webrtc::AudioTrackInterface& AudioTrack = static_cast<webrtc::AudioTrackInterface&>(*Transceiver->receiver()->track());
		AudioTrack.AddSink(AudioSink.Get());
	}
}

void FStreamerConnection::OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> Receiver)
{
	checkf(false, TEXT("Unexpected %s: %s"), TEXT("FStreamerConnection::OnRemoveTrack"), *ToString(Receiver->track()->id()));
}

//
// webrtc::DataChannelObserver impl
//

void FStreamerConnection::OnMessage(const webrtc::DataBuffer& Buffer)
{
	UE_LOG(PixelPlayer, Log, TEXT("%s"), TEXT("FStreamerConnection::OnMessage"));
	unimplemented();
}
