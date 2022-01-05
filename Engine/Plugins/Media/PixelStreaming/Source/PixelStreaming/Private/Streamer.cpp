// Copyright Epic Games, Inc. All Rights Reserved.

#include "Streamer.h"
#include "AudioCapturer.h"
#include "PlayerSession.h"
#include "VideoEncoderFactory.h"
#include "PixelStreamerDelegates.h"
#include "PixelStreamingEncoderFactory.h"
#include "PixelStreamingSettings.h"
#include "WebRTCLogging.h"
#include "WebSocketsModule.h"
#include "PixelStreamingAudioDeviceModule.h"
#include "PixelStreamingAudioSink.h"
#include "WebRtcObservers.h"
#include "PlayerVideoSource.h"
#include "PixelStreamingRTCStatsCollector.h"

DEFINE_LOG_CATEGORY(PixelStreamer);

bool FStreamer::CheckPlatformCompatibility()
{
	return AVEncoder::FVideoEncoderFactory::Get().HasEncoderForCodec(AVEncoder::ECodecType::H264);
}

FStreamer::FStreamer(const FString& InSignallingServerUrl, const FString& InStreamerId)
	: SignallingServerUrl(InSignallingServerUrl)
	, StreamerId(InStreamerId)
	, WebRtcSignallingThread(MakeUnique<rtc::Thread>(rtc::SocketServer::CreateDefault()))
	, SignallingServerConnection(MakeUnique<FSignallingServerConnection>(*this, InStreamerId))
	, FramePump(&FrameSource)
	, PlayerSessions(WebRtcSignallingThread.Get())
	, Stats(&PlayerSessions)
{
	RedirectWebRtcLogsToUnreal(rtc::LoggingSeverity::LS_VERBOSE);

	FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("AVEncoder"));

	// required for communication with Signalling Server and must be called in the game thread, while it's used in signalling thread
	FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");

	StartWebRtcSignallingThread();
	ConnectToSignallingServer();

	this->PlayerSessions.OnQualityControllerChanged.AddRaw(this, &FStreamer::OnQualityControllerChanged);
	this->PlayerSessions.OnPlayerDeleted.AddRaw(this, &FStreamer::PostPlayerDeleted);
}

FStreamer::~FStreamer()
{
	DeleteAllPlayerSessions();
	PeerConnectionFactory = nullptr;
	WebRtcSignallingThread->Stop();
	rtc::CleanupSSL();
}

void FStreamer::StartWebRtcSignallingThread()
{
	// initialisation of WebRTC stuff and things that depends on it should happen in WebRTC signalling thread

	// Create our own WebRTC thread for signalling
	WebRtcSignallingThread->SetName("WebRtcSignallingThread", nullptr);
	WebRtcSignallingThread->Start();

	rtc::InitializeSSL();

	PeerConnectionConfig = {};

	std::unique_ptr<FPixelStreamingSimulcastEncoderFactory> videoEncoderFactory = std::make_unique<FPixelStreamingSimulcastEncoderFactory>(this);
	VideoEncoderFactory = videoEncoderFactory->GetRealFactory();

	bool bUseLegacyAudioDeviceModule = PixelStreamingSettings::CVarPixelStreamingWebRTCUseLegacyAudioDevice.GetValueOnAnyThread();
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
		nullptr,					  // network_thread
		nullptr,					  // worker_thread
		WebRtcSignallingThread.Get(), // signal_thread
		AudioDeviceModule,			  // audio device manager
		webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>(),
		webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>(),
		std::move(videoEncoderFactory),
		std::make_unique<webrtc::InternalDecoderFactory>(),
		nullptr,							 // audio_mixer
		this->SetupAudioProcessingModule()); // audio_processing
	check(PeerConnectionFactory);
}

webrtc::AudioProcessing* FStreamer::SetupAudioProcessingModule()
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

void FStreamer::ConnectToSignallingServer()
{
	this->SignallingServerConnection->Connect(SignallingServerUrl);
}

void FStreamer::OnFrameBufferReady(const FTexture2DRHIRef& FrameBuffer)
{
	if (bStreamingStarted)
	{
		FrameSource.OnFrameReady(FrameBuffer);
	}
}

void FStreamer::OnQualityControllerChanged(FPlayerId PlayerId)
{
	FramePump.SetQualityController(PlayerId);
}

void FStreamer::PostPlayerDeleted(FPlayerId PlayerId)
{
	FramePump.RemovePlayerVideoSource(PlayerId);
}

