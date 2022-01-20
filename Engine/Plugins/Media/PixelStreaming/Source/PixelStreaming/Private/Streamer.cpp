// Copyright Epic Games, Inc. All Rights Reserved.

#include "Streamer.h"
#include "ToStringExtensions.h"
#include "AudioCapturer.h"
#include "PlayerSession.h"
#include "VideoEncoderFactory.h"
#include "EncoderFactory.h"
#include "Settings.h"
#include "WebRTCLogging.h"
#include "WebSocketsModule.h"
#include "AudioDeviceModule.h"
#include "AudioSink.h"
#include "SessionDescriptionObservers.h"
#include "VideoSource.h"
#include "RTCStatsCollector.h"
#include "PixelStreamingDelegates.h"

bool UE::PixelStreaming::FStreamer::IsPlatformCompatible()
{
	return AVEncoder::FVideoEncoderFactory::Get().HasEncoderForCodec(AVEncoder::ECodecType::H264);
}

UE::PixelStreaming::FStreamer::FStreamer(const FString& InSignallingServerUrl, const FString& InStreamerId)
	: SignallingServerUrl(InSignallingServerUrl)
	, StreamerId(InStreamerId)
	, WebRtcSignallingThread(MakeUnique<rtc::Thread>(rtc::SocketServer::CreateDefault()))
	, SignallingServerConnection(MakeUnique<UE::PixelStreaming::FSignallingServerConnection>(*this, InStreamerId))
	, PlayerSessions(WebRtcSignallingThread.Get())
	, Stats(&PlayerSessions)
{
	RedirectWebRtcLogsToUnreal(rtc::LoggingSeverity::LS_VERBOSE);

	FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("AVEncoder"));

	// required for communication with Signalling Server and must be called in the game thread, while it's used in signalling thread
	FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");

	StartWebRtcSignallingThread();
	ConnectToSignallingServer();


	if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
	{
		Delegates->OnClosedConnectionNative.AddRaw(this, &UE::PixelStreaming::FStreamer::PostPlayerDeleted);
		Delegates->OnQualityControllerChangedNative.AddRaw(this, &UE::PixelStreaming::FStreamer::OnQualityControllerChanged);
	}
}

UE::PixelStreaming::FStreamer::~FStreamer()
{
	DeleteAllPlayerSessions();
	P2PPeerConnectionFactory = nullptr;
	WebRtcSignallingThread->Stop();
	rtc::CleanupSSL();
}

void UE::PixelStreaming::FStreamer::StartWebRtcSignallingThread()
{
	// Initialisation of WebRTC. Note: That interfacing with WebRTC should generally happen in WebRTC signalling thread.

	// Create our own WebRTC thread for signalling
	WebRtcSignallingThread->SetName("WebRtcSignallingThread", nullptr);
	WebRtcSignallingThread->Start();

	rtc::InitializeSSL();

	PeerConnectionConfig = {};

	bool bUseLegacyAudioDeviceModule = UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCUseLegacyAudioDevice.GetValueOnAnyThread();
	rtc::scoped_refptr<webrtc::AudioDeviceModule> AudioDeviceModule;
	if (bUseLegacyAudioDeviceModule)
	{
		AudioDeviceModule = new rtc::RefCountedObject<UE::PixelStreaming::FAudioCapturer>();
	}
	else
	{
		AudioDeviceModule = new rtc::RefCountedObject<UE::PixelStreaming::FAudioDeviceModule>();
	}

	/* ---------- P2P Peer Connection Factory ---------- */

	std::unique_ptr<UE::PixelStreaming::FVideoEncoderFactory> P2PPeerConnectionFactoryPtr = std::make_unique<UE::PixelStreaming::FVideoEncoderFactory>();
	P2PVideoEncoderFactory = P2PPeerConnectionFactoryPtr.get(); 

	P2PPeerConnectionFactory = webrtc::CreatePeerConnectionFactory(
		nullptr,					  // network_thread
		nullptr,					  // worker_thread
		WebRtcSignallingThread.Get(), // signal_thread
		AudioDeviceModule,			  // audio device manager
		webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>(),
		webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>(),
		std::move(P2PPeerConnectionFactoryPtr),
		std::make_unique<webrtc::InternalDecoderFactory>(),
		nullptr,					   // audio_mixer
		SetupAudioProcessingModule()); // audio_processing

	check(P2PPeerConnectionFactory.get() != nullptr);

	/* ---------- SFU Peer Connection Factory ---------- */

	std::unique_ptr<UE::PixelStreaming::FSimulcastEncoderFactory> SFUPeerConnectionFactoryPtr = std::make_unique<UE::PixelStreaming::FSimulcastEncoderFactory>();
	SFUVideoEncoderFactory = SFUPeerConnectionFactoryPtr.get(); 

	SFUPeerConnectionFactory = webrtc::CreatePeerConnectionFactory(
		nullptr,					  // network_thread
		nullptr,					  // worker_thread
		WebRtcSignallingThread.Get(), // signal_thread
		AudioDeviceModule,			  // audio device manager
		webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>(),
		webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>(),
		std::move(SFUPeerConnectionFactoryPtr),
		std::make_unique<webrtc::InternalDecoderFactory>(),
		nullptr,					   // audio_mixer
		SetupAudioProcessingModule()); // audio_processing

	check(SFUPeerConnectionFactory.get() != nullptr);

}

