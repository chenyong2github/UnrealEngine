// Copyright Epic Games, Inc. All Rights Reserved.

#include "Streamer.h"
#include "AudioCapturer.h"
#include "VideoCapturer.h"
#include "PlayerSession.h"
#include "Codecs/VideoEncoder.h"
#include "WebRtcLogging.h"
#include "PixelStreamerDelegates.h"

#include "Modules/ModuleManager.h"
#include "WebSocketsModule.h"
#include "HAL/Thread.h"

DEFINE_LOG_CATEGORY(PixelStreamer);

extern TAutoConsoleVariable<int32> CVarPixelStreamingEncoderMaxBitrate;

bool FStreamer::CheckPlatformCompatibility()
{
	return AVEncoder::FVideoEncoderFactory::FindFactory(TEXT("h264")) ? true : false;
}

FStreamer::FStreamer(const FString& InSignallingServerUrl):
	SignallingServerUrl(InSignallingServerUrl)
{
	RedirectWebRtcLogsToUE4(rtc::LoggingSeverity::LS_VERBOSE);

	FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("AVEncoder"));

	// required for communication with Signalling Server and must be called in the game thread, while it's used in signalling thread
	FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");

	FParse::Value(FCommandLine::Get(), TEXT("WebRtcPlanB"), *reinterpret_cast<uint8*>(&bPlanB));

	AVEncoder::FVideoEncoderFactory* UEVideoEncoderFactory = AVEncoder::FVideoEncoderFactory::FindFactory(TEXT("h264"));
	if (!UEVideoEncoderFactory)
	{
		UE_LOG(PixelStreamer, Fatal, TEXT("No video encoder found"));
	}

	HWEncoderDetails.InitialMaxFPS = GEngine->GetMaxFPS();
	if (HWEncoderDetails.InitialMaxFPS == 0)
	{
		check(IsInGameThread());
		HWEncoderDetails.InitialMaxFPS = 60;
		GEngine->SetMaxFPS(HWEncoderDetails.InitialMaxFPS);
	}

	HWEncoderDetails.Encoder = UEVideoEncoderFactory->CreateEncoder(TEXT("h264"));
	if (!HWEncoderDetails.Encoder)
	{
		UE_LOG(PixelStreamer, Fatal, TEXT("Could not create video encoder"));
	}

	AVEncoder::FVideoEncoderConfig Cfg;
	Cfg.Width = 1920;
	Cfg.Height = 1080;
	Cfg.Framerate = 60;
	Cfg.MaxBitrate = CVarPixelStreamingEncoderMaxBitrate.GetValueOnAnyThread();
	Cfg.Bitrate = FMath::Min((uint32)1000000, Cfg.MaxBitrate);
	Cfg.Preset = AVEncoder::FVideoEncoderConfig::EPreset::LowLatency;
	if (!HWEncoderDetails.Encoder->Initialize(Cfg))
	{
		UE_LOG(PixelStreamer, Fatal, TEXT("Could not initialize video encoder"));
	}

	VideoEncoderFactoryStrong = std::make_unique<FVideoEncoderFactory>(HWEncoderDetails);

	// #HACK: Keep a pointer to the Video encoder factory, so we can use it to figure out the
	// FPlayerSession <-> FVideoEncoder relationship later on
	VideoEncoderFactory = VideoEncoderFactoryStrong.get();

	// only now that VideoCapturer is created we can create signalling thread
	WebRtcSignallingThread = MakeUnique<FThread>(TEXT("PixelStreamer WebRTC signalling thread"), [this]() { WebRtcSignallingThreadFunc(); });
}

FStreamer::~FStreamer()
{
	// stop WebRtc WndProc thread
	PostThreadMessage(WebRtcSignallingThreadId, WM_QUIT, 0, 0);
	WebRtcSignallingThread->Join();
	HWEncoderDetails.Encoder->Shutdown();
}

void FStreamer::WebRtcSignallingThreadFunc()
{
	// initialisation of WebRTC stuff and things that depends on it should happen in WebRTC signalling thread

	WebRtcSignallingThreadId = GetCurrentThreadId();

	// init WebRTC networking and inter-thread communication
	rtc::EnsureWinsockInit();
	rtc::Win32SocketServer SocketServer;
	rtc::Win32Thread W32Thread(&SocketServer);
	rtc::ThreadManager::Instance()->SetCurrentThread(&W32Thread);

	rtc::InitializeSSL();

	// WebRTC assumes threads within which PeerConnectionFactory is created is the signalling thread

	PeerConnectionConfig = {};

	PeerConnectionFactory = webrtc::CreatePeerConnectionFactory(
		nullptr,
		nullptr,
		nullptr,
		new rtc::RefCountedObject<FAudioCapturer>(),
		webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>(),
		webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>(),
		std::move(VideoEncoderFactoryStrong),
		std::make_unique<webrtc::InternalDecoderFactory>(),
		nullptr,
		nullptr);
	check(PeerConnectionFactory);

	// now that everything is ready
	ConnectToSignallingServer();

	// WebRTC window messaging loop
	MSG Msg;
	BOOL Gm;
	while ((Gm = ::GetMessageW(&Msg, NULL, 0, 0)) != 0 && Gm != -1)
	{
		::TranslateMessage(&Msg);
		::DispatchMessage(&Msg);
	}

	// WebRTC stuff created in this thread should be deleted here
	DeleteAllPlayerSessions();
	PeerConnectionFactory = nullptr;

	rtc::CleanupSSL();

	UE_LOG(PixelStreamer, Log, TEXT("Exiting WebRTC WndProc thread"));
}

