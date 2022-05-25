// Copyright Epic Games, Inc. All Rights Reserved.

#include "Streamer.h"
#include "IPixelStreamingModule.h"
#include "DataChannelObserver.h"
#include "PixelStreamingPrivate.h"
#include "PixelStreamingDelegates.h"
#include "PixelStreamingSourceFrame.h"
#include "PixelStreamingSignallingConnection.h"
#include "PixelStreamingAudioDeviceModule.h"
#include "WebRTCIncludes.h"
#include "VideoEncoderFactory.h"
#include "ToStringExtensions.h"
#include "AudioCapturer.h"
#include "VideoEncoderFactorySimple.h"
#include "VideoEncoderFactorySimulcast.h"
#include "Settings.h"
#include "WebRTCLogging.h"
#include "WebSocketsModule.h"
#include "VideoSourceP2P.h"
#include "VideoSourceSFU.h"
#include "VideoSourceGroup.h"
#include "InputDevice.h"
#include "Misc/CoreDelegates.h"
#include "UtilsRender.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Async/Async.h"
#include "Framework/Application/SlateApplication.h"

namespace UE::PixelStreaming
{
	FStreamer::FStreamer(const FString& InStreamerId)
		: StreamerId(InStreamerId)
	{
		FStats::Get()->AddSessions(&PlayerSessions);
		VideoSourceGroup = MakeUnique<FVideoSourceGroup>();
		FPixelStreamingSignallingConnection::FWebSocketFactory WebSocketFactory = [](const FString& Url) { return FWebSocketsModule::Get().CreateWebSocket(Url, TEXT("")); };
		SignallingServerConnection = MakeUnique<FPixelStreamingSignallingConnection>(WebSocketFactory, *this, InStreamerId);
		StartWebRtcSignallingThread();
		FCoreDelegates::OnPreExit.AddLambda([&PlayerSessions = PlayerSessions]()
		{ 
			PlayerSessions.DeleteAllPlayerSessions(true);
		});
	}

	FStreamer::~FStreamer()
	{
		FStats::Get()->RemoveSessions(&PlayerSessions);
		StopStreaming();
		P2PPeerConnectionFactory = nullptr;
		SFUPeerConnectionFactory = nullptr;
		StopWebRtcSignallingThread();
	}

	/** 
	 * IPixelStreamingStreamer implementation
	 */
	void FStreamer::SetStreamFPS(int32 InFramesPerSecond)
	{
		VideoSourceGroup->SetFPS(InFramesPerSecond);
	}

	void FStreamer::SetVideoInput(TSharedPtr<FPixelStreamingVideoInput> Input)
	{
		VideoSourceGroup->SetVideoInput(Input);
		Input->OnFrame.AddLambda([this](const FPixelStreamingSourceFrame& SourceFrame)
		{
			if(bCaptureNextBackBufferAndStream)
			{
				bCaptureNextBackBufferAndStream = false;

				// Read the data out of the back buffer and send as a JPEG.
				FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
				FIntRect Rect(0, 0, SourceFrame.FrameTexture->GetDesc().Extent.X, SourceFrame.FrameTexture->GetDesc().Extent.Y);
				TArray<FColor> Data;

				RHICmdList.ReadSurfaceData(SourceFrame.FrameTexture, Rect, Data, FReadSurfaceDataFlags());
				SendFreezeFrame(MoveTemp(Data), Rect);
			}
		});
	}

	void FStreamer::SetTargetViewport(TSharedPtr<FSceneViewport> InTargetViewport)
	{
		InputDevice->SetTargetViewport(InTargetViewport);
	}

	void FStreamer::SetSignallingServerURL(const FString& InSignallingServerURL)
	{
		CurrentSignallingServerURL = InSignallingServerURL;
	}