webrtc::AudioProcessing* UE::PixelStreaming::FStreamer::SetupAudioProcessingModule()
{
	webrtc::AudioProcessing* AudioProcessingModule = webrtc::AudioProcessingBuilder().Create();
	webrtc::AudioProcessing::Config Config;

	// Enabled multi channel audio capture/render
	Config.pipeline.multi_channel_capture = true;
	Config.pipeline.multi_channel_render = true;
	Config.pipeline.maximum_internal_processing_rate = 48000;

	// Turn off all other audio processing effects in UE's WebRTC. We want to stream audio from UE as pure as possible.
	Config.pre_amplifier.enabled = false;
	Config.high_pass_filter.enabled = false;
	Config.echo_canceller.enabled = false;
	Config.noise_suppression.enabled = false;
	Config.transient_suppression.enabled = false;
	Config.voice_detection.enabled = false;
	Config.gain_controller1.enabled = false;
	Config.gain_controller2.enabled = false;
	Config.residual_echo_detector.enabled = false;
	Config.level_estimation.enabled = false;

	// Apply the config.
	AudioProcessingModule->ApplyConfig(Config);

	return AudioProcessingModule;
}

void UE::PixelStreaming::FStreamer::ConnectToSignallingServer()
{
	SignallingServerConnection->Connect(SignallingServerUrl);
}

void UE::PixelStreaming::FStreamer::OnFrameBufferReady(const FTexture2DRHIRef& FrameBuffer)
{
}

void UE::PixelStreaming::FStreamer::OnQualityControllerChanged(FPixelStreamingPlayerId PlayerId)
{
}

void UE::PixelStreaming::FStreamer::PostPlayerDeleted(FPixelStreamingPlayerId PlayerId, bool WasQualityController)
{
}

void UE::PixelStreaming::FStreamer::OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config)
{
	PeerConnectionConfig = Config;
}

void UE::PixelStreaming::FStreamer::OnDataChannelOpen(FPixelStreamingPlayerId PlayerId, webrtc::DataChannelInterface* DataChannel)
{
	if (DataChannel)
	{
		// When data channel is open, try to send cached freeze frame (if we have one)
		SendCachedFreezeFrameTo(PlayerId);
	}
}

webrtc::PeerConnectionInterface* UE::PixelStreaming::FStreamer::CreateSession(FPixelStreamingPlayerId PlayerId, int Flags)
{
	PeerConnectionConfig.enable_simulcast_stats = true;

	bool bIsSFU = (Flags & UE::PixelStreaming::Protocol::EPlayerFlags::PSPFlag_IsSFU) != UE::PixelStreaming::Protocol::EPlayerFlags::PSPFlag_None;
	
	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PCFactory = bIsSFU ?
		SFUPeerConnectionFactory :
		P2PPeerConnectionFactory;

	webrtc::PeerConnectionInterface* PeerConnection = PlayerSessions.CreatePlayerSession(
		PlayerId,
		PCFactory,
		PeerConnectionConfig,
		SignallingServerConnection.Get(),
		Flags);

	if (PeerConnection != nullptr)
	{
		if ((Flags & UE::PixelStreaming::Protocol::EPlayerFlags::PSPFlag_SupportsDataChannel) != UE::PixelStreaming::Protocol::EPlayerFlags::PSPFlag_None)
		{
			webrtc::DataChannelInit DataChannelConfig;
			DataChannelConfig.reliable = true;
			DataChannelConfig.ordered = true;

			rtc::scoped_refptr<webrtc::DataChannelInterface> DataChannel = PeerConnection->CreateDataChannel("datachannel", &DataChannelConfig);
			PlayerSessions.SetPlayerSessionDataChannel(PlayerId, DataChannel);

			// Bind to the on data channel open delegate (we use this as the earliest time peer is ready for freeze frame)
			UE::PixelStreaming::FDataChannelObserver* DataChannelObserver = PlayerSessions.GetDataChannelObserver(PlayerId);
			check(DataChannelObserver);
			if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
			{
				Delegates->OnDataChannelOpenNative.AddRaw(this, &UE::PixelStreaming::FStreamer::OnDataChannelOpen);
			}
			DataChannelObserver->Register(DataChannel);
		}
	}

	return PeerConnection;
}