void FStreamer::OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config)
{
	this->PeerConnectionConfig = Config;
}

void FStreamer::OnDataChannelOpen(FPlayerId PlayerId, webrtc::DataChannelInterface* DataChannel)
{
	if (DataChannel)
	{
		// When data channel is open, try to send cached freeze frame (if we have one)
		this->SendCachedFreezeFrameTo(PlayerId);
	}
}

webrtc::PeerConnectionInterface* FStreamer::CreateSession(FPlayerId PlayerId, int Flags)
{
	PeerConnectionConfig.enable_simulcast_stats = true;

	webrtc::PeerConnectionInterface* PeerConnection = PlayerSessions.CreatePlayerSession(
		PlayerId,
		PeerConnectionFactory,
		PeerConnectionConfig,
		SignallingServerConnection.Get(),
		Flags);

	if (PeerConnection != nullptr)
	{
		if ((Flags & PixelStreamingProtocol::EPixelStreamingPlayerFlags::PSPFlag_SupportsDataChannel) != PixelStreamingProtocol::EPixelStreamingPlayerFlags::PSPFlag_None)
		{
			webrtc::DataChannelInit DataChannelConfig;
			DataChannelConfig.reliable = true;
			DataChannelConfig.ordered = true;

			rtc::scoped_refptr<webrtc::DataChannelInterface> DataChannel = PeerConnection->CreateDataChannel("datachannel", &DataChannelConfig);
			PlayerSessions.SetPlayerSessionDataChannel(PlayerId, DataChannel);

			// Bind to the on data channel open delegate (we use this as the earliest time peer is ready for freeze frame)
			FPixelStreamingDataChannelObserver* DataChannelObserver = PlayerSessions.GetDataChannelObserver(PlayerId);
			check(DataChannelObserver);
			DataChannelObserver->OnDataChannelOpen.AddRaw(this, &FStreamer::OnDataChannelOpen);
			DataChannelObserver->Register(DataChannel);
		}

		AddStreams(PlayerId, PeerConnection, Flags);
	}

	return PeerConnection;
}

void FStreamer::OnSessionDescription(FPlayerId PlayerId, webrtc::SdpType Type, const FString& Sdp)
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
			UE_LOG(PixelStreamer, Error, TEXT("Rollback SDP is currently unsupported. SDP is: %s"), *Sdp);
			break;
	}
}

void FStreamer::OnOffer(FPlayerId PlayerId, const FString& Sdp)
{
	webrtc::PeerConnectionInterface* PeerConnection = CreateSession(PlayerId, PixelStreamingProtocol::EPixelStreamingPlayerFlags::PSPFlag_SupportsDataChannel);
	if (PeerConnection)
	{
		webrtc::SdpParseError Error;
		std::unique_ptr<webrtc::SessionDescriptionInterface> SessionDesc = webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, to_string(Sdp), &Error);
		if (!SessionDesc)
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to parse offer's SDP: %s\n%s"), Error.description.c_str(), *Sdp);
			return;
		}

		SendAnswer(PlayerId, PeerConnection, TUniquePtr<webrtc::SessionDescriptionInterface>(SessionDesc.release()));
		ForceKeyFrame();
	}
	else
	{
		UE_LOG(PixelStreamer, Error, TEXT("Failed to create player session, peer connection was nullptr."));
	}
}

void FStreamer::SetLocalDescription(webrtc::PeerConnectionInterface* PeerConnection, FSetSessionDescriptionObserver* Observer, webrtc::SessionDescriptionInterface* SDP)
{
	// Note from Luke about WebRTC: the sink of video capturer will be added as a direct result
	// of `PeerConnection->SetLocalDescription()` call but video encoder will be created later on
	// when the first frame is pushed into the WebRTC pipeline (by the capturer calling `OnFrame`).

	// Note from Luke about Pixel Streaming: Internally we associate `FPlayerId` with each `FVideoCapturer` and
	// pass an "initialisation frame" that contains `FPlayerId` through `OnFrame` to the encoder.
	// This initialization frame establishes the correct association between the player and `FPixelStreamingVideoEncoder`.
	// The reason we want this association between encoder<->peer is is so that the quality controlling peer
	// can change encoder bitrate etc, while the other peers cannot.

	PeerConnection->SetLocalDescription(Observer, SDP);

	// Once local description has been set we can start setting some encoding information for the video stream rtp sender
	for (rtc::scoped_refptr<webrtc::RtpSenderInterface> Sender : PeerConnection->GetSenders())
	{
		cricket::MediaType MediaType = Sender->media_type();
		if (MediaType == cricket::MediaType::MEDIA_TYPE_VIDEO)
		{
			webrtc::RtpParameters ExistingParams = Sender->GetParameters();

			// Set the degradation preference based on our CVar for it.
			ExistingParams.degradation_preference = PixelStreamingSettings::GetDegradationPreference();

			webrtc::RTCError Err = Sender->SetParameters(ExistingParams);
			if (!Err.ok())
			{
				const char* ErrMsg = Err.message();
				FString ErrorStr(ErrMsg);
				UE_LOG(PixelStreamer, Error, TEXT("Failed to set RTP Sender params: %s"), *ErrorStr);
			}
		}
	}

	// Note: if desired, manually limiting total bitrate for a peer is possible in PeerConnection::SetBitrate(const BitrateSettings& bitrate)
}

