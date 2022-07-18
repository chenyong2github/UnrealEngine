// Copyright Epic Games, Inc. All Rights Reserved.

#include "Streamer.h"
#include "IPixelStreamingModule.h"
#include "PixelStreamingPrivate.h"
#include "PixelStreamingDelegates.h"
#include "PixelStreamingInputFrameRHI.h"
#include "PixelStreamingSignallingConnection.h"
#include "PixelStreamingAudioDeviceModule.h"
#include "WebRTCIncludes.h"
#include "WebSocketsModule.h"
#include "PixelStreamingPrivate.h"
#include "PixelStreamingDataChannel.h"
#include "Settings.h"
#include "AudioSink.h"
#include "Stats.h"
#include "PixelStreamingStatNames.h"
#include "Async/Async.h"
#include "RTCStatsCollector.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Framework/Application/SlateApplication.h"
#include "UtilsRender.h"
#include "PixelStreamingCodec.h"

namespace UE::PixelStreaming
{
	TSharedPtr<FStreamer> FStreamer::Create(const FString& StreamerId)
	{
		return TSharedPtr<FStreamer>(new FStreamer(StreamerId));
	}

	FStreamer::FStreamer(const FString& InStreamerId)
		: StreamerId(InStreamerId)
	{
		VideoSourceGroup = FVideoSourceGroup::Create();
		FPixelStreamingSignallingConnection::FWebSocketFactory WebSocketFactory = [](const FString& Url) { return FWebSocketsModule::Get().CreateWebSocket(Url, TEXT("")); };
		SignallingServerConnection = MakeUnique<FPixelStreamingSignallingConnection>(WebSocketFactory, *this, InStreamerId);
	}

	void FStreamer::SetStreamFPS(int32 InFramesPerSecond)
	{
		VideoSourceGroup->SetFPS(InFramesPerSecond);
	}

	int32 FStreamer::GetStreamFPS()
	{
		return VideoSourceGroup->GetFPS();
	}

	void FStreamer::SetCoupleFramerate(bool bCouple)
	{
		VideoSourceGroup->SetCoupleFramerate(bCouple);
	}

	void FStreamer::SetVideoInput(TSharedPtr<IPixelStreamingVideoInput> Input)
	{
		VideoSourceGroup->SetVideoInput(Input);
		Input->OnFrameReady.AddLambda([this](const IPixelStreamingInputFrame& SourceFrame) {
			if (SourceFrame.GetType() == static_cast<int32>(EPixelStreamingInputFrameType::RHI))
			{
				const FPixelStreamingInputFrameRHI& RHISourceFrame = StaticCast<const FPixelStreamingInputFrameRHI&>(SourceFrame);

				if (bCaptureNextBackBufferAndStream)
				{
					bCaptureNextBackBufferAndStream = false;

					// Read the data out of the back buffer and send as a JPEG.
					FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
					FIntRect Rect(0, 0, RHISourceFrame.GetWidth(), RHISourceFrame.GetHeight());
					TArray<FColor> Data;

					RHICmdList.ReadSurfaceData(RHISourceFrame.FrameTexture, Rect, Data, FReadSurfaceDataFlags());
					SendFreezeFrame(MoveTemp(Data), Rect);
				}
			}
		});
	}

	TWeakPtr<IPixelStreamingVideoInput> FStreamer::GetVideoInput()
	{
		return VideoSourceGroup->GetVideoInput();
	}

	void FStreamer::SetTargetViewport(FSceneViewport* InTargetViewport)
	{
		InputChannel->SetTargetViewport(InTargetViewport);
	}

	FSceneViewport* FStreamer::GetTargetViewport()
	{
		return InputChannel ? InputChannel->GetTargetViewport() : nullptr;
	}

	void FStreamer::SetTargetWindow(TSharedPtr<SWindow> InTargetWindow)
	{
		InputChannel->SetTargetWindow(InTargetWindow);
	}

