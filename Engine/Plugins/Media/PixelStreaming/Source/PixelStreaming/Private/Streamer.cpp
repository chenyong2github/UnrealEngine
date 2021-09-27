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
#include "WebRtcObservers.h"
#include "PixelStreamingVideoSources.h"


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
	, PlayerSessions(WebRtcSignallingThread.Get())
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
	this->SignallingServerConnection->Connect(SignallingServerUrl);
}

void FStreamer::OnFrameBufferReady(const FTexture2DRHIRef& FrameBuffer)
{
	if (bStreamingStarted)
	{
		this->VideoSources.OnFrameReady(FrameBuffer);
	}
}

void FStreamer::OnQualityControllerChanged(FPlayerId PlayerId)
{
	this->VideoSources.SetQualityController(PlayerId);
}

void FStreamer::PostPlayerDeleted(FPlayerId PlayerId)
{
	this->VideoSources.DeleteVideoSource(PlayerId);
}

void FStreamer::OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config)
{
	this->PeerConnectionConfig = Config;
}

void FStreamer::OnDataChannelOpen(FPlayerId PlayerId, webrtc::DataChannelInterface* DataChannel)
{
	if(DataChannel)
	{
		// When data channel is open, try to send cached freeze frame (if we have one)
		this->SendCachedFreezeFrameTo(PlayerId);	
	}
}

void FStreamer::OnOffer(FPlayerId PlayerId, TUniquePtr<webrtc::SessionDescriptionInterface> Sdp)
{

	webrtc::PeerConnectionInterface* PeerConnection = this->PlayerSessions.CreatePlayerSession(PlayerId, 
		this->PeerConnectionFactory, 
		this->PeerConnectionConfig, 
		this->SignallingServerConnection.Get());
	
	if(PeerConnection != nullptr)
	{
		// Bind to the OnDataChannelOpen delegate (we use this as the earliest time peer is ready for freeze frame)
		FPixelStreamingDataChannelObserver* DataChannelObserver = this->PlayerSessions.GetDataChannelObserver(PlayerId);
		check(DataChannelObserver);
		DataChannelObserver->OnDataChannelOpen.AddRaw(this, &FStreamer::OnDataChannelOpen );

		this->AddStreams(PlayerId, PeerConnection);
		this->HandleOffer(PlayerId, PeerConnection, MoveTemp(Sdp));
		this->ForceKeyFrame();
	}
}