void FStreamer::SendAnswer(FPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection, TUniquePtr<webrtc::SessionDescriptionInterface> Sdp)
{
	// below is async execution (with error handling) of:
	//		PeerConnection.SetRemoteDescription(SDP);
	//		Answer = PeerConnection.CreateAnswer();
	//		PeerConnection.SetLocalDescription(Answer);
	//		SignallingServerConnection.SendAnswer(Answer);

	FSetSessionDescriptionObserver* SetLocalDescriptionObserver = FSetSessionDescriptionObserver::Create(
		[this, PlayerId, PeerConnection]() // on success
		{
			this->GetSignallingServerConnection()->SendAnswer(PlayerId, *PeerConnection->local_description());
			this->SetStreamingStarted(true);
		},
		[this, PlayerId](const FString& Error) // on failure
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to set local description: %s"), *Error);
			this->PlayerSessions.DisconnectPlayer(PlayerId, Error);
		});

	auto OnCreateAnswerSuccess = [this, PlayerId, PeerConnection, SetLocalDescriptionObserver](webrtc::SessionDescriptionInterface* SDP) {
		SetLocalDescription(PeerConnection, SetLocalDescriptionObserver, SDP);
	};

	FCreateSessionDescriptionObserver* CreateAnswerObserver = FCreateSessionDescriptionObserver::Create(
		MoveTemp(OnCreateAnswerSuccess),
		[this, PlayerId](const FString& Error) // on failure
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to create answer: %s"), *Error);
			this->PlayerSessions.DisconnectPlayer(PlayerId, Error);
		});

	auto OnSetRemoteDescriptionSuccess = [this, PeerConnection, CreateAnswerObserver]() {
		// Note: these offer to receive at superseded now we are use transceivers to setup our peer connection media
		int offer_to_receive_video = 0;
		int offer_to_receive_audio = PixelStreamingSettings::CVarPixelStreamingWebRTCDisableReceiveAudio.GetValueOnAnyThread() ? 0 : 1;
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

		// modify audio transceiver direction based on CVars just before creating answer
		this->ModifyAudioTransceiverDirection(PeerConnection);

		PeerConnection->CreateAnswer(CreateAnswerObserver, AnswerOption);
	};

	FSetSessionDescriptionObserver* SetRemoteDescriptionObserver = FSetSessionDescriptionObserver::Create(
		MoveTemp(OnSetRemoteDescriptionSuccess),
		[this, PlayerId](const FString& Error) // on failure
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to set remote description: %s"), *Error);
			this->PlayerSessions.DisconnectPlayer(PlayerId, Error);
		});

	PeerConnection->SetRemoteDescription(SetRemoteDescriptionObserver, Sdp.Release());
}

void FStreamer::ModifyAudioTransceiverDirection(webrtc::PeerConnectionInterface* PeerConnection)
{
	//virtual std::vector<rtc::scoped_refptr<RtpTransceiverInterface>>GetTransceivers()
	std::vector<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> Transceivers = PeerConnection->GetTransceivers();
	for (auto& Transceiver : Transceivers)
	{
		if (Transceiver->media_type() == cricket::MediaType::MEDIA_TYPE_AUDIO)
		{

			bool bTransmitUEAudio = !PixelStreamingSettings::CVarPixelStreamingWebRTCDisableTransmitAudio.GetValueOnAnyThread();
			bool bReceiveBrowserAudio = !PixelStreamingSettings::CVarPixelStreamingWebRTCDisableReceiveAudio.GetValueOnAnyThread();

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
		}
	}
}

