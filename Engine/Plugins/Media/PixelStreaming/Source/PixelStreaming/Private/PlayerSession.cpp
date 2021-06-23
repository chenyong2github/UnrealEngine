// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerSession.h"
#include "Streamer.h"
#include "InputDevice.h"
#include "IPixelStreamingModule.h"
#include "VideoEncoder.h"
#include "VideoEncoderFactory.h"
#include "WebRtcObservers.h"
#include "Modules/ModuleManager.h"
#include "PixelStreamingEncoderFactory.h"
#include "PixelStreamingVideoEncoder.h"
#include "PixelStreamingSettings.h"
#include "LatencyTester.h"
#include <chrono>

FPlayerSession::FPlayerSession(FStreamer& InStreamer, FPlayerId InPlayerId, bool bInOriginalQualityController)
    : Streamer(InStreamer)
    , PlayerId(InPlayerId)
	, bOriginalQualityController(bInOriginalQualityController)
	, InputDevice(FModuleManager::Get().GetModuleChecked<IPixelStreamingModule>("PixelStreaming").GetInputDevice())
{
	UE_LOG(PixelStreamer, Log, TEXT("%s: PlayerId=%s, quality controller: %d"), TEXT("FPlayerSession::FPlayerSession"), *PlayerId, bOriginalQualityController);
}

FPlayerSession::~FPlayerSession()
{
	UE_LOG(PixelStreamer, Log, TEXT("%s: PlayerId=%s"), TEXT("FPlayerSession::~FPlayerSession"), *PlayerId);
	if (DataChannel)
		DataChannel->UnregisterObserver();
}

void FPlayerSession::SetVideoEncoder(FPixelStreamingVideoEncoder* InVideoEncoder)
{
	VideoEncoder = InVideoEncoder;
}

webrtc::PeerConnectionInterface& FPlayerSession::GetPeerConnection()
{
	check(PeerConnection);
	return *PeerConnection;
}

void FPlayerSession::SetPeerConnection(const rtc::scoped_refptr<webrtc::PeerConnectionInterface>& InPeerConnection)
{
	PeerConnection = InPeerConnection;
}

FPlayerId FPlayerSession::GetPlayerId() const
{
	return PlayerId;
}

void FPlayerSession::OnOffer(TUniquePtr<webrtc::SessionDescriptionInterface> SDP)
{
	// below is async execution (with error handling) of:
	//		PeerConnection.SetRemoteDescription(SDP);
	//		Answer = PeerConnection.CreateAnswer();
	//		PeerConnection.SetLocalDescription(Answer);
	//		SignallingServerConnection.SendAnswer(Answer);

	FSetSessionDescriptionObserver* SetLocalDescriptionObserver = FSetSessionDescriptionObserver::Create
	(
		[this]() // on success
		{
			Streamer.GetSignallingServerConnection()->SendAnswer(PlayerId, *PeerConnection->local_description());
			Streamer.SetStreamingStarted(true);
		},
		[this](const FString& Error) // on failure
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to set local description: %s"), *Error);
			DisconnectPlayer(Error);
		}
	);

	auto OnCreateAnswerSuccess = [this, SetLocalDescriptionObserver](webrtc::SessionDescriptionInterface* SDP)
	{
		// #REFACTOR : With WebRTC branch-heads/66, the sink of video capturer will be added as a direct result
		// of `PeerConnection->SetLocalDescription()` call but video encoder will be created later on
		// the first frame pushed into the pipeline (by capturer).
		// We need to associate this `FPlayerSession` instance with the right instance of `FVideoEncoder` for quality
		// control, the problem is that `FVideoEncoder` is created asynchronously on demand and there's no
		// clean way to give it the right instance of `FPlayerSession`.
		// The plan is to assume that encoder instances are created in the same order as we call
		// `PeerConnection->SetLocalDescription()`, as these calls are done from the same thread and internally
		// WebRTC uses `std::vector` for capturer's sinks and then iterates over it to create encoder instances,
		// and there's no obvious reason why it can be replaced by an unordered container in the future.
		// So before adding a new sink to the capturer (`PeerConnection->SetLocalDescription()`) we push
		// this `FPlayerSession` into encoder factory queue and pop it out of the queue when encoder instance
		// is created. Unfortunately I (Andriy) don't see a way to put `check`s to verify it works correctly.
		
		Streamer.GetVideoEncoderFactory()->AddSession(*this);

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
		[this](const FString& Error) // on failure
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to create answer: %s"), *Error);
			DisconnectPlayer(Error);
		}
	);

	auto OnSetRemoteDescriptionSuccess = [this, CreateAnswerObserver, OnCreateAnswerSuccess = MoveTemp(OnCreateAnswerSuccess)]()
	{
		int offer_to_receive_video = 0;
		int offer_to_receive_audio = 0; // ToDo: Make CVar to receive browser audio.
		bool voice_activity_detection = true;
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
	};

	FSetSessionDescriptionObserver* SetRemoteDescriptionObserver = FSetSessionDescriptionObserver::Create(
		MoveTemp(OnSetRemoteDescriptionSuccess),
		[this](const FString& Error) // on failure
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to set remote description: %s"), *Error);
			DisconnectPlayer(Error);
		}
	);

	PeerConnection->SetRemoteDescription(SetRemoteDescriptionObserver, SDP.Release());
}