void FStreamer::ConnectToSignallingServer()
{
	SignallingServerConnection = MakeUnique<FSignallingServerConnection>(SignallingServerUrl, *this);
}

void FStreamer::OnFrameBufferReady(const FTexture2DRHIRef& FrameBuffer)
{
	if (bStreamingStarted)
	{
		VideoCapturer->OnFrameReady(FrameBuffer);
	}
}

void FStreamer::OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config)
{
	PeerConnectionConfig = Config;
}

void FStreamer::OnOffer(FPlayerId PlayerId, TUniquePtr<webrtc::SessionDescriptionInterface> Sdp)
{
	CreatePlayerSession(PlayerId);
	AddStreams(PlayerId);

	FPlayerSession* Player = GetPlayerSession(PlayerId);
	checkf(Player, TEXT("just created player %d not found"), PlayerId);

	Player->OnOffer(MoveTemp(Sdp));
}

void FStreamer::OnRemoteIceCandidate(FPlayerId PlayerId, TUniquePtr<webrtc::IceCandidateInterface> Candidate)
{
	FPlayerSession* Player = GetPlayerSession(PlayerId);
	checkf(Player, TEXT("player %u not found"), PlayerId);

	Player->OnRemoteIceCandidate(MoveTemp(Candidate));
}

void FStreamer::OnPlayerDisconnected(FPlayerId PlayerId)
{
	UE_LOG(PixelStreamer, Log, TEXT("player %d disconnected"), PlayerId);
	DeletePlayerSession(PlayerId);
}

void FStreamer::OnSignallingServerDisconnected()
{
	DeleteAllPlayerSessions();
	ConnectToSignallingServer();
}

FPlayerSession* FStreamer::GetPlayerSession(FPlayerId PlayerId)
{
	auto* Player = Players.Find(PlayerId);
	return Player ? Player->Get() : nullptr;
}

void FStreamer::DeleteAllPlayerSessions()
{
	while (Players.Num() > 0)
	{
		DeletePlayerSession(Players.CreateIterator().Key());
	}
}

void FStreamer::CreatePlayerSession(FPlayerId PlayerId)
{
	check(PeerConnectionFactory);

	if (bPlanB)
	{
		verifyf(!Players.Find(PlayerId), TEXT("player %u already exists"), PlayerId);
	}
	else
	{
		// With unified plan, we get several calls to OnOffer, which in turn calls
		// this several times.
		// Therefore, we only try to create the player if not created already
		if (Players.Find(PlayerId))
		{
			return;
		}
	}

	webrtc::FakeConstraints Constraints;
	Constraints.AddOptional(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp, "true");

	bool bOriginalQualityController = Players.Num() == 0; // first player controls quality by default
	TUniquePtr<FPlayerSession> Session = MakeUnique<FPlayerSession>(*this, PlayerId, bOriginalQualityController);
	rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection = PeerConnectionFactory->CreatePeerConnection(PeerConnectionConfig, webrtc::PeerConnectionDependencies{ Session.Get() });
	check(PeerConnection);
	Session->SetPeerConnection(PeerConnection);
	Players.Add(PlayerId) = MoveTemp(Session);
}

void FStreamer::DeletePlayerSession(FPlayerId PlayerId)
{
	FPlayerSession* Player = GetPlayerSession(PlayerId);
	if (!Player)
	{
		UE_LOG(PixelStreamer, VeryVerbose, TEXT("failed to delete player %d: not found"), PlayerId);
		return;
	}

	bool bWasQualityController = Player->IsQualityController();

	Players.Remove(PlayerId);
	if (Players.Num() == 0)
	{
		bStreamingStarted = false;
		if (!bPlanB)
		{
			AudioTrack = nullptr;
			VideoTrack = nullptr;
		}
		else
		{
			Streams.Empty();
		}

		// Inform the application-specific blueprint that nobody is viewing or
		// interacting with the app. This is an opportunity to reset the app.
		UPixelStreamerDelegates* Delegates = UPixelStreamerDelegates::GetPixelStreamerDelegates();
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
		OnQualityOwnership(PlayerIds[0]);
	}
}