void FStreamer::OnRemoteIceCandidate(FPlayerId PlayerId, const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp)
{
	this->PlayerSessions.OnRemoteIceCandidate(PlayerId, SdpMid, SdpMLineIndex, Sdp);
}

void FStreamer::OnPlayerConnected(FPlayerId PlayerId, int Flags)
{
	// create peer connection
	if (webrtc::PeerConnectionInterface* PlayerSession = CreateSession(PlayerId, Flags))
	{
		// make them send only
		std::vector<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> Transceivers = PlayerSession->GetTransceivers();
		for (auto& Transceiver : Transceivers)
		{
			Transceiver->SetDirection(webrtc::RtpTransceiverDirection::kSendOnly);
		}

		// observer for creating offer
		FCreateSessionDescriptionObserver* CreateOfferObserver = FCreateSessionDescriptionObserver::Create(
			[this, PlayerId, PlayerSession](webrtc::SessionDescriptionInterface* SDP) // on SDP create success
			{
				FSetSessionDescriptionObserver* SetLocalDescriptionObserver = FSetSessionDescriptionObserver::Create(
					[this, PlayerId, SDP]() // on SDP set success
					{
						GetSignallingServerConnection()->SendOffer(PlayerId, *SDP);
					},
					[](const FString& Error) // on SDP set failure
					{
						UE_LOG(PixelStreamer, Error, TEXT("Failed to set local description: %s"), *Error);
					});

				SetLocalDescription(PlayerSession, SetLocalDescriptionObserver, SDP);
			},
			[](const FString& Error) // on SDP create failure
			{
				UE_LOG(PixelStreamer, Error, TEXT("Failed to create offer: %s"), *Error);
			});

		PlayerSession->CreateOffer(CreateOfferObserver, {});
	}
}

void FStreamer::OnPlayerDisconnected(FPlayerId PlayerId)
{
	UE_LOG(PixelStreamer, Log, TEXT("player %s disconnected"), *PlayerId);
	DeletePlayerSession(PlayerId);
}

void FStreamer::OnSignallingServerDisconnected()
{
	DeleteAllPlayerSessions();

	// Call disconnect here makes sure any other websocket callbacks etc are cleared
	this->SignallingServerConnection->Disconnect();
	ConnectToSignallingServer();
}

int FStreamer::GetNumPlayers() const
{
	return this->PlayerSessions.GetNumPlayers();
}

void FStreamer::ForceKeyFrame()
{
	if (this->VideoEncoderFactory)
	{
		this->VideoEncoderFactory->ForceKeyFrame();
	}
	else
	{
		UE_LOG(PixelStreamer, Log, TEXT("Cannot force a key frame - video encoder factory is nullptr."));
	}
}

void FStreamer::SetQualityController(FPlayerId PlayerId)
{

	this->PlayerSessions.SetQualityController(PlayerId);
}

bool FStreamer::IsQualityController(FPlayerId PlayerId) const
{
	return this->PlayerSessions.IsQualityController(PlayerId);
}

IPixelStreamingAudioSink* FStreamer::GetAudioSink(FPlayerId PlayerId) const
{
	return this->PlayerSessions.GetAudioSink(PlayerId);
}

IPixelStreamingAudioSink* FStreamer::GetUnlistenedAudioSink() const
{
	return this->PlayerSessions.GetUnlistenedAudioSink();
}