void FPlayerSession::OnRemoteIceCandidate(TUniquePtr<webrtc::IceCandidateInterface> Candidate)
{
	if (!PeerConnection->AddIceCandidate(Candidate.Get()))
	{
		UE_LOG(PixelStreamer, Error, TEXT("Failed to apply remote ICE Candidate from Player %s"), *PlayerId);
		DisconnectPlayer(TEXT("Failed to apply remote ICE Candidate"));
		return;
	}
}

void FPlayerSession::DisconnectPlayer(const FString& Reason)
{
	if (bDisconnecting)
		return; // already notified SignallingServer to disconnect this player

	bDisconnecting = true;
	Streamer.GetSignallingServerConnection()->SendDisconnectPlayer(PlayerId, Reason);
}

bool FPlayerSession::IsOriginalQualityController() const
{
	return bOriginalQualityController;
}

bool FPlayerSession::IsQualityController() const
{
	return VideoEncoder != nullptr && VideoEncoder->IsQualityController();
}

void FPlayerSession::SetQualityController(bool bControlsQuality)
{
	if (!VideoEncoder || !DataChannel)
	{
		return;
	}

	VideoEncoder->SetQualityController(bControlsQuality);
	SendQualityControlStatus();
}

void FPlayerSession::SendKeyFrame()
{
	if (IsQualityController())
	{
		VideoEncoder->ForceKeyFrame();
	}
}

