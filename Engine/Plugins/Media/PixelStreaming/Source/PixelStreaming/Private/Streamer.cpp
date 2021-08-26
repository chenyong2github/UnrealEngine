// Copyright Epic Games, Inc. All Rights Reserved.

#include "Streamer.h"
#include "AudioCapturer.h"
#include "VideoCapturer.h"
#include "PlayerSession.h"
#include "VideoEncoderFactory.h"
#include "PixelStreamerDelegates.h"
#include "PixelStreamingEncoderFactory.h"
#include "PixelStreamingSettings.h"
#include "WebRTCLogging.h"
#include "WebSocketsModule.h"
#include "PixelStreamingAudioDeviceModule.h"
#include "PixelStreamingAudioSink.h"


DEFINE_LOG_CATEGORY(PixelStreamer);


bool FStreamer::CheckPlatformCompatibility()
{
	return AVEncoder::FVideoEncoderFactory::Get().HasEncoderForCodec(AVEncoder::ECodecType::H264);
}

FStreamer::FStreamer(const FString& InSignallingServerUrl, const FString& InStreamerId)
	: SignallingServerUrl(InSignallingServerUrl), StreamerId(InStreamerId)
{
	RedirectWebRtcLogsToUnreal(rtc::LoggingSeverity::LS_VERBOSE);

	FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("AVEncoder"));

	// required for communication with Signalling Server and must be called in the game thread, while it's used in signalling thread
	FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");

	StartWebRtcSignallingThread();
	ConnectToSignallingServer();
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
	WebRtcSignallingThread = MakeUnique<rtc::Thread>(rtc::SocketServer::CreateDefault());
	WebRtcSignallingThread->SetName("WebRtcSignallingThread", nullptr);
	WebRtcSignallingThread->Start();

	rtc::InitializeSSL();

	PeerConnectionConfig = {};

	std::unique_ptr<FPixelStreamingVideoEncoderFactory> videoEncoderFactory = std::make_unique<FPixelStreamingVideoEncoderFactory>(this);
	this->VideoEncoderFactory = videoEncoderFactory.get();

	bool bUseLegacyAudioDeviceModule = PixelStreamingSettings::CVarPixelStreamingWebRTCUseLegacyAudioDevice.GetValueOnAnyThread();
	rtc::scoped_refptr<webrtc::AudioDeviceModule> AudioDeviceModule;
	if(bUseLegacyAudioDeviceModule)
	{
		AudioDeviceModule = new rtc::RefCountedObject<FAudioCapturer>();
	}
	else
	{
		AudioDeviceModule = new rtc::RefCountedObject<FPixelStreamingAudioDeviceModule>();
	}

	PeerConnectionFactory = webrtc::CreatePeerConnectionFactory(
		nullptr, // network_thread
		nullptr, // worker_thread
		WebRtcSignallingThread.Get(), // signal_thread
		AudioDeviceModule, // audio device manager
		webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>(),
		webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>(),
		std::move(videoEncoderFactory),
		std::make_unique<webrtc::InternalDecoderFactory>(),
		nullptr, // audio_mixer
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
	SignallingServerConnection = MakeUnique<FSignallingServerConnection>(SignallingServerUrl, *this, StreamerId);
}

void FStreamer::OnFrameBufferReady(const FTexture2DRHIRef& FrameBuffer)
{
	if (bStreamingStarted && VideoSource)
	{
		VideoSource->OnFrameReady(FrameBuffer);
	}
}

void FStreamer::OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config)
{
	this->PeerConnectionConfig = Config;
}

void FStreamer::OnOffer(FPlayerId PlayerId, TUniquePtr<webrtc::SessionDescriptionInterface> Sdp)
{
	CreatePlayerSession(PlayerId);
	AddStreams(PlayerId);

	FPlayerSession* Player = this->GetPlayerSession(PlayerId);
	checkf(Player, TEXT("Somehow a player we just created player %s was not found."), *PlayerId);

	Player->OnOffer(MoveTemp(Sdp));

	{
		FScopeLock PlayersLock(&PlayersCS);
		for (auto&& PlayerEntry : Players)
		{
			PlayerEntry.Value->SendKeyFrame();
		}
	}
}