bool FStreamer::SendMessage(FPlayerId PlayerId, PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const
{
	return this->PlayerSessions.SendMessage(PlayerId, Type, Descriptor);
}

void FStreamer::SendLatestQP(FPlayerId PlayerId, int LatestQP) const
{
	this->PlayerSessions.SendLatestQP(PlayerId, LatestQP);
}

void FStreamer::PollWebRTCStats() const
{
	this->PlayerSessions.PollWebRTCStats();
}

void FStreamer::DeleteAllPlayerSessions()
{
	this->PlayerSessions.DeleteAllPlayerSessions();
	this->bStreamingStarted = false;
}

void FStreamer::DeletePlayerSession(FPlayerId PlayerId)
{
	int NumRemainingPlayers = this->PlayerSessions.DeletePlayerSession(PlayerId);

	if (NumRemainingPlayers == 0)
	{
		this->bStreamingStarted = false;
	}
}

void FStreamer::AddStreams(FPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection, int Flags)
{
	check(PeerConnection);

	bool bSyncVideoAndAudio = !PixelStreamingSettings::CVarPixelStreamingWebRTCDisableAudioSync.GetValueOnAnyThread();

	const FString AudioStreamId = bSyncVideoAndAudio ? TEXT("pixelstreaming_av_stream_id") : TEXT("pixelstreaming_audio_stream_id");
	const FString VideoStreamId = bSyncVideoAndAudio ? TEXT("pixelstreaming_av_stream_id") : TEXT("pixelstreaming_video_stream_id");
	const FString AudioTrackLabel = TEXT("pixelstreaming_audio_track_label");
	const FString VideoTrackLabel = TEXT("pixelstreaming_video_track_label");

	if (!PeerConnection->GetSenders().empty())
	{
		return; // Already added tracks
	}

	// Use PeerConnection's transceiver API to add create audio/video tracks will correct directionality.
	// These tracks are only thin wrappers around the underlying sources (the sources are shared among all peer's tracks).
	// As per the WebRTC source: "The same source can be used by multiple VideoTracks."
	this->SetupVideoTrack(PlayerId, PeerConnection, VideoStreamId, VideoTrackLabel, Flags);
	this->SetupAudioTrack(PeerConnection, AudioStreamId, AudioTrackLabel);
}

void FStreamer::SetupVideoTrack(FPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection, const FString VideoStreamId, const FString VideoTrackLabel, int Flags)
{
	// Create a video source for this player
	FPlayerVideoSource* PlayerVideoSource = FramePump.CreatePlayerVideoSource(PlayerId, Flags);

	// Create video track
	rtc::scoped_refptr<webrtc::VideoTrackInterface> VideoTrack = PeerConnectionFactory->CreateVideoTrack(TCHAR_TO_UTF8(*VideoTrackLabel), PlayerVideoSource);

	// Add tranceiver
	webrtc::RtpTransceiverInit TransceiverOptions;
	TransceiverOptions.stream_ids = { TCHAR_TO_UTF8(*VideoStreamId) };

	bool bIsSFU = (Flags & PixelStreamingProtocol::EPixelStreamingPlayerFlags::PSPFlag_IsSFU) != PixelStreamingProtocol::EPixelStreamingPlayerFlags::PSPFlag_None;

	if (bIsSFU && PixelStreamingSettings::SimulcastParameters.Layers.Num() > 0)
	{

		// encodings should be lowest res to highest
		TArray<PixelStreamingSettings::FSimulcastParameters::FLayer*> SortedLayers;
		for (auto& Layer : PixelStreamingSettings::SimulcastParameters.Layers)
		{
			SortedLayers.Add(&Layer);
		}

		SortedLayers.Sort([](const auto& LayerA, const auto& LayerB) { return LayerA.Scaling > LayerB.Scaling; });

		const int LayerCount = SortedLayers.Num();
		for (int i = 0; i < LayerCount; ++i)
		{
			const auto SimulcastLayer = SortedLayers[i];
			webrtc::RtpEncodingParameters LayerEncoding{};
			LayerEncoding.rid = TCHAR_TO_UTF8(*(FString("simulcast") + FString::FromInt(LayerCount - i)));
			LayerEncoding.min_bitrate_bps = SimulcastLayer->MinBitrate;
			LayerEncoding.max_bitrate_bps = SimulcastLayer->MaxBitrate;
			LayerEncoding.scale_resolution_down_by = SimulcastLayer->Scaling;
			LayerEncoding.max_framerate = FMath::Max(60, PixelStreamingSettings::CVarPixelStreamingWebRTCFps.GetValueOnAnyThread());
			TransceiverOptions.send_encodings.push_back(LayerEncoding);
		}
	}
	else
	{
		webrtc::RtpEncodingParameters encoding{};
		encoding.rid = "base";
		encoding.max_bitrate_bps = PixelStreamingSettings::CVarPixelStreamingWebRTCMaxBitrate.GetValueOnAnyThread();
		encoding.min_bitrate_bps = PixelStreamingSettings::CVarPixelStreamingWebRTCMinBitrate.GetValueOnAnyThread();
		encoding.max_framerate = FMath::Max(60, PixelStreamingSettings::CVarPixelStreamingWebRTCFps.GetValueOnAnyThread());
		encoding.scale_resolution_down_by.reset();
		TransceiverOptions.send_encodings.push_back(encoding);
	}

	webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> Result = PeerConnection->AddTransceiver(VideoTrack, TransceiverOptions);
	checkf(Result.ok(), TEXT("Failed to add Video transceiver to PeerConnection. Msg=%s"), *FString(Result.error().message()));

	// Set some content hints based on degradation prefs, WebRTC uses these internally.
	webrtc::DegradationPreference DegradationPref = PixelStreamingSettings::GetDegradationPreference();
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
}

void FStreamer::SetupAudioTrack(webrtc::PeerConnectionInterface* PeerConnection, FString const AudioStreamId, FString const AudioTrackLabel)
{
	bool bTransmitUEAudio = !PixelStreamingSettings::CVarPixelStreamingWebRTCDisableTransmitAudio.GetValueOnAnyThread();
	bool bReceiveBrowserAudio = !PixelStreamingSettings::CVarPixelStreamingWebRTCDisableReceiveAudio.GetValueOnAnyThread();

	// Create one and only one audio source for Pixel Streaming.
	if (!AudioSource && bTransmitUEAudio)
	{
		// Setup audio source options, we turn off many of the "nice" audio settings that
		// would traditionally be used in a conference call because the audio source we are
		// transmitting is UE application audio (not some unknown microphone).
		this->AudioSourceOptions.echo_cancellation = false;
		this->AudioSourceOptions.auto_gain_control = false;
		this->AudioSourceOptions.noise_suppression = false;
		this->AudioSourceOptions.highpass_filter = false;
		this->AudioSourceOptions.stereo_swapping = false;
		this->AudioSourceOptions.audio_jitter_buffer_max_packets = 1000;
		this->AudioSourceOptions.audio_jitter_buffer_fast_accelerate = false;
		this->AudioSourceOptions.audio_jitter_buffer_min_delay_ms = 0;
		this->AudioSourceOptions.audio_jitter_buffer_enable_rtx_handling = false;
		this->AudioSourceOptions.typing_detection = false;
		this->AudioSourceOptions.experimental_agc = false;
		this->AudioSourceOptions.experimental_ns = false;
		this->AudioSourceOptions.residual_echo_detector = false;
		// Create audio source
		AudioSource = PeerConnectionFactory->CreateAudioSource(this->AudioSourceOptions);
	}

	// Add the audio track to the audio transceiver's sender if we are transmitting audio
	if (bTransmitUEAudio)
	{

		rtc::scoped_refptr<webrtc::AudioTrackInterface> AudioTrack = PeerConnectionFactory->CreateAudioTrack(TCHAR_TO_UTF8(*AudioTrackLabel), AudioSource);

		// Add the track
		webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>> Result = PeerConnection->AddTrack(AudioTrack, { TCHAR_TO_UTF8(*AudioStreamId) });

		if (!Result.ok())
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to add audio track to PeerConnection. Msg=%s"), TCHAR_TO_UTF8(Result.error().message()));
		}
	}
}