void UE::PixelStreaming::FStreamer::OnSessionDescription(FPixelStreamingPlayerId PlayerId, webrtc::SdpType Type, const FString& Sdp)
{
	switch (Type)
	{
		case webrtc::SdpType::kOffer:
			OnOffer(PlayerId, Sdp);
			break;
		case webrtc::SdpType::kAnswer:
		case webrtc::SdpType::kPrAnswer:
			PlayerSessions.OnAnswer(PlayerId, Sdp);
			SetStreamingStarted(true);
			break;
		case webrtc::SdpType::kRollback:
			UE_LOG(LogPixelStreaming, Error, TEXT("Rollback SDP is currently unsupported. SDP is: %s"), *Sdp);
			break;
	}
}

void UE::PixelStreaming::FStreamer::OnOffer(FPixelStreamingPlayerId PlayerId, const FString& Sdp)
{
	webrtc::PeerConnectionInterface* PeerConnection = CreateSession(PlayerId, UE::PixelStreaming::Protocol::EPlayerFlags::PSPFlag_SupportsDataChannel);
	if (PeerConnection)
	{
		webrtc::SdpParseError Error;
		std::unique_ptr<webrtc::SessionDescriptionInterface> SessionDesc = webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, to_string(Sdp), &Error);
		if (!SessionDesc)
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to parse offer's SDP: %s\n%s"), Error.description.c_str(), *Sdp);
			return;
		}

		SendAnswer(PlayerId, PeerConnection, TUniquePtr<webrtc::SessionDescriptionInterface>(SessionDesc.release()));
		ForceKeyFrame();
	}
	else
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("Failed to create player session, peer connection was nullptr."));
	}
}

void UE::PixelStreaming::FStreamer::SetLocalDescription(webrtc::PeerConnectionInterface* PeerConnection, UE::PixelStreaming::FSetSessionDescriptionObserver* Observer, webrtc::SessionDescriptionInterface* SDP)
{
	// Note from Luke about WebRTC: the sink of video capturer will be added as a direct result
	// of `PeerConnection->SetLocalDescription()` call but video encoder will be created later on
	// when the first frame is pushed into the WebRTC pipeline (by the capturer calling `OnFrame`).

	PeerConnection->SetLocalDescription(Observer, SDP);

	// Once local description has been set we can start setting some encoding information for the video stream rtp sender
	for (rtc::scoped_refptr<webrtc::RtpSenderInterface> Sender : PeerConnection->GetSenders())
	{
		cricket::MediaType MediaType = Sender->media_type();
		if (MediaType == cricket::MediaType::MEDIA_TYPE_VIDEO)
		{
			webrtc::RtpParameters ExistingParams = Sender->GetParameters();

			// Set the degradation preference based on our CVar for it.
			ExistingParams.degradation_preference = UE::PixelStreaming::Settings::GetDegradationPreference();

			webrtc::RTCError Err = Sender->SetParameters(ExistingParams);
			if (!Err.ok())
			{
				const char* ErrMsg = Err.message();
				FString ErrorStr(ErrMsg);
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to set RTP Sender params: %s"), *ErrorStr);
			}
		}
	}
}