void FStreamer::AddStreams(FPlayerId PlayerId)
{
	const FString StreamId = TEXT("stream_id");
	const char AudioLabel[] = "audio_label";
	const char VideoLabel[] = "video_label";

	FPlayerSession* Session = GetPlayerSession(PlayerId);
	check(Session);

	if (bPlanB)
	{
		rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream;

		if (auto* StreamPtr = Streams.Find(StreamId))
		{
			Stream = *StreamPtr;
		}
		else
		{
			Stream = PeerConnectionFactory->CreateLocalMediaStream(TCHAR_TO_ANSI(*StreamId));

			rtc::scoped_refptr<webrtc::AudioTrackInterface> AudioTrackLocal(
				PeerConnectionFactory->CreateAudioTrack(AudioLabel, PeerConnectionFactory->CreateAudioSource(cricket::AudioOptions{})));

			Stream->AddTrack(AudioTrackLocal);

			auto VideoCapturerStrong = std::make_unique<FVideoCapturer>(HWEncoderDetails);
			VideoCapturer = VideoCapturerStrong.get();
			rtc::scoped_refptr<webrtc::VideoTrackInterface> VideoTrackLocal(PeerConnectionFactory->CreateVideoTrack(
				VideoLabel, PeerConnectionFactory->CreateVideoSource(std::move(VideoCapturerStrong))));

			Stream->AddTrack(VideoTrackLocal);

			Streams[StreamId] = Stream;
		}

		verifyf(Session->GetPeerConnection().AddStream(Stream), TEXT("Failed to add stream for player %u"), PlayerId);
	}
	else
	{
		if (!Session->GetPeerConnection().GetSenders().empty())
		{
			return;  // Already added tracks
		}

		if (!AudioTrack)
		{
			AudioTrack =
				PeerConnectionFactory->CreateAudioTrack(AudioLabel, PeerConnectionFactory->CreateAudioSource(cricket::AudioOptions{}));
		}

		if (!VideoTrack)
		{
			auto VideoCapturerStrong = std::make_unique<FVideoCapturer>(HWEncoderDetails);
			VideoCapturer = VideoCapturerStrong.get();
			VideoTrack = PeerConnectionFactory->CreateVideoTrack(
				VideoLabel, PeerConnectionFactory->CreateVideoSource(std::move(VideoCapturerStrong)));
		}

		auto Res = Session->GetPeerConnection().AddTrack(AudioTrack, { TCHAR_TO_ANSI(*StreamId) });
		if (!Res.ok())
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to add AudioTrack to PeerConnection of player %u. Msg=%s"), Session->GetPlayerId(), ANSI_TO_TCHAR(Res.error().message()));
		}

		Res = Session->GetPeerConnection().AddTrack(VideoTrack, { TCHAR_TO_ANSI(*StreamId) });
		if (!Res.ok())
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to add VideoTrack to PeerConnection of player %u. Msg=%s"), Session->GetPlayerId(), ANSI_TO_TCHAR(Res.error().message()));
		}
	}
}

void FStreamer::OnQualityOwnership(FPlayerId PlayerId)
{
	checkf(GetPlayerSession(PlayerId), TEXT("player %d not found"), PlayerId);
	for (auto&& PlayerEntry : Players)
	{
		FPlayerSession& Player = *PlayerEntry.Value;
		Player.SetQualityController(Player.GetPlayerId() == PlayerId ? true : false);
	}
}

void FStreamer::SendPlayerMessage(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor)
{
	UE_LOG(PixelStreamer, Log, TEXT("SendPlayerMessage: %d - %s"), static_cast<int32>(Type), *Descriptor);
	for (auto&& PlayerEntry : Players)
	{
		PlayerEntry.Value->SendMessage(Type, Descriptor);
	}
}

void FStreamer::SendFreezeFrame(const TArray<uint8>& JpegBytes)
{
	UE_LOG(PixelStreamer, Log, TEXT("Sending freeze frame to players: %d bytes"), JpegBytes.Num());
	for (auto&& PlayerEntry : Players)
	{
		PlayerEntry.Value->SendFreezeFrame(JpegBytes);
	}

	CachedJpegBytes = JpegBytes;
}

void FStreamer::SendCachedFreezeFrameTo(FPlayerSession& Player)
{
	if (CachedJpegBytes.Num() > 0)
	{
		UE_LOG(PixelStreamer, Log, TEXT("Sending cached freeze frame to player %d: %d bytes"), Player.GetPlayerId(), CachedJpegBytes.Num());
		Player.SendFreezeFrame(CachedJpegBytes);
	}
}

void FStreamer::SendUnfreezeFrame()
{
	UE_LOG(PixelStreamer, Log, TEXT("Sending unfreeze message to players"));

	for (auto&& PlayerEntry : Players)
	{
		PlayerEntry.Value->SendUnfreezeFrame();
	}

	CachedJpegBytes.Empty();
}