	void FStreamer::StartStreaming()
	{
		if(CurrentSignallingServerURL.IsEmpty())
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Attempted to start streamer (%s) but no signalling server URL has been set. Use Streamer->SetSignallingServerURL(URL)"), *StreamerId);
			return;
		}
		StopStreaming();
		VideoSourceGroup->Start();
		SignallingServerConnection->Connect(CurrentSignallingServerURL);
	}

	void FStreamer::StopStreaming()
	{
		SignallingServerConnection->Disconnect();
		DeleteAllPlayerSessions();

		VideoSourceGroup->Stop();

		if (bStreamingStarted)
		{
			OnStreamingStopped().Broadcast(this);
		}
		bStreamingStarted = false;
	}

	void FStreamer::SendPlayerMessage(Protocol::EToPlayerMsg Type, const FString& Descriptor)
	{
		PlayerSessions.ForEachSession([Type, Descriptor](TSharedPtr<IPlayerSession> PlayerSession) { PlayerSession->SendMessage(Type, Descriptor); });
	}

	void FStreamer::FreezeStream(UTexture2D* Texture)
	{
		if (Texture)
		{
			ENQUEUE_RENDER_COMMAND(ReadSurfaceCommand)
			([this, Texture](FRHICommandListImmediate& RHICmdList) {
				// A frame is supplied so immediately read its data and send as a JPEG.
				FTextureRHIRef TextureRHI = Texture->GetResource() ? Texture->GetResource()->TextureRHI : nullptr;
				if (!TextureRHI)
				{
					UE_LOG(LogPixelStreaming, Error, TEXT("Attempting freeze frame with texture %s with no texture RHI"), *Texture->GetName());
					return;
				}
				uint32 Width = TextureRHI->GetDesc().Extent.X;
				uint32 Height = TextureRHI->GetDesc().Extent.Y;

				FTextureRHIRef DestTexture = CreateRHITexture(Width, Height);

				FGPUFenceRHIRef CopyFence = GDynamicRHI->RHICreateGPUFence(*FString::Printf(TEXT("FreezeFrameFence")));

				// Copy freeze frame texture to empty texture
				CopyTexture(RHICmdList, TextureRHI, DestTexture, CopyFence);

				TArray<FColor> Data;
				FIntRect Rect(0, 0, Width, Height);
				RHICmdList.ReadSurfaceData(DestTexture, Rect, Data, FReadSurfaceDataFlags());
				SendFreezeFrame(MoveTemp(Data), Rect);
			});
		}
		else
		{
			// A frame is not supplied, so we need to capture the back buffer at
			// the next opportunity, and send as a JPEG.
			bCaptureNextBackBufferAndStream = true;
		}
	}

	void FStreamer::UnfreezeStream()
	{
		// Force a keyframe so when stream unfreezes if player has never received a h.264 frame before they can still connect.
		ForceKeyFrame();

		PlayerSessions.ForEachSession([](TSharedPtr<IPlayerSession> PlayerSession) { PlayerSession->SendUnfreezeFrame(); });

		CachedJpegBytes.Empty();
	}

	void FStreamer::SendFileData(TArray<uint8>& ByteData, FString& MimeType, FString& FileExtension)
	{
		PlayerSessions.ForEachSession([&ByteData, &MimeType, &FileExtension](TSharedPtr<IPlayerSession> PlayerSession) {
			AsyncTask(ENamedThreads::AnyHiPriThreadHiPriTask, [&ByteData, &MimeType, &FileExtension, PlayerSession]() {
				bool bSentFileData = PlayerSession->SendFileData(ByteData, MimeType, FileExtension);
				if (!bSentFileData)
				{
					UE_LOG(LogPixelStreaming, Log, TEXT("Could not send file over data channel for some reason."));
				}
			});
		});
	}

	void FStreamer::KickPlayer(FPixelStreamingPlayerId PlayerId)
	{
		SignallingServerConnection->SendDisconnectPlayer(PlayerId, TEXT("Player was kicked"));
	}

	IPixelStreamingAudioSink* FStreamer::GetPeerAudioSink(FPixelStreamingPlayerId PlayerId)
	{
		return PlayerSessions.ForSession<IPixelStreamingAudioSink*>(PlayerId, [](TSharedPtr<IPlayerSession> Session) { return Session->GetAudioSink(); });
	}

	IPixelStreamingAudioSink* FStreamer::GetUnlistenedAudioSink()
	{
		IPixelStreamingAudioSink* Result = nullptr;
		PlayerSessions.ForEachSession([&Result](TSharedPtr<IPlayerSession> Session) {
			if (!Result && Session->GetAudioSink() && !Session->GetAudioSink()->HasAudioConsumers())
			{
				Result = Session->GetAudioSink();
			}
		});
		return Result;
	}

	IPixelStreamingStreamer::FStreamingStartedEvent& FStreamer::OnStreamingStarted()
	{
		return StreamingStartedEvent;
	}

	IPixelStreamingStreamer::FStreamingStoppedEvent& FStreamer::OnStreamingStopped()
	{
		return StreamingStoppedEvent;
	}


	/** 
	 * End IPixelStreamingStreamer implementation
	 */

	/** 
	 * Own methods
	 */
	void FStreamer::ForceKeyFrame()
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

	void FStreamer::PushFrame()
	{
		VideoSourceGroup->Tick();
	}

	void FStreamer::AddPlayerConfig(TSharedRef<FJsonObject>& JsonObject)
	{
		checkf(InputDevice.IsValid(), TEXT("No Input Device available when populating Player Config"));
		JsonObject->SetBoolField(TEXT("FakingTouchEvents"), InputDevice->IsFakingTouchEvents());
		FString PixelStreamingControlScheme;
		if (Settings::GetControlScheme(PixelStreamingControlScheme))
		{
			JsonObject->SetStringField(TEXT("ControlScheme"), PixelStreamingControlScheme);
		}
		float PixelStreamingFastPan;
		if (Settings::GetFastPan(PixelStreamingFastPan))
		{
			JsonObject->SetNumberField(TEXT("FastPan"), PixelStreamingFastPan);
		}
	}
	/** 
	 * End own methods
	 */



	/** 
	 * ISignallingServerConnectionObserver implementation
	 */
	void FStreamer::OnSignallingConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config)
	{
		PeerConnectionConfig = Config;
	}

	void FStreamer::OnSignallingSessionDescription(FPixelStreamingPlayerId PlayerId, webrtc::SdpType Type, const FString& Sdp)
	{
		switch (Type)
		{
			case webrtc::SdpType::kOffer:
				OnOffer(PlayerId, Sdp);
				break;
			case webrtc::SdpType::kAnswer:
			case webrtc::SdpType::kPrAnswer:
			{
				PlayerSessions.ForSession(PlayerId, [&Sdp](TSharedPtr<IPlayerSession> Session) { Session->OnAnswer(Sdp); });
				bStreamingStarted = true;
				break;
			}
			case webrtc::SdpType::kRollback:
				UE_LOG(LogPixelStreaming, Error, TEXT("Rollback SDP is currently unsupported. SDP is: %s"), *Sdp);
				break;
		}
	}

	void FStreamer::OnSignallingRemoteIceCandidate(FPixelStreamingPlayerId PlayerId, const FString& SdpMid, int SdpMLineIndex, const FString& Sdp)
	{
		PlayerSessions.ForSession(PlayerId, [&SdpMid, SdpMLineIndex, &Sdp](TSharedPtr<IPlayerSession> Session) { Session->OnRemoteIceCandidate(SdpMid, SdpMLineIndex, Sdp); });
	}

	void FStreamer::OnSignallingPlayerConnected(FPixelStreamingPlayerId PlayerId, int Flags)
	{
		// create peer connection
		if (TSharedPtr<IPlayerSession> NewSession = CreateSession(PlayerId, Flags))
		{
			AddStreams(NewSession, Flags);

			rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection = NewSession->GetPeerConnection();
			ModifyTransceivers(PeerConnection->GetTransceivers(), Flags);

			// observer for creating offer
			FPixelStreamingCreateSessionDescriptionObserver* CreateOfferObserver = FPixelStreamingCreateSessionDescriptionObserver::Create(
				[this, PlayerId, PeerConnection](webrtc::SessionDescriptionInterface* SDP) // on SDP create success
				{
					FPixelStreamingSetSessionDescriptionObserver* SetLocalDescriptionObserver = FPixelStreamingSetSessionDescriptionObserver::Create(
						[this, PlayerId, SDP]() // on SDP set success
						{
							SignallingServerConnection->SendOffer(PlayerId, *SDP);
						},
						[](const FString& Error) // on SDP set failure
						{
							UE_LOG(LogPixelStreaming, Error, TEXT("Failed to set local description: %s"), *Error);
						});

					MungeLocalSDP(SDP->description());
					SetLocalDescription(PeerConnection, SetLocalDescriptionObserver, SDP);
				},
				[](const FString& Error) // on SDP create failure
				{
					UE_LOG(LogPixelStreaming, Error, TEXT("Failed to create offer: %s"), *Error);
				});

			PeerConnection->CreateOffer(CreateOfferObserver, {});
		}
	}

	void FStreamer::OnSignallingPlayerDisconnected(FPixelStreamingPlayerId PlayerId)
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("player %s disconnected"), *PlayerId);
		DeletePlayerSession(PlayerId);
	}

	void FStreamer::OnSignallingSFUPeerDataChannels(FPixelStreamingPlayerId SFUId, FPixelStreamingPlayerId PlayerId, int32 SendStreamId, int32 RecvStreamId)
	{
		PlayerSessions.CreateSFUDataOnlyPeer(SFUId, PlayerId, InputDevice, SendStreamId, RecvStreamId, SignallingServerConnection.Get());
	}

	void FStreamer::OnSignallingConnected()
	{
		OnStreamingStarted().Broadcast(this);
	}

	void FStreamer::OnSignallingDisconnected(int32 StatusCode, const FString& Reason, bool bWasClean)
	{
		DeleteAllPlayerSessions();

		// Call disconnect here makes sure any other websocket callbacks etc are cleared
		SignallingServerConnection->Disconnect();
		SignallingServerConnection->Connect(CurrentSignallingServerURL);
	}

	void FStreamer::OnSignallingError(const FString& ErrorMsg)
	{
		DeleteAllPlayerSessions();

		// Call disconnect here makes sure any other websocket callbacks etc are cleared
		SignallingServerConnection->Disconnect();
		SignallingServerConnection->Connect(CurrentSignallingServerURL);
	}
	/** 
	 * End ISignallingServerConnectionObserver implementation
	 */

	/** 
	 * Own methods
	 */
	void FStreamer::SendFreezeFrame(TArray<FColor> RawData, const FIntRect& Rect)
	{
		IImageWrapperModule& ImageWrapperModule = FModuleManager::GetModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
		bool bSuccess = ImageWrapper->SetRaw(RawData.GetData(), RawData.Num() * sizeof(FColor), Rect.Width(), Rect.Height(), ERGBFormat::BGRA, 8);
		if (bSuccess)
		{
			// Compress to a JPEG of the maximum possible quality.
			int32 Quality = Settings::CVarPixelStreamingFreezeFrameQuality.GetValueOnAnyThread();
			const TArray64<uint8>& JpegBytes = ImageWrapper->GetCompressed(Quality);
			PlayerSessions.ForEachSession([&JpegBytes](TSharedPtr<IPlayerSession> PlayerSession) { PlayerSession->SendFreezeFrame(JpegBytes); });
			CachedJpegBytes = JpegBytes;
		}
		else
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("JPEG image wrapper failed to accept frame data"));
		}
	}

	void FStreamer::SendCachedFreezeFrameTo(FPixelStreamingPlayerId PlayerId) const
	{
		if (CachedJpegBytes.Num() > 0)
		{
			PlayerSessions.ForSession(PlayerId, [&CachedJpegBytes = CachedJpegBytes](TSharedPtr<IPlayerSession> Session) { Session->SendFreezeFrame(CachedJpegBytes); });
		}
	}

	TSharedPtr<IPlayerSession> FStreamer::CreateSession(FPixelStreamingPlayerId PlayerId, int Flags)
	{
#if WEBRTC_VERSION == 84
		PeerConnectionConfig.enable_simulcast_stats = true;
#endif

		bool bIsSFU = (Flags & Protocol::EPlayerFlags::PSPFlag_IsSFU) != 0;

		rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PCFactory = bIsSFU ? SFUPeerConnectionFactory : P2PPeerConnectionFactory;

		TSharedPtr<IPlayerSession> NewSession = PlayerSessions.CreatePlayerSession(
			PlayerId,
			InputDevice,
			PCFactory,
			PeerConnectionConfig,
			SignallingServerConnection.Get(),
			Flags);

		return NewSession;
	}

	void FStreamer::OnOffer(FPixelStreamingPlayerId PlayerId, const FString& Sdp)
	{
		TSharedPtr<IPlayerSession> NewSession = CreateSession(PlayerId, Protocol::EPlayerFlags::PSPFlag_SupportsDataChannel);
		if (NewSession)
		{
			webrtc::SdpParseError Error;
			std::unique_ptr<webrtc::SessionDescriptionInterface> SessionDesc = webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, UE::PixelStreaming::ToString(Sdp), &Error);
			if (!SessionDesc)
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to parse offer's SDP: %s\n%s"), Error.description.c_str(), *Sdp);
				return;
			}

			SendAnswer(NewSession, TUniquePtr<webrtc::SessionDescriptionInterface>(SessionDesc.release()));
		}
		else
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to create player session, peer connection was nullptr."));
	}

	void FStreamer::ModifyTransceivers(std::vector<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> Transceivers, int Flags)
	{
		bool bTransmitUEAudio = !Settings::CVarPixelStreamingWebRTCDisableTransmitAudio.GetValueOnAnyThread();
		bool bIsSFU = (Flags & Protocol::EPlayerFlags::PSPFlag_IsSFU) != Protocol::EPlayerFlags::PSPFlag_None;
		bool bReceiveBrowserAudio = !bIsSFU && !Settings::CVarPixelStreamingWebRTCDisableReceiveAudio.GetValueOnAnyThread();

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
				Sender->SetStreams({ TCHAR_TO_UTF8(*GetAudioStreamID()) });
			}
			else if (Transceiver->media_type() == cricket::MediaType::MEDIA_TYPE_VIDEO)
			{
				Transceiver->SetDirection(webrtc::RtpTransceiverDirection::kSendOnly);
				Sender->SetStreams({ TCHAR_TO_UTF8(*GetVideoStreamID()) });

				webrtc::RtpParameters RtpParams = Sender->GetParameters();
				RtpParams.encodings = CreateRTPEncodingParams(Flags);
				Sender->SetParameters(RtpParams);
			}
		}
	}

	void FStreamer::MungeRemoteSDP(cricket::SessionDescription* RemoteDescription)
	{
		// Munge SDP of remote description to inject min, max, start bitrates
		std::vector<cricket::ContentInfo>& ContentInfos = RemoteDescription->contents();
		for (cricket::ContentInfo& Content : ContentInfos)
		{
			cricket::MediaContentDescription* MediaDescription = Content.media_description();
			if (MediaDescription->type() == cricket::MediaType::MEDIA_TYPE_VIDEO)
			{
				cricket::VideoContentDescription* VideoDescription = MediaDescription->as_video();
				std::vector<cricket::VideoCodec> CodecsCopy = VideoDescription->codecs();
				for (cricket::VideoCodec& Codec : CodecsCopy)
				{
					// Note: These params are passed as kilobits, so divide by 1000.
					Codec.SetParam(cricket::kCodecParamMinBitrate, Settings::CVarPixelStreamingWebRTCMinBitrate.GetValueOnAnyThread() / 1000);
					Codec.SetParam(cricket::kCodecParamStartBitrate, Settings::CVarPixelStreamingWebRTCStartBitrate.GetValueOnAnyThread() / 1000);
					Codec.SetParam(cricket::kCodecParamMaxBitrate, Settings::CVarPixelStreamingWebRTCMaxBitrate.GetValueOnAnyThread() / 1000);
				}
				VideoDescription->set_codecs(CodecsCopy);
			}
		}
	}

	void FStreamer::MungeLocalSDP(cricket::SessionDescription* SessionDescription)
	{
		std::vector<cricket::ContentInfo>& ContentInfos = SessionDescription->contents();
		for (cricket::ContentInfo& ContentInfo : ContentInfos)
		{
			cricket::MediaContentDescription* MediaDescription = ContentInfo.media_description();
			cricket::MediaType MediaType = MediaDescription->type();
			if (MediaType != cricket::MediaType::MEDIA_TYPE_AUDIO)
			{
				continue;
			}
			cricket::AudioContentDescription* AudioDescription = MediaDescription->as_audio();
			if (AudioDescription == nullptr)
			{
				continue;
			}
			std::vector<cricket::AudioCodec> CodecsCopy = AudioDescription->codecs();
			for (cricket::AudioCodec& Codec : CodecsCopy)
			{
				if (Codec.name == "opus")
				{
					Codec.SetParam(cricket::kCodecParamPTime, "20");
					Codec.SetParam(cricket::kCodecParamMaxPTime, "120");
					Codec.SetParam(cricket::kCodecParamMinPTime, "3");
					Codec.SetParam(cricket::kCodecParamSPropStereo, "1");
					Codec.SetParam(cricket::kCodecParamStereo, "1");
					Codec.SetParam(cricket::kCodecParamUseInbandFec, "1");
					Codec.SetParam(cricket::kCodecParamUseDtx, "0");
					Codec.SetParam(cricket::kCodecParamMaxAverageBitrate, "510000");
					Codec.SetParam(cricket::kCodecParamMaxPlaybackRate, "48000");
				}
			}
			AudioDescription->set_codecs(CodecsCopy);
		}
	}

	void FStreamer::DeletePlayerSession(FPixelStreamingPlayerId PlayerId)
	{
		PlayerSessions.ForSession(PlayerId, [this](TSharedPtr<IPlayerSession> Session) {
			VideoSourceGroup->RemoveVideoSource(Session->GetVideoSource().get());
		});

		int NumRemainingPlayers = PlayerSessions.DeletePlayerSession(PlayerId);

		if (NumRemainingPlayers == 0)
		{
			bStreamingStarted = false;
		}
	}

	void FStreamer::DeleteAllPlayerSessions()
	{
		PlayerSessions.ForEachSession([this](TSharedPtr<IPlayerSession> Session) {
			VideoSourceGroup->RemoveVideoSource(Session->GetVideoSource().get());
		});

		PlayerSessions.DeleteAllPlayerSessions();
		bStreamingStarted = false;
	}

	void FStreamer::AddStreams(TSharedPtr<IPlayerSession> Session, int Flags)
	{
		// Use PeerConnection's transceiver API to add create audio/video tracks.
		SetupVideoTrack(Session, GetVideoStreamID(), VideoTrackLabel, Flags);
		SetupAudioTrack(Session, GetAudioStreamID(), AudioTrackLabel, Flags);
	}

	void FStreamer::SetupVideoTrack(TSharedPtr<IPlayerSession> Session, const FString InVideoStreamId, const FString InVideoTrackLabel, int Flags)
	{
		bool bIsSFU = (Flags & Protocol::EPlayerFlags::PSPFlag_IsSFU) != Protocol::EPlayerFlags::PSPFlag_None;

		rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PCFactory = bIsSFU ? SFUPeerConnectionFactory : P2PPeerConnectionFactory;

		// Create a video source for this player
		rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> VideoSource;
		if (bIsSFU)
		{
			VideoSource = VideoSourceGroup->CreateSFUVideoSource();
		}
		else
		{
			FPixelStreamingPlayerId PlayerId = Session->GetPlayerId();
			VideoSource = VideoSourceGroup->CreateVideoSource([PlayerId, this]() { return PlayerSessions.IsQualityController(PlayerId); });
		}

		Session->SetVideoSource(VideoSource);

		// Create video track
		rtc::scoped_refptr<webrtc::VideoTrackInterface> VideoTrack = PCFactory->CreateVideoTrack(TCHAR_TO_UTF8(*InVideoTrackLabel), VideoSource.get());
		VideoTrack->set_enabled(true);

		// Set some content hints based on degradation prefs, WebRTC uses these internally.
		webrtc::DegradationPreference DegradationPref = Settings::GetDegradationPreference();
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

		rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection = Session->GetPeerConnection();
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
		if (!bHasVideoTransceiver)
		{
			webrtc::RtpTransceiverInit TransceiverOptions;
			TransceiverOptions.stream_ids = { TCHAR_TO_UTF8(*InVideoStreamId) };
			TransceiverOptions.direction = webrtc::RtpTransceiverDirection::kSendOnly;
			TransceiverOptions.send_encodings = CreateRTPEncodingParams(Flags);

			webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpTransceiverInterface>> Result = PeerConnection->AddTransceiver(VideoTrack, TransceiverOptions);
			checkf(Result.ok(), TEXT("Failed to add Video transceiver to PeerConnection. Msg=%s"), *FString(Result.error().message()));
		}
	}

	void FStreamer::SetupAudioTrack(TSharedPtr<IPlayerSession> Session, FString const InAudioStreamId, FString const InAudioTrackLabel, int Flags)
	{
		bool bIsSFU = (Flags & Protocol::EPlayerFlags::PSPFlag_IsSFU) != Protocol::EPlayerFlags::PSPFlag_None;

		rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PCFactory = bIsSFU ? SFUPeerConnectionFactory : P2PPeerConnectionFactory;

		bool bTransmitUEAudio = !Settings::CVarPixelStreamingWebRTCDisableTransmitAudio.GetValueOnAnyThread();

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
		if (!bTransmitUEAudio)
		{
			return;
		}

		rtc::scoped_refptr<webrtc::AudioTrackInterface> AudioTrack = PCFactory->CreateAudioTrack(TCHAR_TO_UTF8(*InAudioTrackLabel), AudioSource);

		bool bHasAudioTransceiver = false;
		rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection = Session->GetPeerConnection();
		for (auto& Transceiver : PeerConnection->GetTransceivers())
		{
			rtc::scoped_refptr<webrtc::RtpSenderInterface> Sender = Transceiver->sender();
			if (Transceiver->media_type() == cricket::MediaType::MEDIA_TYPE_AUDIO)
			{
				bHasAudioTransceiver = true;
				Sender->SetTrack(AudioTrack);
			}
		}

		if (!bHasAudioTransceiver)
		{
			// Add the track
			webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>> Result = PeerConnection->AddTrack(AudioTrack, { TCHAR_TO_UTF8(*InAudioStreamId) });

			if (!Result.ok())
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to add audio track to PeerConnection. Msg=%s"), TCHAR_TO_UTF8(Result.error().message()));
			}
		}
	}

	std::vector<webrtc::RtpEncodingParameters> FStreamer::CreateRTPEncodingParams(int Flags)
	{
		bool bIsSFU = (Flags & Protocol::EPlayerFlags::PSPFlag_IsSFU) != Protocol::EPlayerFlags::PSPFlag_None;

		std::vector<webrtc::RtpEncodingParameters> EncodingParams;

		if (bIsSFU && Settings::SimulcastParameters.Layers.Num() > 0)
		{
			using FLayer = Settings::FSimulcastParameters::FLayer;

			// encodings should be lowest res to highest
			TArray<FLayer*> SortedLayers;
			for (FLayer& Layer : Settings::SimulcastParameters.Layers)
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

				// In M84 this will crash with "Attempted to set an unimplemented parameter of RtpParameters".
				// Try re-enabling this when we upgrade WebRTC versions.
				// LayerEncoding.network_priority = webrtc::Priority::kHigh;

				LayerEncoding.max_framerate = FMath::Max(60, Settings::CVarPixelStreamingWebRTCFps.GetValueOnAnyThread());
				EncodingParams.push_back(LayerEncoding);
			}
		}
		else
		{
			webrtc::RtpEncodingParameters Encoding{};
			Encoding.rid = "base";
			Encoding.max_bitrate_bps = Settings::CVarPixelStreamingWebRTCMaxBitrate.GetValueOnAnyThread();
			Encoding.min_bitrate_bps = Settings::CVarPixelStreamingWebRTCMinBitrate.GetValueOnAnyThread();
			Encoding.max_framerate = FMath::Max(60, Settings::CVarPixelStreamingWebRTCFps.GetValueOnAnyThread());
			Encoding.scale_resolution_down_by.reset();
			Encoding.network_priority = webrtc::Priority::kHigh;
			EncodingParams.push_back(Encoding);
		}

		return EncodingParams;
	}

	void FStreamer::SendAnswer(TSharedPtr<IPlayerSession> Session, TUniquePtr<webrtc::SessionDescriptionInterface> Sdp)
	{
		// below is async execution (with error handling) of:
		//		PeerConnection.SetRemoteDescription(SDP);
		//		Answer = PeerConnection.CreateAnswer();
		//		PeerConnection.SetLocalDescription(Answer);
		//		SignallingServerConnection.SendAnswer(Answer);

		FPixelStreamingSetSessionDescriptionObserver* SetLocalDescriptionObserver = FPixelStreamingSetSessionDescriptionObserver::Create(
			[this, Session]() // on success
			{
				SignallingServerConnection->SendAnswer(Session->GetPlayerId(), *Session->GetPeerConnection()->local_description());
				bStreamingStarted = true;
			},
			[this, Session](const FString& Error) // on failure
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to set local description: %s"), *Error);
				Session->DisconnectPlayer(Error);
				PlayerSessions.DeletePlayerSession(Session->GetPlayerId());
			});

		auto OnCreateAnswerSuccess = [this, Session, SetLocalDescriptionObserver](webrtc::SessionDescriptionInterface* SDP) {
			SetLocalDescription(Session->GetPeerConnection(), SetLocalDescriptionObserver, SDP);
		};

		FPixelStreamingCreateSessionDescriptionObserver* CreateAnswerObserver = FPixelStreamingCreateSessionDescriptionObserver::Create(
			MoveTemp(OnCreateAnswerSuccess),
			[this, Session](const FString& Error) // on failure
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to create answer: %s"), *Error);
				Session->DisconnectPlayer(Error);
				PlayerSessions.DeletePlayerSession(Session->GetPlayerId());
			});

		auto OnSetRemoteDescriptionSuccess = [this, Session, CreateAnswerObserver]() {
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

			AddStreams(Session, Protocol::EPlayerFlags::PSPFlag_SupportsDataChannel);
			ModifyTransceivers(Session->GetPeerConnection()->GetTransceivers(), Protocol::EPlayerFlags::PSPFlag_SupportsDataChannel);
			Session->GetPeerConnection()->CreateAnswer(CreateAnswerObserver, AnswerOption);
		};
		FPixelStreamingSetSessionDescriptionObserver* SetRemoteDescriptionObserver = FPixelStreamingSetSessionDescriptionObserver::Create(
			MoveTemp(OnSetRemoteDescriptionSuccess),
			[this, Session](const FString& Error) // on failure
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to set remote description: %s"), *Error);
				Session->DisconnectPlayer(Error);
				PlayerSessions.DeletePlayerSession(Session->GetPlayerId());
			});

		cricket::SessionDescription* RemoteDescription = Sdp->description();
		MungeRemoteSDP(RemoteDescription);

		Session->GetPeerConnection()->SetRemoteDescription(SetRemoteDescriptionObserver, Sdp.Release());
	}

	void FStreamer::OnDataChannelOpen(FPixelStreamingPlayerId PlayerId, webrtc::DataChannelInterface* DataChannel)
	{
		if (DataChannel)
		{
			// When data channel is open, try to send cached freeze frame (if we have one)
			SendCachedFreezeFrameTo(PlayerId);
		}
	}

	void FStreamer::SetLocalDescription(webrtc::PeerConnectionInterface* PeerConnection, FPixelStreamingSetSessionDescriptionObserver* Observer, webrtc::SessionDescriptionInterface* SDP)
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
				ExistingParams.degradation_preference = Settings::GetDegradationPreference();

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

	FString FStreamer::GetAudioStreamID() const
	{
		bool bSyncVideoAndAudio = !Settings::CVarPixelStreamingWebRTCDisableAudioSync.GetValueOnAnyThread();
		return bSyncVideoAndAudio ? TEXT("pixelstreaming_av_stream_id") : TEXT("pixelstreaming_audio_stream_id");
	}

	FString FStreamer::GetVideoStreamID() const
	{
		bool bSyncVideoAndAudio = !Settings::CVarPixelStreamingWebRTCDisableAudioSync.GetValueOnAnyThread();
		return bSyncVideoAndAudio ? TEXT("pixelstreaming_av_stream_id") : TEXT("pixelstreaming_video_stream_id");
	}

	rtc::scoped_refptr<webrtc::AudioDeviceModule> FStreamer::CreateAudioDeviceModule() const
	{
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
		return AudioDeviceModule;
	}

	rtc::scoped_refptr<webrtc::AudioProcessing> FStreamer::SetupAudioProcessingModule()
	{
#if WEBRTC_VERSION == 84
		webrtc::AudioProcessing* AudioProcessingModule = webrtc::AudioProcessingBuilder().Create();
#elif WEBRTC_VERSION == 96
		rtc::scoped_refptr<webrtc::AudioProcessing> AudioProcessingModule = webrtc::AudioProcessingBuilder().Create();
#endif 
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

	void FStreamer::StartWebRtcSignallingThread()
	{
		// Initialisation of WebRTC. Note: That interfacing with WebRTC should generally happen in WebRTC signalling thread.

		// Create our own WebRTC thread for signalling
		checkf(!WebRtcSignallingThread, TEXT("Signalling thread already exists."));
		WebRtcSignallingThread = MakeUnique<rtc::Thread>(std::make_unique<rtc::PhysicalSocketServer>());
		WebRtcSignallingThread->SetName("WebRtcSignallingThread", nullptr);
		WebRtcSignallingThread->Start();

		PeerConnectionConfig = {};

		/* ---------- P2P Peer Connection Factory ---------- */

		std::unique_ptr<FVideoEncoderFactorySimple> P2PPeerConnectionFactoryPtr = std::make_unique<FVideoEncoderFactorySimple>();
		P2PVideoEncoderFactory = P2PPeerConnectionFactoryPtr.get();

		P2PPeerConnectionFactory = webrtc::CreatePeerConnectionFactory(
			nullptr,					  // network_thread
			nullptr,					  // worker_thread
			WebRtcSignallingThread.Get(), // signal_thread
			CreateAudioDeviceModule(),	  // audio device module
			webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>(),
			webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>(),
			std::move(P2PPeerConnectionFactoryPtr),
			std::make_unique<webrtc::InternalDecoderFactory>(),
			nullptr,					   // audio_mixer
			SetupAudioProcessingModule()); // audio_processing

		check(P2PPeerConnectionFactory.get() != nullptr);

		/* ---------- SFU Peer Connection Factory ---------- */

		std::unique_ptr<FVideoEncoderFactorySimulcast> SFUPeerConnectionFactoryPtr = std::make_unique<FVideoEncoderFactorySimulcast>();
		SFUVideoEncoderFactory = SFUPeerConnectionFactoryPtr.get();

		SFUPeerConnectionFactory = webrtc::CreatePeerConnectionFactory(
			nullptr,					  // network_thread
			nullptr,					  // worker_thread
			WebRtcSignallingThread.Get(), // signal_thread
			CreateAudioDeviceModule(),	  // audio device module
			webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>(),
			webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>(),
			std::move(SFUPeerConnectionFactoryPtr),
			std::make_unique<webrtc::InternalDecoderFactory>(),
			nullptr,					   // audio_mixer
			SetupAudioProcessingModule()); // audio_processing

		check(SFUPeerConnectionFactory.get() != nullptr);
	}

	void FStreamer::StopWebRtcSignallingThread()
	{
		if (WebRtcSignallingThread)
		{
			WebRtcSignallingThread->Stop();
			WebRtcSignallingThread = nullptr;
		}
	}
	/** 
	 * End own methods
	 */	
} // namespace UE::PixelStreaming