	TWeakPtr<SWindow> FStreamer::GetTargetWindow()
	{
		return InputChannel->GetTargetWindow();
	}

	void FStreamer::SetSignallingServerURL(const FString& InSignallingServerURL)
	{
		CurrentSignallingServerURL = InSignallingServerURL;
	}

	FString FStreamer::GetSignallingServerURL()
	{
		return CurrentSignallingServerURL;
	}

	bool FStreamer::IsSignallingConnected()
	{
		return SignallingServerConnection && SignallingServerConnection->IsConnected();
	}

	void FStreamer::StartStreaming()
	{
		if (CurrentSignallingServerURL.IsEmpty())
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Attempted to start streamer (%s) but no signalling server URL has been set. Use Streamer->SetSignallingServerURL(URL) or -PixelStreamingURL="), *StreamerId);
			return;
		}

		StopStreaming();

		if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
		{
			ConsumeStatsHandle = Delegates->OnStatChangedNative.AddSP(AsShared(), &FStreamer::ConsumeStats);
		}

		VideoSourceGroup->Start();
		SignallingServerConnection->Connect(CurrentSignallingServerURL);
		bStreamingStarted = true;
	}

	void FStreamer::StopStreaming()
	{
		if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
		{
			Delegates->OnStatChangedNative.Remove(ConsumeStatsHandle);
		}

		SignallingServerConnection->Disconnect();
		VideoSourceGroup->Stop();

		if (bStreamingStarted)
		{
			OnStreamingStopped().Broadcast(this);
		}

		DeleteAllPlayerSessions();
		bStreamingStarted = false;
	}

	IPixelStreamingStreamer::FStreamingStartedEvent& FStreamer::OnStreamingStarted()
	{
		return StreamingStartedEvent;
	}

	IPixelStreamingStreamer::FStreamingStoppedEvent& FStreamer::OnStreamingStopped()
	{
		return StreamingStoppedEvent;
	}

	void FStreamer::ForceKeyFrame()
	{
		FPixelStreamingPeerConnection::ForceVideoKeyframe();
	}

	void FStreamer::PushFrame()
	{
		VideoSourceGroup->Tick();
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

		Players.Apply([](FPixelStreamingPlayerId PlayerId, FPlayerContext& PlayerContext) {
			if (PlayerContext.DataChannel)
			{
				PlayerContext.DataChannel->SendMessage(Protocol::EToPlayerMsg::UnfreezeFrame);
			}
		});

		CachedJpegBytes.Empty();
	}

	void FStreamer::SendPlayerMessage(Protocol::EToPlayerMsg Type, const FString& Descriptor)
	{
		Players.Apply([&Type, &Descriptor](FPixelStreamingPlayerId PlayerId, FPlayerContext& PlayerContext) {
			if (PlayerContext.DataChannel)
			{
				PlayerContext.DataChannel->SendMessage(Type, Descriptor);
			}
		});
	}

	void FStreamer::SendFileData(const TArray64<uint8>& ByteData, FString& MimeType, FString& FileExtension)
	{
		// TODO this should be dispatched as an async task, but because we lock when we visit the data
		// channels it might be a bad idea. At some point it would be good to take a snapshot of the
		// keys in the map when we start, then one by one get the channel and send the data

		Players.Apply([&ByteData, &MimeType, &FileExtension](FPixelStreamingPlayerId PlayerId, FPlayerContext& PlayerContext) {
			if (PlayerContext.DataChannel)
			{
				// Send the mime type first
				PlayerContext.DataChannel->SendMessage(Protocol::EToPlayerMsg::FileMimeType, MimeType);

				// Send the extension next
				PlayerContext.DataChannel->SendMessage(Protocol::EToPlayerMsg::FileExtension, FileExtension);

				// Send the contents of the file. Note to callers: consider running this on its own thread, it can take a while if the file is big.
				if (!PlayerContext.DataChannel->SendArbitraryData(Protocol::EToPlayerMsg::FileContents, ByteData))
				{
					UE_LOG(LogPixelStreaming, Error, TEXT("Unable to send file data over the data channel for player %s."), *PlayerId);
				}
			}
		});
	}

	void FStreamer::KickPlayer(FPixelStreamingPlayerId PlayerId)
	{
		SignallingServerConnection->SendDisconnectPlayer(PlayerId, TEXT("Player was kicked"));
		// TODO Delete player session?
	}

	IPixelStreamingAudioSink* FStreamer::GetPeerAudioSink(FPixelStreamingPlayerId PlayerId)
	{
		if (FPlayerContext* PlayerContext = Players.Find(PlayerId))
		{
			return PlayerContext->PeerConnection->GetAudioSink().Get();
		}
		return nullptr;
	}

	IPixelStreamingAudioSink* FStreamer::GetUnlistenedAudioSink()
	{
		IPixelStreamingAudioSink* Result = nullptr;
		Players.ApplyUntil([&Result](FPixelStreamingPlayerId PlayerId, FPlayerContext& PlayerContext) {
			if (PlayerContext.PeerConnection)
			{
				if (!PlayerContext.PeerConnection->GetAudioSink()->HasAudioConsumers())
				{
					Result = PlayerContext.PeerConnection->GetAudioSink().Get();
					return true;
				}
			}
			return false;
		});
		return Result;
	}

	void FStreamer::AddPlayerConfig(TSharedRef<FJsonObject>& JsonObject)
	{
		checkf(InputChannel.IsValid(), TEXT("No Input Device available when populating Player Config"));
		JsonObject->SetBoolField(TEXT("FakingTouchEvents"), InputChannel->IsFakingTouchEvents());
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

	bool FStreamer::CreateSession(FPixelStreamingPlayerId PlayerId)
	{
		if (FPlayerContext* PlayerContext = Players.Find(PlayerId))
		{
			if (PlayerContext->Config.IsSFU && SFUPlayerId != INVALID_PLAYER_ID)
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("SFU is connecting but we already have an SFU"));
			}
			else
			{
				TUniquePtr<FPixelStreamingPeerConnection> NewConnection = FPixelStreamingPeerConnection::Create(PeerConnectionConfig, PlayerContext->Config.IsSFU);

				NewConnection->OnEmitIceCandidate.AddLambda([this, PlayerId](const webrtc::IceCandidateInterface* SDP) {
					SignallingServerConnection->SendIceCandidate(PlayerId, *SDP);
				});

				NewConnection->OnNewDataChannel.AddLambda([this, PlayerId](TSharedPtr<FPixelStreamingDataChannel> NewChannel) {
					AddNewDataChannel(PlayerId, NewChannel);
				});

				NewConnection->SetWebRTCStatsCallback(new rtc::RefCountedObject<FRTCStatsCollector>(PlayerId));

				PlayerContext->PeerConnection = MakeShareable(NewConnection.Release());

				if (PlayerContext->Config.IsSFU)
				{
					SFUPlayerId = PlayerId;
				}
				else
				{
					SetQualityController(PlayerId);
				}

				return true;
			}
		}
		return false;
	}

	// Streams need to be added after the remote description when we get an offer to receive.
	void FStreamer::AddStreams(FPixelStreamingPlayerId PlayerId)
	{
		if (VideoSourceGroup->GetVideoInput() != nullptr)
		{
			if (FPlayerContext* PlayerContext = Players.Find(PlayerId))
			{
				const bool AllowSimulcast = PlayerContext->Config.IsSFU;
				PlayerContext->PeerConnection->SetVideoSource(VideoSourceGroup->CreateVideoSource(AllowSimulcast, [this, PlayerId]() { return ShouldPeerGenerateFrames(PlayerId); }));
				if (!Settings::CVarPixelStreamingWebRTCDisableTransmitAudio.GetValueOnAnyThread())
				{
					PlayerContext->PeerConnection->SetAudioSource(FPixelStreamingPeerConnection::GetApplicationAudioSource());
				}

				PlayerContext->PeerConnection->SetAudioSink(MakeShared<FAudioSink>());
			}
		}
	}

	void FStreamer::OnSignallingConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config)
	{
		PeerConnectionConfig = Config;

#if WEBRTC_VERSION == 84
		PeerConnectionConfig.enable_simulcast_stats = true;
#endif
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
				OnAnswer(PlayerId, Sdp);
				break;
			}
			case webrtc::SdpType::kRollback:
				UE_LOG(LogPixelStreaming, Error, TEXT("Rollback SDP is currently unsupported. SDP is: %s"), *Sdp);
				break;
		}
	}

	void FStreamer::OnSignallingRemoteIceCandidate(FPixelStreamingPlayerId PlayerId, const FString& SdpMid, int SdpMLineIndex, const FString& Sdp)
	{
		if (FPlayerContext* PlayerContext = Players.Find(PlayerId))
		{
			PlayerContext->PeerConnection->AddRemoteIceCandidate(SdpMid, SdpMLineIndex, Sdp);
		}
	}

	void FStreamer::OnSignallingPlayerConnected(FPixelStreamingPlayerId PlayerId, const FPixelStreamingPlayerConfig& PlayerConfig)
	{
		FPlayerContext& PlayerContext = Players.GetOrAdd(PlayerId);
		PlayerContext.Config = PlayerConfig;

		// create peer connection
		if (CreateSession(PlayerId))
		{
			AddStreams(PlayerId);

			// Create a datachannel, if needed.
			if (PlayerContext.Config.SupportsDataChannel)
			{
				const bool Negotiated = PlayerContext.Config.IsSFU;
				const int Id = PlayerContext.Config.IsSFU ? 1023 : 0;
				TSharedPtr<FPixelStreamingDataChannel> NewChannel = PlayerContext.PeerConnection->CreateDataChannel(Id, Negotiated);
				AddNewDataChannel(PlayerId, NewChannel);
			}

			const FPixelStreamingPeerConnection::EReceiveMediaOption ReceiveOption = PlayerContext.Config.IsSFU
				? FPixelStreamingPeerConnection::EReceiveMediaOption::Nothing
				: FPixelStreamingPeerConnection::EReceiveMediaOption::Audio;

			PlayerContext.PeerConnection->CreateOffer(
				ReceiveOption,
				[this, PlayerId](const webrtc::SessionDescriptionInterface* SDP) {
					// on success
					SignallingServerConnection->SendOffer(PlayerId, *SDP);
				},
				[this, PlayerId](const FString& Error) {
					// on error
					DeletePlayerSession(PlayerId);
				});
		}
	}

	void FStreamer::OnSignallingPlayerDisconnected(FPixelStreamingPlayerId PlayerId)
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("player %s disconnected"), *PlayerId);
		DeletePlayerSession(PlayerId);
	}

	void FStreamer::OnSignallingSFUPeerDataChannels(FPixelStreamingPlayerId SFUId, FPixelStreamingPlayerId PlayerId, int32 SendStreamId, int32 RecvStreamId)
	{
		FPlayerContext* PlayerContext = Players.Find(SFUId);
		if (PlayerContext == nullptr)
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Trying to create data channels from SFU connection but no SFU connection found."));
			return;
		}

		TSharedPtr<FPixelStreamingDataChannel> NewChannel = FPixelStreamingDataChannel::Create(*PlayerContext->PeerConnection, SendStreamId, RecvStreamId);
		AddNewDataChannel(PlayerId, NewChannel);
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

	void FStreamer::ConsumeStats(FPixelStreamingPlayerId PlayerId, FName StatName, float StatValue)
	{
		if (StatName == PixelStreamingStatNames::MeanQPPerSecond)
		{
			if (FPlayerContext* PlayerContext = Players.Find(PlayerId))
			{
				if (PlayerContext->DataChannel)
				{
					PlayerContext->DataChannel->SendMessage(Protocol::EToPlayerMsg::VideoEncoderAvgQP, FString::FromInt((int)StatValue));
				}
			}
		}
	}

	void FStreamer::OnOffer(FPixelStreamingPlayerId PlayerId, const FString& Sdp)
	{
		if (FPlayerContext* PlayerContext = Players.Find(PlayerId))
		{
			// clear any existing connection
			PlayerContext->PeerConnection = nullptr;

			if (CreateSession(PlayerId))
			{
				auto OnGeneralFailure = [this, PlayerId](const FString& Error) {
					DeletePlayerSession(PlayerId);
				};

				PlayerContext->PeerConnection->ReceiveOffer(
					Sdp,
					[this, PlayerContext, PlayerId, OnGeneralFailure]() {
						AddStreams(PlayerId);
						PlayerContext->PeerConnection->CreateAnswer(
							FPixelStreamingPeerConnection::EReceiveMediaOption::Audio,
							[this, PlayerId](const webrtc::SessionDescriptionInterface* LocalDescription) {
								SignallingServerConnection->SendAnswer(PlayerId, *LocalDescription);
								bStreamingStarted = true;
							},
							OnGeneralFailure);
					},
					OnGeneralFailure);
			}
			else
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to create player session, peer connection was nullptr."));
			}
		}
	}

	void FStreamer::OnAnswer(FPixelStreamingPlayerId PlayerId, const FString& Sdp)
	{
		if (FPlayerContext* PlayerContext = Players.Find(PlayerId))
		{
			PlayerContext->PeerConnection->ReceiveAnswer(
				Sdp,
				[this]() {
					bStreamingStarted = true;
				},
				[this, PlayerId](const FString& Error) {
					DeletePlayerSession(PlayerId);
				});
		}
		else
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to find player id %s for incoming answer."), *PlayerId);
		}
	}

	void FStreamer::DeletePlayerSession(FPixelStreamingPlayerId PlayerId)
	{
		if (FPlayerContext* PlayerContext = Players.Find(PlayerId))
		{
			// when a sfu is connected we only get disconnect messages.
			// we dont get connect messages but we might get datachannel requests which can result
			// in players with no PeerConnection but a datachannel
			if (PlayerContext->PeerConnection)
			{
				VideoSourceGroup->RemoveVideoSource(PlayerContext->PeerConnection->GetVideoSource().get());
			}
		}
		Players.Remove(PlayerId);

		if (PlayerId == SFUPlayerId)
		{
			SFUPlayerId = INVALID_PLAYER_ID;
		}

		if (PlayerId == QualityControllingId)
		{
			SetQualityController(INVALID_PLAYER_ID);

			// find the first non sfu peer and give it quality controller status
			Players.ApplyUntil([this](FPixelStreamingPlayerId PlayerId, FPlayerContext& PlayerContext) {
				if (PlayerContext.PeerConnection)
				{
					if (PlayerId != SFUPlayerId)
					{
						SetQualityController(PlayerId);
						return true;
					}
				}
				return false;
			});
		}

		if (FStats* Stats = FStats::Get())
		{
			Stats->RemovePeerStats(PlayerId);
		}
	}

	void FStreamer::DeleteAllPlayerSessions()
	{
		VideoSourceGroup->RemoveAllVideoSources();
		Players.Clear();
		SFUPlayerId = INVALID_PLAYER_ID;
		QualityControllingId = INVALID_PLAYER_ID;
		InputControllingId = INVALID_PLAYER_ID;
	}

	void FStreamer::AddNewDataChannel(FPixelStreamingPlayerId PlayerId, TSharedPtr<FPixelStreamingDataChannel> NewChannel)
	{
		FPlayerContext& PlayerContext = Players.GetOrAdd(PlayerId);
		PlayerContext.DataChannel = NewChannel;

		TWeakPtr<FStreamer> WeakStreamer = AsShared();
		PlayerContext.DataChannel->OnOpen.AddLambda([WeakStreamer, PlayerId](FPixelStreamingDataChannel& Channel) {
			if (TSharedPtr<FStreamer> Streamer = WeakStreamer.Pin())
			{
				Streamer->OnDataChannelOpen(PlayerId);
			}
		});

		PlayerContext.DataChannel->OnClosed.AddLambda([WeakStreamer, PlayerId](FPixelStreamingDataChannel& Channel) {
			if (TSharedPtr<FStreamer> Streamer = WeakStreamer.Pin())
			{
				Streamer->OnDataChannelClosed(PlayerId);
			}
		});

		PlayerContext.DataChannel->OnMessageReceived.AddLambda([WeakStreamer, PlayerId](uint8 Type, const webrtc::DataBuffer& RawBuffer) {
			if (TSharedPtr<FStreamer> Streamer = WeakStreamer.Pin())
			{
				
				Streamer->OnDataChannelMessage(PlayerId, Type, RawBuffer);
			}
		});
	}

	void FStreamer::OnDataChannelOpen(FPixelStreamingPlayerId PlayerId)
	{
		// Only time we automatically make a new peer the input controlling host is if they are the first peer (and not the SFU).
		bool HostControlsInput = Settings::GetInputControllerMode() == Settings::EInputControllerMode::Host;
		if (HostControlsInput && PlayerId != SFUPlayerId && InputControllingId == INVALID_PLAYER_ID)
		{
			InputControllingId = PlayerId;
		}

		// When data channel is open, try to send cached freeze frame (if we have one)
		SendCachedFreezeFrameTo(PlayerId);
		SendInitialSettings(PlayerId);
		SendPeerControllerMessages(PlayerId);
	}

	void FStreamer::OnDataChannelClosed(FPixelStreamingPlayerId PlayerId)
	{
		if (FPlayerContext* PlayerContext = Players.Find(PlayerId))
		{
			PlayerContext->DataChannel = nullptr;

			if (InputControllingId == PlayerId)
			{
				InputControllingId = INVALID_PLAYER_ID;
				// just get the first channel we have and give it input control.
				Players.ApplyUntil([this](FPixelStreamingPlayerId PlayerId, FPlayerContext& PlayerContext) {
					if (PlayerContext.DataChannel)
					{
						if (PlayerId != SFUPlayerId)
						{
							InputControllingId = PlayerId;
							return true;
						}
					}
					return false;
				});
			}
		}
	}

	void FStreamer::OnDataChannelMessage(FPixelStreamingPlayerId PlayerId, uint8 Type, const webrtc::DataBuffer& RawBuffer)
	{
		Protocol::EToStreamerMsg MsgType = static_cast<Protocol::EToStreamerMsg>(Type);

		if (MsgType == Protocol::EToStreamerMsg::RequestQualityControl)
		{
			UE_LOG(LogPixelStreaming, Log, TEXT("Player %s has requested quality control through the data channel."), *PlayerId);
			SetQualityController(PlayerId);
		}
		else if (MsgType == Protocol::EToStreamerMsg::LatencyTest)
		{
			SendLatencyReport(PlayerId);
		}
		else if (MsgType == Protocol::EToStreamerMsg::RequestInitialSettings)
		{
			SendInitialSettings(PlayerId);
		}
		else if (MsgType == Protocol::EToStreamerMsg::TestEcho)
		{
			if (FPlayerContext* PlayerContext = Players.Find(PlayerId))
			{
				if (PlayerContext->DataChannel)
				{
					const size_t DescriptorSize = (RawBuffer.data.size() - 1) / sizeof(TCHAR);
					const TCHAR* DescPtr = reinterpret_cast<const TCHAR*>(RawBuffer.data.data() + 1);
					const FString Message(DescriptorSize, DescPtr);
					PlayerContext->DataChannel->SendMessage(Protocol::EToPlayerMsg::TestEcho, Message);
				}
			}
		}
		else if (!IsEngineExitRequested())
		{
			if (Settings::GetInputControllerMode() == Settings::EInputControllerMode::Host)
			{
				// If we are in "Host" mode and the current peer is not the host, then discard this input.
				if (InputControllingId != PlayerId)
				{
					return;
				}
			}

			TArray<uint8> MessageData(RawBuffer.data.data(), RawBuffer.data.size());
			OnInputReceived.Broadcast(PlayerId, Type, MessageData);

			if (InputChannel)
			{
				InputChannel->OnMessage(RawBuffer);
			}
		}
	}

	void FStreamer::SendInitialSettings(FPixelStreamingPlayerId PlayerId) const
	{
		const FString PixelStreamingPayload = FString::Printf(TEXT("{ \"AllowPixelStreamingCommands\": %s, \"DisableLatencyTest\": %s }"),
			Settings::CVarPixelStreamingAllowConsoleCommands.GetValueOnAnyThread() ? TEXT("true") : TEXT("false"),
			Settings::CVarPixelStreamingDisableLatencyTester.GetValueOnAnyThread() ? TEXT("true") : TEXT("false"));

		const FString WebRTCPayload = FString::Printf(TEXT("{ \"DegradationPref\": \"%s\", \"FPS\": %d, \"MinBitrate\": %d, \"MaxBitrate\": %d, \"LowQP\": %d, \"HighQP\": %d }"),
			*Settings::CVarPixelStreamingDegradationPreference.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingWebRTCFps.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingWebRTCMinBitrate.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingWebRTCMaxBitrate.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingWebRTCLowQpThreshold.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingWebRTCHighQpThreshold.GetValueOnAnyThread());

		const FString EncoderPayload = FString::Printf(TEXT("{ \"TargetBitrate\": %d, \"MaxBitrate\": %d, \"MinQP\": %d, \"MaxQP\": %d, \"RateControl\": \"%s\", \"FillerData\": %d, \"MultiPass\": \"%s\" }"),
			Settings::CVarPixelStreamingEncoderTargetBitrate.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingEncoderMaxBitrate.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingEncoderMinQP.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingEncoderMaxQP.GetValueOnAnyThread(),
			*Settings::CVarPixelStreamingEncoderRateControl.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingEnableFillerData.GetValueOnAnyThread() ? 1 : 0,
			*Settings::CVarPixelStreamingEncoderMultipass.GetValueOnAnyThread());

		const FString FullPayload = FString::Printf(TEXT("{ \"PixelStreaming\": %s, \"Encoder\": %s, \"WebRTC\": %s }"), *PixelStreamingPayload, *EncoderPayload, *WebRTCPayload);

		if (const FPlayerContext* PlayerContext = Players.Find(PlayerId))
		{
			if (PlayerContext->DataChannel)
			{
				if (!PlayerContext->DataChannel->SendMessage(Protocol::EToPlayerMsg::InitialSettings, FullPayload))
				{
					UE_LOG(LogPixelStreaming, Log, TEXT("Failed to send initial Pixel Streaming settings to player %s."), *PlayerId);
				}
			}
		}
	}

	void FStreamer::SendPeerControllerMessages(FPixelStreamingPlayerId PlayerId) const
	{
		if (const FPlayerContext* PlayerContext = Players.Find(PlayerId))
		{
			if (PlayerContext->DataChannel)
			{
				const uint8 ControlsInput = (Settings::GetInputControllerMode() == Settings::EInputControllerMode::Host) ? (PlayerId == InputControllingId) : 1;
				const uint8 ControlsQuality = PlayerId == QualityControllingId ? 1 : 0;
				PlayerContext->DataChannel->SendMessage(Protocol::EToPlayerMsg::InputControlOwnership, ControlsInput);
				PlayerContext->DataChannel->SendMessage(Protocol::EToPlayerMsg::QualityControlOwnership, ControlsQuality);
			}
		}
	}

	void FStreamer::SendLatencyReport(FPixelStreamingPlayerId PlayerId) const
	{
		if (Settings::CVarPixelStreamingDisableLatencyTester.GetValueOnAnyThread())
		{
			return;
		}

		double ReceiptTimeMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64());

		AsyncTask(ENamedThreads::GameThread, [this, PlayerId, ReceiptTimeMs]() {
			FString ReportToTransmitJSON;

			if (!Settings::CVarPixelStreamingWebRTCDisableStats.GetValueOnAnyThread())
			{
				double EncodeMs = -1.0;
				double CaptureToSendMs = 0.0;

				FStats* Stats = FStats::Get();
				if (Stats)
				{
					// bool QueryPeerStat(FPixelStreamingPlayerId PlayerId, FName StatToQuery, double& OutStatValue)
					Stats->QueryPeerStat(PlayerId, PixelStreamingStatNames::MeanEncodeTime, EncodeMs);
					Stats->QueryPeerStat(PlayerId, PixelStreamingStatNames::MeanSendDelay, CaptureToSendMs);
				}

				double TransmissionTimeMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64());
				ReportToTransmitJSON = FString::Printf(
					TEXT("{ \"ReceiptTimeMs\": %.2f, \"EncodeMs\": %.2f, \"CaptureToSendMs\": %.2f, \"TransmissionTimeMs\": %.2f }"),
					ReceiptTimeMs,
					EncodeMs,
					CaptureToSendMs,
					TransmissionTimeMs);
			}
			else
			{
				double TransmissionTimeMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64());
				ReportToTransmitJSON = FString::Printf(
					TEXT("{ \"ReceiptTimeMs\": %.2f, \"EncodeMs\": \"Pixel Streaming stats are disabled\", \"CaptureToSendMs\": \"Pixel Streaming stats are disabled\", \"TransmissionTimeMs\": %.2f }"),
					ReceiptTimeMs,
					TransmissionTimeMs);
			}

			if (const FPlayerContext* PlayerContext = Players.Find(PlayerId))
			{
				if (PlayerContext->DataChannel)
				{
					PlayerContext->DataChannel->SendMessage(Protocol::EToPlayerMsg::LatencyTest, ReportToTransmitJSON);
				}
			}
		});
	}

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
			Players.Apply([&JpegBytes](FPixelStreamingPlayerId PlayerId, FPlayerContext& PlayerContext) {
				if (PlayerContext.DataChannel)
				{
					PlayerContext.DataChannel->SendArbitraryData(Protocol::EToPlayerMsg::FreezeFrame, JpegBytes);
				}
			});
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
			if (const FPlayerContext* PlayerContext = Players.Find(PlayerId))
			{
				if (PlayerContext->DataChannel)
				{
					PlayerContext->DataChannel->SendArbitraryData(Protocol::EToPlayerMsg::FreezeFrame, CachedJpegBytes);
				}
			}
		}
	}

	bool FStreamer::ShouldPeerGenerateFrames(FPixelStreamingPlayerId PlayerId) const
	{
		EPixelStreamingCodec Codec = Settings::GetSelectedCodec();
		switch(Codec)
		{
			case EPixelStreamingCodec::H264:
				return PlayerId != INVALID_PLAYER_ID && (PlayerId == QualityControllingId || PlayerId == SFUPlayerId);
			break;
			case EPixelStreamingCodec::VP8:
			case EPixelStreamingCodec::VP9:
				return PlayerId != INVALID_PLAYER_ID;
			break;
			default:
				// There should be a case for every Codec type, so this should never happen.
				checkNoEntry();
				break;
		}
		return false;
	}

	void FStreamer::SetQualityController(FPixelStreamingPlayerId PlayerId)
	{
		QualityControllingId = PlayerId;
		Players.Apply([this](FPixelStreamingPlayerId DataPlayerId, FPlayerContext& PlayerContext) {
			if (PlayerContext.DataChannel)
			{
				const uint8 IsController = DataPlayerId == QualityControllingId ? 1 : 0;
				PlayerContext.DataChannel->SendMessage(Protocol::EToPlayerMsg::QualityControlOwnership, IsController);
			}
		});
	}
} // namespace UE::PixelStreaming