void UE::PixelStreaming::FStreamer::SendAnswer(FPixelStreamingPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection, TUniquePtr<webrtc::SessionDescriptionInterface> Sdp)
{
	// below is async execution (with error handling) of:
	//		PeerConnection.SetRemoteDescription(SDP);
	//		Answer = PeerConnection.CreateAnswer();
	//		PeerConnection.SetLocalDescription(Answer);
	//		SignallingServerConnection.SendAnswer(Answer);

	UE::PixelStreaming::FSetSessionDescriptionObserver* SetLocalDescriptionObserver = UE::PixelStreaming::FSetSessionDescriptionObserver::Create(
		[this, PlayerId, PeerConnection]() // on success
		{
			GetSignallingServerConnection()->SendAnswer(PlayerId, *PeerConnection->local_description());
			SetStreamingStarted(true);
		},
		[this, PlayerId](const FString& Error) // on failure
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to set local description: %s"), *Error);
			PlayerSessions.DisconnectPlayer(PlayerId, Error);
		});

	auto OnCreateAnswerSuccess = [this, PlayerId, PeerConnection, SetLocalDescriptionObserver](webrtc::SessionDescriptionInterface* SDP) {
		SetLocalDescription(PeerConnection, SetLocalDescriptionObserver, SDP);
	};

	UE::PixelStreaming::FCreateSessionDescriptionObserver* CreateAnswerObserver = UE::PixelStreaming::FCreateSessionDescriptionObserver::Create(
		MoveTemp(OnCreateAnswerSuccess),
		[this, PlayerId](const FString& Error) // on failure
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to create answer: %s"), *Error);
			PlayerSessions.DisconnectPlayer(PlayerId, Error);
		});

	auto OnSetRemoteDescriptionSuccess = [this, PlayerId, PeerConnection, CreateAnswerObserver]() {
		// Note: these offer to receive are superseded now we are use transceivers to setup our peer connection media
		int offer_to_receive_video = webrtc::PeerConnectionInterface::RTCOfferAnswerOptions::kUndefined; 
		int offer_to_receive_audio = webrtc::PeerConnectionInterface::RTCOfferAnswerOptions::kUndefined; 
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

		AddStreams(PlayerId, PeerConnection, UE::PixelStreaming::Protocol::EPlayerFlags::PSPFlag_SupportsDataChannel);
		ModifyTransceivers(PeerConnection->GetTransceivers(), UE::PixelStreaming::Protocol::EPlayerFlags::PSPFlag_SupportsDataChannel);

		PeerConnection->CreateAnswer(CreateAnswerObserver, AnswerOption);
	};

	UE::PixelStreaming::FSetSessionDescriptionObserver* SetRemoteDescriptionObserver = UE::PixelStreaming::FSetSessionDescriptionObserver::Create(
		MoveTemp(OnSetRemoteDescriptionSuccess),
		[this, PlayerId](const FString& Error) // on failure
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to set remote description: %s"), *Error);
			PlayerSessions.DisconnectPlayer(PlayerId, Error);
		});

	cricket::SessionDescription* RemoteDescription = Sdp->description();
	MungeRemoteSDP(RemoteDescription);

	PeerConnection->SetRemoteDescription(SetRemoteDescriptionObserver, Sdp.Release());
}

void UE::PixelStreaming::FStreamer::MungeRemoteSDP(cricket::SessionDescription* RemoteDescription)
{
	// Munge SDP of remote description to inject min, max, start bitrates
	std::vector<cricket::ContentInfo>& ContentInfos = RemoteDescription->contents();
	for(cricket::ContentInfo& Content : ContentInfos)
	{
		cricket::MediaContentDescription* MediaDescription = Content.media_description();
		if(MediaDescription->type() == cricket::MediaType::MEDIA_TYPE_VIDEO)
		{
			cricket::VideoContentDescription* VideoDescription = MediaDescription->as_video();
			std::vector<cricket::VideoCodec> CodecsCopy = VideoDescription->codecs();
			for(cricket::VideoCodec& Codec : CodecsCopy)
			{
				// Note: These params are passed as kilobits, so divide by 1000.
				Codec.SetParam(cricket::kCodecParamMinBitrate, UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCMinBitrate.GetValueOnAnyThread() / 1000);
				Codec.SetParam(cricket::kCodecParamStartBitrate, UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCStartBitrate.GetValueOnAnyThread() / 1000);
				Codec.SetParam(cricket::kCodecParamMaxBitrate, UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCMaxBitrate.GetValueOnAnyThread() / 1000);
			}
			VideoDescription->set_codecs(CodecsCopy);
		}	
	}
}

void UE::PixelStreaming::FStreamer::OnRemoteIceCandidate(FPixelStreamingPlayerId PlayerId, const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp)
{
	PlayerSessions.OnRemoteIceCandidate(PlayerId, SdpMid, SdpMLineIndex, Sdp);
}