void FStreamer::HandleOffer(FPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection, TUniquePtr<webrtc::SessionDescriptionInterface> Sdp)
{
	// below is async execution (with error handling) of:
	//		PeerConnection.SetRemoteDescription(SDP);
	//		Answer = PeerConnection.CreateAnswer();
	//		PeerConnection.SetLocalDescription(Answer);
	//		SignallingServerConnection.SendAnswer(Answer);

	FSetSessionDescriptionObserver* SetLocalDescriptionObserver = FSetSessionDescriptionObserver::Create
	(
		[this, PlayerId, PeerConnection]() // on success
		{
			this->GetSignallingServerConnection()->SendAnswer(PlayerId, *PeerConnection->local_description());
			this->SetStreamingStarted(true);
		},
		[this, PlayerId](const FString& Error) // on failure
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to set local description: %s"), *Error);
			this->PlayerSessions.DisconnectPlayer(PlayerId, Error);
		}
	);

	auto OnCreateAnswerSuccess = [this, PlayerId, PeerConnection, SetLocalDescriptionObserver](webrtc::SessionDescriptionInterface* SDP)
	{
		// Note from Luke about WebRTC: the sink of video capturer will be added as a direct result
		// of `PeerConnection->SetLocalDescription()` call but video encoder will be created later on
		// when the first frame is pushed into the WebRTC pipeline (by the capturer calling `OnFrame`).

		// Note from Luke about Pixel Streaming: Internally we associate `FPlayerId` with each `FVideoCapturer` and 
		// pass an "initialisation frame" that contains `FPlayerId` through `OnFrame` to the encoder. 
		// This initialization frame establishes the correct association between the player and `FPixelStreamingVideoEncoder`. 
		// The reason we want this association between encoder<->peer is is so that the quality controlling peer 
		// can change encoder bitrate etc, while the other peers cannot.

		PeerConnection->SetLocalDescription(SetLocalDescriptionObserver, SDP);

		// Once local description has been set we can start setting some encoding information for the video stream rtp sender
		std::vector<rtc::scoped_refptr<webrtc::RtpSenderInterface>> SendersArr = PeerConnection->GetSenders();
		for(rtc::scoped_refptr<webrtc::RtpSenderInterface> Sender : SendersArr)
		{
			cricket::MediaType MediaType = Sender->media_type();
			if(MediaType == cricket::MediaType::MEDIA_TYPE_VIDEO)
			{
				webrtc::RtpParameters ExistingParams = Sender->GetParameters();
				
				// Set the start min/max bitrate and max framerate for the codec.
				for (webrtc::RtpEncodingParameters& codec_params : ExistingParams.encodings)
				{
					codec_params.max_bitrate_bps = PixelStreamingSettings::CVarPixelStreamingWebRTCMaxBitrate.GetValueOnAnyThread();
					codec_params.min_bitrate_bps = PixelStreamingSettings::CVarPixelStreamingWebRTCMinBitrate.GetValueOnAnyThread();
					codec_params.max_framerate = PixelStreamingSettings::CVarPixelStreamingWebRTCMaxFps.GetValueOnAnyThread();
				}
				
				// Set the degradation preference based on our CVar for it.
				webrtc::DegradationPreference DegradationPref = PixelStreamingSettings::GetDegradationPreference();
				ExistingParams.degradation_preference = DegradationPref;

				webrtc::RTCError Err = Sender->SetParameters(ExistingParams);
				if(!Err.ok())
				{
					const char* ErrMsg = Err.message();
					FString ErrorStr(ErrMsg);
    				UE_LOG(PixelStreamer, Error, TEXT("Failed to set RTP Sender params: %s"), *ErrorStr);
				}
			}
		}

		// Note: if desired, manually limiting total bitrate for a peer is possible in PeerConnection::SetBitrate(const BitrateSettings& bitrate)

	};

	FCreateSessionDescriptionObserver* CreateAnswerObserver = FCreateSessionDescriptionObserver::Create
	(
		MoveTemp(OnCreateAnswerSuccess),
		[this, PlayerId](const FString& Error) // on failure
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to create answer: %s"), *Error);
			this->PlayerSessions.DisconnectPlayer(PlayerId, Error);
		}
	);

	auto OnSetRemoteDescriptionSuccess = [this, PeerConnection, CreateAnswerObserver]()
	{
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
		}
	);

	PeerConnection->SetRemoteDescription(SetRemoteDescriptionObserver, Sdp.Release());
}