bool FPlayerSession::SendMessage(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const
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

void FPlayerSession::SendQualityControlStatus() const
{
	if (!DataChannel)
	{
		return;
	}

	const uint8 MessageType = static_cast<uint8>(PixelStreamingProtocol::EToPlayerMsg::QualityControlOwnership);
	const uint8 ControlsQuality = VideoEncoder->IsQualityController() ? 1 : 0;

	rtc::CopyOnWriteBuffer Buffer(sizeof(MessageType) + sizeof(ControlsQuality));

	size_t Pos = 0;
	Pos = SerializeToBuffer(Buffer, Pos, &MessageType, sizeof(MessageType));
	Pos = SerializeToBuffer(Buffer, Pos, &ControlsQuality, sizeof(ControlsQuality));

	if (!DataChannel->Send(webrtc::DataBuffer(Buffer, true)))
	{
		UE_LOG(PixelStreamer, Error, TEXT("failed to send quality control status"));
	}
}

void FPlayerSession::SendFreezeFrame(const TArray64<uint8>& JpegBytes) const
{
	if (!DataChannel)
	{
		return;
	}

	// just a sanity check. WebRTC buffer size is 16MB, which translates to 3840Mbps for 30fps video. It's not expected
	// that freeze frame size will come close to this limit and it's not expected we'll send freeze frames too often.
	// if this fails check that you send compressed image or that you don't do this too often (like 30fps or more)
	if (DataChannel->buffered_amount() + JpegBytes.Num() >= 16 * 1024 * 1024)
	{
		UE_LOG(PixelStreamer, Error, TEXT("Freeze frame too big: image size %d, buffered amount %d"), JpegBytes.Num(), DataChannel->buffered_amount());
		return;
	}

	const uint8 MessageType = static_cast<uint8>(PixelStreamingProtocol::EToPlayerMsg::FreezeFrame);
	const int32 JpegSize = JpegBytes.Num();
	rtc::CopyOnWriteBuffer Buffer(sizeof(MessageType) + sizeof(JpegSize) + JpegSize);

	size_t Pos = 0;
	Pos = SerializeToBuffer(Buffer, Pos, &MessageType, sizeof(MessageType));
	Pos = SerializeToBuffer(Buffer, Pos, &JpegSize, sizeof(JpegSize));
	Pos = SerializeToBuffer(Buffer, Pos, JpegBytes.GetData(), JpegSize);

	if (!DataChannel->Send(webrtc::DataBuffer(Buffer, true)))
	{
		UE_LOG(PixelStreamer, Error, TEXT("failed to send freeze frame"));
	}
}

void FPlayerSession::SendUnfreezeFrame() const
{
	if (!DataChannel)
	{
		return;
	}

	const uint8 MessageType = static_cast<uint8>(PixelStreamingProtocol::EToPlayerMsg::UnfreezeFrame);

	rtc::CopyOnWriteBuffer Buffer(sizeof(MessageType));

	size_t Pos = 0;
	Pos = SerializeToBuffer(Buffer, Pos, &MessageType, sizeof(MessageType));

	if (!DataChannel->Send(webrtc::DataBuffer(Buffer, true)))
	{
		UE_LOG(PixelStreamer, Error, TEXT("failed to send unfreeze frame"));
	}
}

void FPlayerSession::SendInitialSettings() const
{
	if (!DataChannel)
	{
		return;
	}

	const FString WebRTCPayload = FString::Printf(TEXT("{ \"DegradationPref\": \"%s\", \"MaxFPS\": %d, \"MinBitrate\": %d, \"MaxBitrate\": %d, \"LowQP\": %d, \"HighQP\": %d }"),
		*PixelStreamingSettings::CVarPixelStreamingDegradationPreference.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingWebRTCMaxFps.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingWebRTCMinBitrate.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingWebRTCMaxBitrate.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingWebRTCLowQpThreshold.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingWebRTCHighQpThreshold.GetValueOnAnyThread());

	const FString EncoderPayload = FString::Printf(TEXT("{ \"TargetBitrate\": %d, \"MaxBitrate\": %d, \"MinQP\": %d, \"MaxQP\": %d, \"RateControl\": \"%s\", \"FillerData\": %d, \"MultiPass\": \"%s\" }"),
		PixelStreamingSettings::CVarPixelStreamingEncoderTargetBitrate.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingEncoderMaxBitrate.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingEncoderMinQP.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingEncoderMaxQP.GetValueOnAnyThread(),
		*PixelStreamingSettings::CVarPixelStreamingEncoderRateControl.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingEnableFillerData.GetValueOnAnyThread() ? 1 : 0,
		*PixelStreamingSettings::CVarPixelStreamingEncoderMultipass.GetValueOnAnyThread());

	const FString FullPayload = FString::Printf(TEXT("{ \"Encoder\": %s, \"WebRTC\": %s }"), *EncoderPayload, *WebRTCPayload);
	if (!SendMessage(PixelStreamingProtocol::EToPlayerMsg::InitialSettings, FullPayload))
	{
		UE_LOG(PixelStreamer, Error, TEXT("failed to send initial settings"));
	}
}

//
// webrtc::PeerConnectionObserver implementation.
//

void FPlayerSession::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState NewState)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s, NewState=%s"), TEXT("FPlayerSession::OnSignalingChange"), *PlayerId, ToString(NewState));
}

// Called when a remote stream is added
void FPlayerSession::OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s, Stream=%s"), TEXT("FPlayerSession::OnAddStream"), *PlayerId, *ToString(Stream->id()));
}

void FPlayerSession::OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s, Stream=%s"), TEXT("FPlayerSession::OnRemoveStream"), *PlayerId, *ToString(Stream->id()));
}

void FPlayerSession::OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> InDataChannel)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s"), TEXT("FPlayerSession::OnDataChannel"), *PlayerId);
	DataChannel = InDataChannel;
	DataChannel->RegisterObserver(this);
}

void FPlayerSession::OnRenegotiationNeeded()
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s"), TEXT("FPlayerSession::OnRenegotiationNeeded"), *PlayerId);
}

void FPlayerSession::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState NewState)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s, NewState=%s"), TEXT("FPlayerSession::OnIceConnectionChange"), *PlayerId, ToString(NewState));
}

void FPlayerSession::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState NewState)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s, NewState=%s"), TEXT("FPlayerSession::OnIceGatheringChange"), *PlayerId, ToString(NewState));
}

void FPlayerSession::OnIceCandidate(const webrtc::IceCandidateInterface* Candidate)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s"), TEXT("FPlayerSession::OnIceCandidate"), *PlayerId);

	Streamer.GetSignallingServerConnection()->SendIceCandidate(PlayerId, *Candidate);
}

void FPlayerSession::OnIceCandidatesRemoved(const std::vector<cricket::Candidate>& candidates)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s"), TEXT("FPlayerSession::OnIceCandidatesRemoved"), *PlayerId);
}

void FPlayerSession::OnIceConnectionReceivingChange(bool Receiving)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s, Receiving=%d"), TEXT("FPlayerSession::OnIceConnectionReceivingChange"), *PlayerId, *reinterpret_cast<int8*>(&Receiving));
}