void UE::PixelStreaming::FStreamer::OnPlayerConnected(FPixelStreamingPlayerId PlayerId, int Flags)
{
	// create peer connection
	if (webrtc::PeerConnectionInterface* PeerConnection = CreateSession(PlayerId, Flags))
	{
		AddStreams(PlayerId, PeerConnection, Flags);
		ModifyTransceivers(PeerConnection->GetTransceivers(), Flags);

		// observer for creating offer
		UE::PixelStreaming::FCreateSessionDescriptionObserver* CreateOfferObserver = UE::PixelStreaming::FCreateSessionDescriptionObserver::Create(
			[this, PlayerId, PeerConnection](webrtc::SessionDescriptionInterface* SDP) // on SDP create success
			{
				UE::PixelStreaming::FSetSessionDescriptionObserver* SetLocalDescriptionObserver = UE::PixelStreaming::FSetSessionDescriptionObserver::Create(
					[this, PlayerId, SDP]() // on SDP set success
					{
						GetSignallingServerConnection()->SendOffer(PlayerId, *SDP);
					},
					[](const FString& Error) // on SDP set failure
					{
						UE_LOG(LogPixelStreaming, Error, TEXT("Failed to set local description: %s"), *Error);
					});

				SetLocalDescription(PeerConnection, SetLocalDescriptionObserver, SDP);
			},
			[](const FString& Error) // on SDP create failure
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to create offer: %s"), *Error);
			});

		PeerConnection->CreateOffer(CreateOfferObserver, {});
	}
}

void UE::PixelStreaming::FStreamer::OnPlayerDisconnected(FPixelStreamingPlayerId PlayerId)
{
	UE_LOG(LogPixelStreaming, Log, TEXT("player %s disconnected"), *PlayerId);
	DeletePlayerSession(PlayerId);
}

void UE::PixelStreaming::FStreamer::OnSignallingServerDisconnected()
{
	DeleteAllPlayerSessions();

	// Call disconnect here makes sure any other websocket callbacks etc are cleared
	SignallingServerConnection->Disconnect();
	ConnectToSignallingServer();
}

int UE::PixelStreaming::FStreamer::GetNumPlayers() const
{
	return PlayerSessions.GetNumPlayers();
}

void UE::PixelStreaming::FStreamer::ForceKeyFrame()
{
	if (P2PVideoEncoderFactory)
	{
		P2PVideoEncoderFactory->ForceKeyFrame();
	}
	else
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("Cannot force a key frame - video encoder factory is nullptr."));
	}
}

void UE::PixelStreaming::FStreamer::SetQualityController(FPixelStreamingPlayerId PlayerId)
{

	PlayerSessions.SetQualityController(PlayerId);
}

bool UE::PixelStreaming::FStreamer::IsQualityController(FPixelStreamingPlayerId PlayerId) const
{
	return PlayerSessions.IsQualityController(PlayerId);
}

IPixelStreamingAudioSink* UE::PixelStreaming::FStreamer::GetAudioSink(FPixelStreamingPlayerId PlayerId) const
{
	return PlayerSessions.GetAudioSink(PlayerId);
}

IPixelStreamingAudioSink* UE::PixelStreaming::FStreamer::GetUnlistenedAudioSink() const
{
	return PlayerSessions.GetUnlistenedAudioSink();
}

bool UE::PixelStreaming::FStreamer::SendMessage(FPixelStreamingPlayerId PlayerId, UE::PixelStreaming::Protocol::EToPlayerMsg Type, const FString& Descriptor) const
{
	return PlayerSessions.SendMessage(PlayerId, Type, Descriptor);
}

void UE::PixelStreaming::FStreamer::SendLatestQP(FPixelStreamingPlayerId PlayerId, int LatestQP) const
{
	PlayerSessions.SendLatestQP(PlayerId, LatestQP);
}

void UE::PixelStreaming::FStreamer::PollWebRTCStats() const
{
	PlayerSessions.PollWebRTCStats();
}

void UE::PixelStreaming::FStreamer::DeleteAllPlayerSessions()
{
	PlayerSessions.DeleteAllPlayerSessions();
	bStreamingStarted = false;
}

void UE::PixelStreaming::FStreamer::DeletePlayerSession(FPixelStreamingPlayerId PlayerId)
{
	int NumRemainingPlayers = PlayerSessions.DeletePlayerSession(PlayerId);

	if (NumRemainingPlayers == 0)
	{
		bStreamingStarted = false;
	}
}

FString UE::PixelStreaming::FStreamer::GetAudioStreamID() const
{
	bool bSyncVideoAndAudio = !UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCDisableAudioSync.GetValueOnAnyThread();
	return bSyncVideoAndAudio ? TEXT("pixelstreaming_av_stream_id") : TEXT("pixelstreaming_audio_stream_id");
}