void FStreamer::OnRemoteIceCandidate(FPlayerId PlayerId, TUniquePtr<webrtc::IceCandidateInterface> Candidate)
{
	FPlayerSession* Player = GetPlayerSession(PlayerId);
	if(Player)
	{
		Player->OnRemoteIceCandidate(MoveTemp(Candidate));
	}
	else
	{
		UE_LOG(PixelStreamer, Log, TEXT("Could not pass remote ice candidate to player because Player %s no available."), *PlayerId);
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
	ConnectToSignallingServer();
}

int FStreamer::GetNumPlayers() const
{
	return this->Players.Num();
}

int32 FStreamer::GetPlayers(TArray<FPlayerId> OutPlayerIds) const
{
	FScopeLock PlayersLock(&PlayersCS);
	return this->Players.GetKeys(OutPlayerIds);
}

FPlayerSession* FStreamer::GetPlayerSession(FPlayerId PlayerId) const
{
	FScopeLock PlayersLock(&PlayersCS);
	if(Players.Contains(PlayerId))
	{
		return Players[PlayerId];
	}

	return nullptr;
}

bool FStreamer::SetQualityController(FPlayerId PlayerId)
{

	FScopeLock PlayersLock(&PlayersCS);

	if (this->Players.Contains(PlayerId))
	{

		// Lock just for quality controller change
		{
			FScopeLock QualityControllerLock(&QualityControllerCS);
			this->QualityControllingPlayer = PlayerId;
			UE_LOG(PixelStreamer, Log, TEXT("Quality controller is now PlayerId=%s."), *PlayerId);
		}

		// Update quality controller status on the browser side too
		for (auto& Entry : Players)
		{
			FPlayerSession* Session = Entry.Value;
			if(Session)
			{
				Session->SendQualityControlStatus();
			}
		}

		return true;
	}
	return false;
}

bool FStreamer::IsQualityController(FPlayerId PlayerId) const
{
	FScopeLock QualityControllerLock(&QualityControllerCS);
	return this->QualityControllingPlayer == PlayerId;
}

FPixelStreamingAudioSink* FStreamer::GetAudioSink(FPlayerId PlayerId) const
{
	FPlayerSession* Session = this->GetPlayerSession(PlayerId);
	if(Session)
	{
		return &Session->GetAudioSink();
	}
	return nullptr;
}

FPixelStreamingAudioSink* FStreamer::GetUnlistenedAudioSink() const
{
	FScopeLock PlayersLock(&PlayersCS);
	for (auto& Entry : Players)
	{
		FPlayerSession* Session = Entry.Value;
		FPixelStreamingAudioSink& AudioSink = Session->GetAudioSink();
		if(!AudioSink.HasAudioConsumers())
		{
			return &Session->GetAudioSink();
		}
	}
	return nullptr;
}

bool FStreamer::SendMessage(FPlayerId PlayerId, PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const 
{
	FPlayerSession* Session = this->GetPlayerSession(PlayerId);
	if(Session)
	{
		return Session->SendMessage(Type, Descriptor);
	}
	else
	{
		UE_LOG(PixelStreamer, Log, TEXT("Could not send message over data using PlayerId=%s because that player was not found."), *PlayerId);
	}
	return false;
}

void FStreamer::SendLatestQP(FPlayerId PlayerId) const
{
	FPlayerSession* Session = this->GetPlayerSession(PlayerId);
	if(Session)
	{
		return Session->SendVideoEncoderQP();
	}
	else
	{
		UE_LOG(PixelStreamer, Log, TEXT("Could not send latest QP for PlayerId=%s because that player was not found."), *PlayerId);
	}
}

void FStreamer::AssociateVideoEncoder(FPlayerId AssociatedPlayerId, FPixelStreamingVideoEncoder* VideoEncoder)
{
	FPlayerSession* Session = this->GetPlayerSession(AssociatedPlayerId);
	if(Session)
	{
		return Session->SetVideoEncoder(VideoEncoder);
	}
	else
	{
		UE_LOG(PixelStreamer, Log, TEXT("Could not set video encoder for PlayerId=%s because that player was not found."), *AssociatedPlayerId);
	}
}

void FStreamer::DeleteAllPlayerSessions()
{
	{
		FScopeLock PlayersLock(&PlayersCS);
		while (Players.Num() > 0)
		{
			DeletePlayerSession(Players.CreateIterator().Key());
		}
	}
}

void FStreamer::CreatePlayerSession(FPlayerId PlayerId)
{
	check(PeerConnectionFactory);

	// With unified plan, we get several calls to OnOffer, which in turn calls
	// this several times.
	// Therefore, we only try to create the player if not created already
	{
		FScopeLock PlayersLock(&PlayersCS);
		if (Players.Contains(PlayerId))
		{
			return;
		}
	}

	UE_LOG(PixelStreamer, Log, TEXT("Creating player session for PlayerId=%s"), *PlayerId);
	
	// this is called from WebRTC signalling thread, the only thread were `Players` map is modified, so no need to lock it
	bool bMakeQualityController = Players.Num() == 0; // first player controls quality by default
	FPlayerSession* Session = new FPlayerSession(*this, PlayerId);

	rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection = PeerConnectionFactory->CreatePeerConnection(PeerConnectionConfig, webrtc::PeerConnectionDependencies{ Session });
	check(PeerConnection);

	// Setup suggested bitrate settings on the Peer Connection based on our CVars
	webrtc::BitrateSettings BitrateSettings;
	BitrateSettings.min_bitrate_bps = PixelStreamingSettings::CVarPixelStreamingWebRTCMinBitrate.GetValueOnAnyThread();
	BitrateSettings.max_bitrate_bps = PixelStreamingSettings::CVarPixelStreamingWebRTCMaxBitrate.GetValueOnAnyThread();
	BitrateSettings.start_bitrate_bps = PixelStreamingSettings::CVarPixelStreamingWebRTCStartBitrate.GetValueOnAnyThread();
	PeerConnection->SetBitrate(BitrateSettings);

	Session->SetPeerConnection(PeerConnection);

	{
		FScopeLock PlayersLock(&PlayersCS);
		// This means the player map will hold a SharedPtr reference, so it shouldn't be deleted until this player is removed.
		Players.Add(PlayerId, Session);
	}

	if(bMakeQualityController)
	{
		Session->SetQualityController(bMakeQualityController);
	}

	if (UPixelStreamerDelegates* Delegates = UPixelStreamerDelegates::GetPixelStreamerDelegates())
	{
		Delegates->OnNewConnection.Broadcast(PlayerId, bMakeQualityController);
	}
}

void FStreamer::DeletePlayerSession(FPlayerId PlayerId)
{
	FPlayerSession* Player = this->GetPlayerSession(PlayerId);
	if (!Player)
	{
		UE_LOG(PixelStreamer, VeryVerbose, TEXT("Failed to delete player %s - that player was not found."), *PlayerId);
		return;
	}

	bool bWasQualityController = Player->IsQualityController();

	{
		FScopeLock PlayersLock(&PlayersCS);
		Players.Remove(PlayerId);
	}

	UPixelStreamerDelegates* Delegates = UPixelStreamerDelegates::GetPixelStreamerDelegates();
	if (Delegates)
	{
		Delegates->OnClosedConnection.Broadcast(PlayerId, bWasQualityController);
	}

	// this is called from WebRTC signalling thread, the only thread were `Players` map is modified, so no need to lock it
	if (Players.Num() == 0)
	{
		bStreamingStarted = false;

		// Inform the application-specific blueprint that nobody is viewing or
		// interacting with the app. This is an opportunity to reset the app.
		if (Delegates)
		{
			Delegates->OnAllConnectionsClosed.Broadcast();
		}
	}
	else if (bWasQualityController)
	{
		// Quality Controller session has been just removed, set quality control to any of remaining sessions
		TArray<FPlayerId> PlayerIds;
		Players.GetKeys(PlayerIds);
		check(PlayerIds.Num() != 0);
		this->SetQualityController(PlayerIds[0]);
	}
}

void FStreamer::AddStreams(FPlayerId PlayerId)
{

	FPlayerSession* Session = this->GetPlayerSession(PlayerId);
	if(!Session)
	{
		UE_LOG(PixelStreamer, VeryVerbose, TEXT("Failed to create media stream for player %s - that player was not found."), *PlayerId);
		return;
	}

	bool bSyncVideoAndAudio = !PixelStreamingSettings::CVarPixelStreamingWebRTCDisableAudioSync.GetValueOnAnyThread();
	
	FString const AudioStreamId = bSyncVideoAndAudio ? TEXT("pixelstreaming_av_stream_id") : TEXT("pixelstreaming_audio_stream_id");
	FString const VideoStreamId = bSyncVideoAndAudio ? TEXT("pixelstreaming_av_stream_id") : TEXT("pixelstreaming_video_stream_id");
	FString const AudioTrackLabel = TEXT("pixelstreaming_audio_track_label");
	FString const VideoTrackLabel = TEXT("pixelstreaming_video_track_label");

	if (!Session->GetPeerConnection().GetSenders().empty())
	{
		return;  // Already added tracks
	}

	// Use PeerConnection's transceiver API to add create audio/video tracks will correct directionality.
	// These tracks are only thin wrappers around the underlying sources (the sources are shared among all peer's tracks).
	// As per the WebRTC source: "The same source can be used by multiple VideoTracks."
	this->SetupVideoTrack(Session, VideoStreamId, VideoTrackLabel);
	this->SetupAudioTrack(Session, AudioStreamId, AudioTrackLabel);

}

void FStreamer::SetupVideoTrack(FPlayerSession* Session, FString const VideoStreamId, FString const VideoTrackLabel)
{
	// Create one and only one VideoCapturer for Pixel Streaming.
	// Video capturuer is actually a "VideoSource" in WebRTC terminology.
	if (!VideoSource)
	{
		VideoSource = new FVideoCapturer();
	}

	// Create video track
	rtc::scoped_refptr<webrtc::VideoTrackInterface> VideoTrack = PeerConnectionFactory->CreateVideoTrack(TCHAR_TO_UTF8(*VideoTrackLabel), VideoSource);

	// Add the track
	webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>> Result = Session->GetPeerConnection().AddTrack(VideoTrack, { TCHAR_TO_UTF8(*VideoStreamId) });

	if (Result.ok())
	{
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
	else
	{
		UE_LOG(PixelStreamer, Error, TEXT("Failed to add Video transceiver to PeerConnection of player %s. Msg=%s"), *Session->GetPlayerId(), TCHAR_TO_UTF8(Result.error().message()));
	}

}

void FStreamer::SetupAudioTrack(FPlayerSession* Session, FString const AudioStreamId, FString const AudioTrackLabel)
{
	bool bTransmitUEAudio = !PixelStreamingSettings::CVarPixelStreamingWebRTCDisableTransmitAudio.GetValueOnAnyThread();
	bool bReceiveBrowserAudio = !PixelStreamingSettings::CVarPixelStreamingWebRTCDisableReceiveAudio.GetValueOnAnyThread();

	// Create one and only one audio source for Pixel Streaming.
	if (!AudioSource && bTransmitUEAudio)
	{
		// Setup audio source options, we turn off many of the "nice" audio settings that
		// would traditionally be used in a conference call because the audio source we are 
		// transmitting is UE application audio (not some unknown microphone).
		this->AudioSourceOptions.echo_cancellation 							= false;
		this->AudioSourceOptions.auto_gain_control 							= false;
		this->AudioSourceOptions.noise_suppression 							= false;
		this->AudioSourceOptions.highpass_filter 							= false;
		this->AudioSourceOptions.stereo_swapping 							= false;
		this->AudioSourceOptions.audio_jitter_buffer_max_packets 			= 1000;
		this->AudioSourceOptions.audio_jitter_buffer_fast_accelerate 		= false;
		this->AudioSourceOptions.audio_jitter_buffer_min_delay_ms 			= 0;
		this->AudioSourceOptions.audio_jitter_buffer_enable_rtx_handling 	= false;
		this->AudioSourceOptions.typing_detection 							= false;
		this->AudioSourceOptions.experimental_agc							= false;
		this->AudioSourceOptions.experimental_ns 							= false;
		this->AudioSourceOptions.residual_echo_detector						= false;
		// Create audio source
		AudioSource = PeerConnectionFactory->CreateAudioSource(this->AudioSourceOptions);
	}

	// Add the audio track to the audio transceiver's sender if we are transmitting audio
	if(bTransmitUEAudio)
	{

		rtc::scoped_refptr<webrtc::AudioTrackInterface> AudioTrack =  PeerConnectionFactory->CreateAudioTrack(TCHAR_TO_UTF8(*AudioTrackLabel), AudioSource);

		// Add the track
		webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>> Result = Session->GetPeerConnection().AddTrack(AudioTrack, { TCHAR_TO_UTF8(*AudioStreamId) });

		if(!Result.ok())
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to add audio track to PeerConnection of player %s. Msg=%s"), *Session->GetPlayerId(), TCHAR_TO_UTF8(Result.error().message()));
		}
	}
}

void FStreamer::SendPlayerMessage(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor)
{
	UE_LOG(PixelStreamer, Log, TEXT("SendPlayerMessage: %d - %s"), static_cast<int32>(Type), *Descriptor);
	{
		FScopeLock PlayersLock(&PlayersCS);
		for (auto&& PlayerEntry : Players)
		{
			PlayerEntry.Value->SendMessage(Type, Descriptor);
		}
	}
}

void FStreamer::SendFreezeFrame(const TArray64<uint8>& JpegBytes)
{
	if(!this->bStreamingStarted)
	{
		return;
	}

	UE_LOG(PixelStreamer, Log, TEXT("Sending freeze frame to players: %d bytes"), JpegBytes.Num());
	{
		FScopeLock PlayersLock(&PlayersCS);
		for (auto&& PlayerEntry : Players)
		{
			PlayerEntry.Value->SendFreezeFrame(JpegBytes);
		}
	}

	CachedJpegBytes = JpegBytes;
}

void FStreamer::SendCachedFreezeFrameTo(FPlayerSession& Player)
{
	if (CachedJpegBytes.Num() > 0)
	{
		UE_LOG(PixelStreamer, Log, TEXT("Sending cached freeze frame to player %s: %d bytes"), *Player.GetPlayerId(), CachedJpegBytes.Num());
		Player.SendFreezeFrame(CachedJpegBytes);
	}
}

void FStreamer::SendUnfreezeFrame()
{
	UE_LOG(PixelStreamer, Log, TEXT("Sending unfreeze message to players"));

	{
		FScopeLock PlayersLock(&PlayersCS);
		for (auto&& PlayerEntry : Players)
		{
			PlayerEntry.Value->SendUnfreezeFrame();
			PlayerEntry.Value->SendKeyFrame();
		}
	}
	
	CachedJpegBytes.Empty();
}