void FPlayerSession::OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s"), TEXT("FPlayerSession::OnTrack"), *PlayerId);
	
	// print out track type
	auto mediaType = transceiver->media_type();
	switch(mediaType)
	{
		case cricket::MediaType::MEDIA_TYPE_AUDIO:
			UE_LOG(PixelStreamer, Log, TEXT("Track was type: audio"));
			break;
		case cricket::MediaType::MEDIA_TYPE_VIDEO:
			UE_LOG(PixelStreamer, Log, TEXT("Track was type: video"));
			break;
		case cricket::MediaType::MEDIA_TYPE_DATA:
			UE_LOG(PixelStreamer, Log, TEXT("Track was type: data"));
			break;
		default:
			UE_LOG(PixelStreamer, Log, TEXT("Track was an unsupported type"));
			break;
  
	}

	// print out track direction
	webrtc::RtpTransceiverDirection direction = transceiver->direction();
	switch(direction)
	{
		case webrtc::RtpTransceiverDirection::kSendRecv:
			UE_LOG(PixelStreamer, Log, TEXT("Track direction: send+recv"));
			break;
		case webrtc::RtpTransceiverDirection::kSendOnly:
			UE_LOG(PixelStreamer, Log, TEXT("Track direction: send only"));
			break;
		case webrtc::RtpTransceiverDirection::kRecvOnly:
			UE_LOG(PixelStreamer, Log, TEXT("Track direction: recv only"));
			break;
		case webrtc::RtpTransceiverDirection::kInactive:
			UE_LOG(PixelStreamer, Log, TEXT("Track direction: inactive"));
			break;
		case webrtc::RtpTransceiverDirection::kStopped:
			UE_LOG(PixelStreamer, Log, TEXT("Track direction: stopped"));
			break;
	}

}

void FPlayerSession::OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s"), TEXT("FPlayerSession::OnRemoveTrack"), *PlayerId);
}

//
// webrtc::DataChannelObserver implementation.
//

void FPlayerSession::OnStateChange()
{
	webrtc::DataChannelInterface::DataState State = this->DataChannel->state();
	if (State == webrtc::DataChannelInterface::DataState::kOpen)
	{
		// Once the data channel is opened, we check to see if we have any
		// freeze frame chunks cached. If so then we send them immediately
		// to the browser for display, without waiting for the video.
		Streamer.SendCachedFreezeFrameTo(*this);
	}
}

void FPlayerSession::OnBufferedAmountChange(uint64_t PreviousAmount)
{
	UE_LOG(PixelStreamer, VeryVerbose, TEXT("player %s: OnBufferedAmountChanged: prev %d, cur %d"), *PlayerId, PreviousAmount, DataChannel->buffered_amount());
}

void FPlayerSession::OnMessage(const webrtc::DataBuffer& Buffer)
{
	const uint8* Data = Buffer.data.data();
	uint32 Size = static_cast<uint32>(Buffer.data.size());
	PixelStreamingProtocol::EToStreamerMsg MsgType = static_cast<PixelStreamingProtocol::EToStreamerMsg>(Data[0]);

	if (MsgType == PixelStreamingProtocol::EToStreamerMsg::RequestQualityControl)
	{
		check(Size == 1);
		Streamer.OnQualityOwnership(PlayerId);
	}
	else if(MsgType == PixelStreamingProtocol::EToStreamerMsg::LatencyTest)
	{
		// Have to parse even though we already have this so that pointer arithmetic lines up when we parse the rest of the data
		MsgType = PixelStreamingProtocol::ParseBuffer<PixelStreamingProtocol::EToStreamerMsg>(Data, Size);
		FString TestStartTimeInBrowserMs = PixelStreamingProtocol::ParseString(Data, Size);
		FLatencyTester::Start();
		FLatencyTester::RecordReceiptTime();
		unsigned long long NowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		UE_LOG(PixelStreamer, Log, TEXT("Browser start time: %s | UE start time: %llu"), *TestStartTimeInBrowserMs, NowMs);
	}
	else if (MsgType == PixelStreamingProtocol::EToStreamerMsg::RequestInitialSettings)
	{
		SendInitialSettings();
	}
	else if (!IsEngineExitRequested())
	{
		InputDevice.OnMessage(Data, Size);
	}
}

size_t FPlayerSession::SerializeToBuffer(rtc::CopyOnWriteBuffer& Buffer, size_t Pos, const void* Data, size_t DataSize) const
{
	FMemory::Memcpy(&Buffer[Pos], reinterpret_cast<const uint8_t*>(Data), DataSize);
	return Pos + DataSize;
}