FString UE::PixelStreaming::FStreamer::GetVideoStreamID() const
{
	bool bSyncVideoAndAudio = !UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCDisableAudioSync.GetValueOnAnyThread();
	return bSyncVideoAndAudio ? TEXT("pixelstreaming_av_stream_id") : TEXT("pixelstreaming_video_stream_id");
}

void UE::PixelStreaming::FStreamer::AddStreams(FPixelStreamingPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection, int Flags)
{
	check(PeerConnection);

	// Use PeerConnection's transceiver API to add create audio/video tracks.
	SetupVideoTrack(PlayerId, PeerConnection, GetVideoStreamID(), VideoTrackLabel, Flags);
	SetupAudioTrack(PeerConnection, GetAudioStreamID(), AudioTrackLabel, Flags);
}

std::vector<webrtc::RtpEncodingParameters> UE::PixelStreaming::FStreamer::CreateRTPEncodingParams(int Flags)
{
	bool bIsSFU = (Flags & UE::PixelStreaming::Protocol::EPlayerFlags::PSPFlag_IsSFU) != UE::PixelStreaming::Protocol::EPlayerFlags::PSPFlag_None;

	std::vector<webrtc::RtpEncodingParameters> EncodingParams;

	if (bIsSFU && UE::PixelStreaming::Settings::SimulcastParameters.Layers.Num() > 0)
	{
		using FLayer = UE::PixelStreaming::Settings::FSimulcastParameters::FLayer;

		// encodings should be lowest res to highest
		TArray<FLayer*> SortedLayers;
		for (FLayer& Layer : UE::PixelStreaming::Settings::SimulcastParameters.Layers)
		{
			SortedLayers.Add(&Layer);
		}

		SortedLayers.Sort([](const FLayer& LayerA, const FLayer& LayerB) { return LayerA.Scaling > LayerB.Scaling; });

		const int LayerCount = SortedLayers.Num();
		for (int i = 0; i < LayerCount; ++i)
		{
			const FLayer* SimulcastLayer = SortedLayers[i];
			webrtc::RtpEncodingParameters LayerEncoding{};
			LayerEncoding.rid = TCHAR_TO_UTF8(*(FString("simulcast") + FString::FromInt(LayerCount - i)));
			LayerEncoding.min_bitrate_bps = SimulcastLayer->MinBitrate;
			LayerEncoding.max_bitrate_bps = SimulcastLayer->MaxBitrate;
			LayerEncoding.scale_resolution_down_by = SimulcastLayer->Scaling;
			LayerEncoding.max_framerate = FMath::Max(60, UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCFps.GetValueOnAnyThread());
			EncodingParams.push_back(LayerEncoding);
		}
	}
	else
	{
		webrtc::RtpEncodingParameters encoding{};
		encoding.rid = "base";
		encoding.max_bitrate_bps = UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCMaxBitrate.GetValueOnAnyThread();
		encoding.min_bitrate_bps = UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCMinBitrate.GetValueOnAnyThread();
		encoding.max_framerate = FMath::Max(60, UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCFps.GetValueOnAnyThread());
		encoding.scale_resolution_down_by.reset();
		EncodingParams.push_back(encoding);
	}

	return EncodingParams;
}