void FStreamer::ModifyAudioTransceiverDirection(webrtc::PeerConnectionInterface* PeerConnection)
{
	//virtual std::vector<rtc::scoped_refptr<RtpTransceiverInterface>>GetTransceivers()
	std::vector<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> Transceivers = PeerConnection->GetTransceivers();
	for(auto& Transceiver : Transceivers)
	{
		if(Transceiver->media_type() == cricket::MediaType::MEDIA_TYPE_AUDIO)
		{

			bool bTransmitUEAudio = !PixelStreamingSettings::CVarPixelStreamingWebRTCDisableTransmitAudio.GetValueOnAnyThread();
			bool bReceiveBrowserAudio = !PixelStreamingSettings::CVarPixelStreamingWebRTCDisableReceiveAudio.GetValueOnAnyThread();

			// Determine the direction of the transceiver
			webrtc::RtpTransceiverDirection AudioTransceiverDirection;
			if(bTransmitUEAudio && bReceiveBrowserAudio)
			{
				AudioTransceiverDirection = webrtc::RtpTransceiverDirection::kSendRecv;
			}
			else if(bTransmitUEAudio)
			{
				AudioTransceiverDirection = webrtc::RtpTransceiverDirection::kSendOnly;
			}
			else if(bReceiveBrowserAudio)
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

void FStreamer::SendLatestQPAllPlayers() const
{
	if(this->VideoEncoderFactory)
	{
		double LatestQP = this->VideoEncoderFactory->GetLatestQP();
		this->PlayerSessions.SendLatestQPAllPlayers( (int)LatestQP );
	}
}

int FStreamer::GetNumPlayers() const
{
	return this->PlayerSessions.GetNumPlayers();
}

void FStreamer::ForceKeyFrame()
{
	if(this->VideoEncoderFactory)
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

void FStreamer::DeleteAllPlayerSessions()
{
	this->PlayerSessions.DeleteAllPlayerSessions();
	this->bStreamingStarted = false;
}

void FStreamer::DeletePlayerSession(FPlayerId PlayerId)
{
	int NumRemainingPlayers = this->PlayerSessions.DeletePlayerSession(PlayerId);

	if(NumRemainingPlayers == 0)
	{
		this->bStreamingStarted = false;
	}
}

void FStreamer::AddStreams(FPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection)
{
	check(PeerConnection);

	bool bSyncVideoAndAudio = !PixelStreamingSettings::CVarPixelStreamingWebRTCDisableAudioSync.GetValueOnAnyThread();
	
	FString const AudioStreamId = bSyncVideoAndAudio ? TEXT("pixelstreaming_av_stream_id") : TEXT("pixelstreaming_audio_stream_id");
	FString const VideoStreamId = bSyncVideoAndAudio ? TEXT("pixelstreaming_av_stream_id") : TEXT("pixelstreaming_video_stream_id");
	FString const AudioTrackLabel = TEXT("pixelstreaming_audio_track_label");
	FString const VideoTrackLabel = TEXT("pixelstreaming_video_track_label");

	if (!PeerConnection->GetSenders().empty())
	{
		return;  // Already added tracks
	}

	// Use PeerConnection's transceiver API to add create audio/video tracks will correct directionality.
	// These tracks are only thin wrappers around the underlying sources (the sources are shared among all peer's tracks).
	// As per the WebRTC source: "The same source can be used by multiple VideoTracks."
	this->SetupVideoTrack(PlayerId, PeerConnection, VideoStreamId, VideoTrackLabel);
	this->SetupAudioTrack(PeerConnection, AudioStreamId, AudioTrackLabel);

}

void FStreamer::SetupVideoTrack(FPlayerId PlayerId, webrtc::PeerConnectionInterface* PeerConnection, FString const VideoStreamId, FString const VideoTrackLabel)
{
	// Create a video source for this player
	webrtc::VideoTrackSourceInterface* VideoSource = this->VideoSources.CreateVideoSource(PlayerId);

	// Create video track
	rtc::scoped_refptr<webrtc::VideoTrackInterface> VideoTrack = PeerConnectionFactory->CreateVideoTrack(TCHAR_TO_UTF8(*VideoTrackLabel), VideoSource);

	// Add the track
	webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>> Result = PeerConnection->AddTrack(VideoTrack, { TCHAR_TO_UTF8(*VideoStreamId) });

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
		UE_LOG(PixelStreamer, Error, TEXT("Failed to add Video transceiver to PeerConnection. Msg=%s"), TCHAR_TO_UTF8(Result.error().message()));
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
		webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>> Result = PeerConnection->AddTrack(AudioTrack, { TCHAR_TO_UTF8(*AudioStreamId) });

		if(!Result.ok())
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
	if(!this->bStreamingStarted)
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