void FStreamer::SendPlayerMessage(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor)
{
	this->PlayerSessions.SendMessageAll(Type, Descriptor);
}

void FStreamer::SendFreezeFrame(const TArray64<uint8>& JpegBytes)
{
	if (!this->bStreamingStarted)
	{
		return;
	}

	this->PlayerSessions.SendFreezeFrame(JpegBytes);

	CachedJpegBytes = JpegBytes;
}

void FStreamer::SendCachedFreezeFrameTo(FPlayerId PlayerId) const
{
	if (this->CachedJpegBytes.Num() > 0)
	{
		this->PlayerSessions.SendFreezeFrameTo(PlayerId, this->CachedJpegBytes);
	}
}

void FStreamer::SendFreezeFrameTo(FPlayerId PlayerId, const TArray64<uint8>& JpegBytes) const
{
	this->PlayerSessions.SendFreezeFrameTo(PlayerId, JpegBytes);
}

void FStreamer::SendUnfreezeFrame()
{
	// Force a keyframe so when stream unfreezes if player has never received a h.264 frame before they can still connect.
	this->ForceKeyFrame();

	this->PlayerSessions.SendUnfreezeFrame();

	CachedJpegBytes.Empty();
}

void FStreamer::SendFileData(TArray<uint8>& ByteData, FString& MimeType, FString& FileExtension)
{
	this->PlayerSessions.SendFileData(ByteData, MimeType, FileExtension);
}

void FStreamer::AddAnyStatChangedCallback(TWeakPtr<IPixelStreamingStatsConsumer> Callback)
{
	this->Stats.AddOnAnyStatChangedCallback(Callback);
}

void FStreamer::RemoveAnyStatChangedCallback(TWeakPtr<IPixelStreamingStatsConsumer> Callback)
{
	this->Stats.RemoveOnAnyStatChangedCallback(Callback);
}