void UE::PixelStreaming::FStreamer::SetupVideoTrack(FPixelStreamingPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection, const FString InVideoStreamId, const FString InVideoTrackLabel, int Flags)
{

	bool bIsSFU = (Flags & UE::PixelStreaming::Protocol::EPlayerFlags::PSPFlag_IsSFU) != UE::PixelStreaming::Protocol::EPlayerFlags::PSPFlag_None;
	
	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PCFactory = bIsSFU ?
		SFUPeerConnectionFactory :
		P2PPeerConnectionFactory;

	// Create a video source for this player
	rtc::scoped_refptr<FVideoSourceBase> VideoSource;
	if(bIsSFU)
	{
		VideoSource = new FVideoSourceSFU();
	}
	else
	{
		VideoSource = new FVideoSourceP2P(PlayerId, this);
	}

	PlayerSessions.SetVideoSource(PlayerId, VideoSource);
	VideoSource->Initialize();

	// Create video track
	rtc::scoped_refptr<webrtc::VideoTrackInterface> VideoTrack = PCFactory->CreateVideoTrack(TCHAR_TO_UTF8(*InVideoTrackLabel), VideoSource.get());
	VideoTrack->set_enabled(true);

	// Set some content hints based on degradation prefs, WebRTC uses these internally.
	webrtc::DegradationPreference DegradationPref = UE::PixelStreaming::Settings::GetDegradationPreference();
	switch (DegradationPref)
	{
		case webrtc::DegradationPreference::MAINTAIN_FRAMERATE:
			VideoTrack->set_content_hint(webrtc::VideoTrackInterface::ContentHint::kFluid);
			break;
		case webrtc::DegradationPreference::MAINTAIN_RESOLUTION:
			VideoTrack->set_content_hint(webrtc::VideoTrackInterface::ContentHint::kDetailed);
			break;
		default:
			break;
	}

	bool bHasVideoTransceiver = false;

	for (auto& Transceiver : PeerConnection->GetTransceivers())
	{
		rtc::scoped_refptr<webrtc::RtpSenderInterface> Sender = Transceiver->sender();
		if (Transceiver->media_type() == cricket::MediaType::MEDIA_TYPE_VIDEO)
		{
			bHasVideoTransceiver = true;
			Sender->SetTrack(VideoTrack);
		}
	}

	// If there is no existing video transceiver, add one.
	if(!bHasVideoTransceiver)
	{
		webrtc::RtpTransceiverInit TransceiverOptions;
		TransceiverOptions.stream_ids = { TCHAR_TO_UTF8(*InVideoStreamId) };
		TransceiverOptions.direction = webrtc::RtpTransceiverDirection::kSendOnly;
		TransceiverOptions.send_encodings = CreateRTPEncodingParams(Flags);

		webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> Result = PeerConnection->AddTransceiver(VideoTrack, TransceiverOptions);
		checkf(Result.ok(), TEXT("Failed to add Video transceiver to PeerConnection. Msg=%s"), *FString(Result.error().message()));
	}
}

void UE::PixelStreaming::FStreamer::SetupAudioTrack(webrtc::PeerConnectionInterface* PeerConnection, FString const InAudioStreamId, FString const InAudioTrackLabel, int Flags)
{

	bool bIsSFU = (Flags & UE::PixelStreaming::Protocol::EPlayerFlags::PSPFlag_IsSFU) != UE::PixelStreaming::Protocol::EPlayerFlags::PSPFlag_None;
	
	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PCFactory = bIsSFU ?
		SFUPeerConnectionFactory :
		P2PPeerConnectionFactory;

	bool bTransmitUEAudio = !UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCDisableTransmitAudio.GetValueOnAnyThread();

	// Create one and only one audio source for Pixel Streaming.
	if (!AudioSource && bTransmitUEAudio)
	{
		// Setup audio source options, we turn off many of the "nice" audio settings that
		// would traditionally be used in a conference call because the audio source we are
		// transmitting is UE application audio (not some unknown microphone).
		AudioSourceOptions.echo_cancellation = false;
		AudioSourceOptions.auto_gain_control = false;
		AudioSourceOptions.noise_suppression = false;
		AudioSourceOptions.highpass_filter = false;
		AudioSourceOptions.stereo_swapping = false;
		AudioSourceOptions.audio_jitter_buffer_max_packets = 1000;
		AudioSourceOptions.audio_jitter_buffer_fast_accelerate = false;
		AudioSourceOptions.audio_jitter_buffer_min_delay_ms = 0;
		AudioSourceOptions.audio_jitter_buffer_enable_rtx_handling = false;
		AudioSourceOptions.typing_detection = false;
		AudioSourceOptions.experimental_agc = false;
		AudioSourceOptions.experimental_ns = false;
		AudioSourceOptions.residual_echo_detector = false;
		// Create audio source
		AudioSource = PCFactory->CreateAudioSource(AudioSourceOptions);
	}

	// Add the audio track to the audio transceiver's sender if we are transmitting audio
	if(!bTransmitUEAudio)
	{
		return;
	}

	rtc::scoped_refptr<webrtc::AudioTrackInterface> AudioTrack = PCFactory->CreateAudioTrack(TCHAR_TO_UTF8(*InAudioTrackLabel), AudioSource);

	bool bHasAudioTransceiver = false;
	for (auto& Transceiver : PeerConnection->GetTransceivers())
	{
		rtc::scoped_refptr<webrtc::RtpSenderInterface> Sender = Transceiver->sender();
		if (Transceiver->media_type() == cricket::MediaType::MEDIA_TYPE_AUDIO)
		{
			bHasAudioTransceiver = true;
			Sender->SetTrack(AudioTrack);
		}
	}

	if(!bHasAudioTransceiver)
	{
		// Add the track
		webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>> Result = PeerConnection->AddTrack(AudioTrack, { TCHAR_TO_UTF8(*InAudioStreamId) });

		if (!Result.ok())
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to add audio track to PeerConnection. Msg=%s"), TCHAR_TO_UTF8(Result.error().message()));
		}
	}

}

void UE::PixelStreaming::FStreamer::ModifyTransceivers(std::vector<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> Transceivers, int Flags)
{
	bool bTransmitUEAudio = !UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCDisableTransmitAudio.GetValueOnAnyThread();
	bool bIsSFU = (Flags & UE::PixelStreaming::Protocol::EPlayerFlags::PSPFlag_IsSFU) != UE::PixelStreaming::Protocol::EPlayerFlags::PSPFlag_None;
	bool bReceiveBrowserAudio = !bIsSFU && !UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCDisableReceiveAudio.GetValueOnAnyThread();

	for (auto& Transceiver : Transceivers)
	{
		rtc::scoped_refptr<webrtc::RtpSenderInterface> Sender = Transceiver->sender();

		if (Transceiver->media_type() == cricket::MediaType::MEDIA_TYPE_AUDIO)
		{

			// Determine the direction of the transceiver
			webrtc::RtpTransceiverDirection AudioTransceiverDirection;
			if (bTransmitUEAudio && bReceiveBrowserAudio)
			{
				AudioTransceiverDirection = webrtc::RtpTransceiverDirection::kSendRecv;
			}
			else if (bTransmitUEAudio)
			{
				AudioTransceiverDirection = webrtc::RtpTransceiverDirection::kSendOnly;
			}
			else if (bReceiveBrowserAudio)
			{
				AudioTransceiverDirection = webrtc::RtpTransceiverDirection::kRecvOnly;
			}
			else
			{
				AudioTransceiverDirection = webrtc::RtpTransceiverDirection::kInactive;
			}

			Transceiver->SetDirection(AudioTransceiverDirection);
			Sender->SetStreams( { TCHAR_TO_UTF8(*GetAudioStreamID()) } );
		}
		else if (Transceiver->media_type() == cricket::MediaType::MEDIA_TYPE_VIDEO)
		{
			Transceiver->SetDirection(webrtc::RtpTransceiverDirection::kSendOnly);
			Sender->SetStreams( { TCHAR_TO_UTF8(*GetVideoStreamID()) } );

			webrtc::RtpParameters RtpParams = Sender->GetParameters();
			RtpParams.encodings = CreateRTPEncodingParams(Flags);
			Sender->SetParameters(RtpParams);

		}
	}
}

void UE::PixelStreaming::FStreamer::SendPlayerMessage(UE::PixelStreaming::Protocol::EToPlayerMsg Type, const FString& Descriptor)
{
	PlayerSessions.SendMessageAll(Type, Descriptor);
}

void UE::PixelStreaming::FStreamer::SendFreezeFrame(const TArray64<uint8>& JpegBytes)
{
	if (!bStreamingStarted)
	{
		return;
	}

	PlayerSessions.SendFreezeFrame(JpegBytes);

	CachedJpegBytes = JpegBytes;
}

void UE::PixelStreaming::FStreamer::SendCachedFreezeFrameTo(FPixelStreamingPlayerId PlayerId) const
{
	if (CachedJpegBytes.Num() > 0)
	{
		PlayerSessions.SendFreezeFrameTo(PlayerId, CachedJpegBytes);
	}
}

void UE::PixelStreaming::FStreamer::SendFreezeFrameTo(FPixelStreamingPlayerId PlayerId, const TArray64<uint8>& JpegBytes) const
{
	PlayerSessions.SendFreezeFrameTo(PlayerId, JpegBytes);
}

void UE::PixelStreaming::FStreamer::SendUnfreezeFrame()
{
	// Force a keyframe so when stream unfreezes if player has never received a h.264 frame before they can still connect.
	ForceKeyFrame();

	PlayerSessions.SendUnfreezeFrame();

	CachedJpegBytes.Empty();
}

void UE::PixelStreaming::FStreamer::SendFileData(TArray<uint8>& ByteData, FString& MimeType, FString& FileExtension)
{
	PlayerSessions.SendFileData(ByteData, MimeType, FileExtension);
}
