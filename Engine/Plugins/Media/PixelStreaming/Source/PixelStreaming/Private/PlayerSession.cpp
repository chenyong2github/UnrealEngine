// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PlayerSession.h"
#include "Streamer.h"
#include "InputDevice.h"
#include "IPixelStreamingModule.h"
#include "Codecs/VideoEncoder.h"
#include "WebRtcObservers.h"

#include "Modules/ModuleManager.h"

FPlayerSession::FPlayerSession(FStreamer& InStreamer, FPlayerId InPlayerId, bool bInOriginalQualityController)
    : Streamer(InStreamer)
    , PlayerId(InPlayerId)
	, bOriginalQualityController(bInOriginalQualityController)
	, InputDevice(FModuleManager::Get().GetModuleChecked<IPixelStreamingModule>("PixelStreaming").GetInputDevice())
{
	UE_LOG(PixelStreamer, Log, TEXT("%s: PlayerId=%u, quality controller: %d"), TEXT(__FUNCTION__), PlayerId, bOriginalQualityController);
}

FPlayerSession::~FPlayerSession()
{
	UE_LOG(PixelStreamer, Log, TEXT("%s: PlayerId=%u"), TEXT(__FUNCTION__), PlayerId);
	if (DataChannel)
		DataChannel->UnregisterObserver();
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
			Streamer.SignallingServerConnection->SendAnswer(PlayerId, *PeerConnection->local_description());
			Streamer.bStreamingStarted = true;
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
		Streamer.VideoEncoderFactory->AddSession(*this);

		PeerConnection->SetLocalDescription(SetLocalDescriptionObserver, SDP);
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
		webrtc::PeerConnectionInterface::RTCOfferAnswerOptions AnswerOption{ 0, 0, true, true, true };
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
		UE_LOG(PixelStreamer, Error, TEXT("Failed to apply remote ICE Candidate from Player %u"), PlayerId);
		DisconnectPlayer(TEXT("Failed to apply remote ICE Candidate"));
		return;
	}
}

void FPlayerSession::DisconnectPlayer(const FString& Reason)
{
	if (bDisconnecting)
		return; // already notified SignallingServer to disconnect this player

	bDisconnecting = true;
	Streamer.SignallingServerConnection->SendDisconnectPlayer(PlayerId, Reason);
}

//
// webrtc::PeerConnectionObserver implementation.
//

void FPlayerSession::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState NewState)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%u, NewState=%s"), TEXT(__FUNCTION__), PlayerId, ToString(NewState));
}

// Called when a remote stream is added
void FPlayerSession::OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%u, Stream=%s"), TEXT(__FUNCTION__), PlayerId, *ToString(Stream->id()));
}

void FPlayerSession::OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> Stream)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%u, Stream=%s"), TEXT(__FUNCTION__), PlayerId, *ToString(Stream->id()));
}

void FPlayerSession::OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> InDataChannel)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%u"), TEXT(__FUNCTION__), PlayerId);
	DataChannel = InDataChannel;
	DataChannel->RegisterObserver(this);
}

void FPlayerSession::OnRenegotiationNeeded()
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%u"), TEXT(__FUNCTION__), PlayerId);
}

void FPlayerSession::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState NewState)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%u, NewState=%s"), TEXT(__FUNCTION__), PlayerId, ToString(NewState));
}

void FPlayerSession::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState NewState)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%u, NewState=%s"), TEXT(__FUNCTION__), PlayerId, ToString(NewState));
}

void FPlayerSession::OnIceCandidate(const webrtc::IceCandidateInterface* Candidate)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%u"), TEXT(__FUNCTION__), PlayerId);

	Streamer.SignallingServerConnection->SendIceCandidate(PlayerId, *Candidate);
}

void FPlayerSession::OnIceCandidatesRemoved(const std::vector<cricket::Candidate>& candidates)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%u"), TEXT(__FUNCTION__), PlayerId);
}

void FPlayerSession::OnIceConnectionReceivingChange(bool Receiving)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%u, Receiving=%d"), TEXT(__FUNCTION__), PlayerId, *reinterpret_cast<int8*>(&Receiving));
}

void FPlayerSession::OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%u"), TEXT(__FUNCTION__), PlayerId);
}

void FPlayerSession::OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver)
{
	UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%u"), TEXT(__FUNCTION__), PlayerId);
}

//
// webrtc::DataChannelObserver implementation.
//

void FPlayerSession::OnMessage(const webrtc::DataBuffer& Buffer)
{
	auto MsgType = static_cast<PixelStreamingProtocol::EToStreamerMsg>(Buffer.data.data()[0]);
	if (MsgType == PixelStreamingProtocol::EToStreamerMsg::RequestQualityControl)
	{
		check(Buffer.data.size() == 1);
		Streamer.OnQualityOwnership(PlayerId);
	}
	else
	{
		InputDevice.OnMessage(Buffer.data.data(), static_cast<uint32>(Buffer.data.size()));
	}
}

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

//////////////////////////////////////////////////////////////////////////

bool FPlayerSession::IsOriginalQualityController() const
{
	return bOriginalQualityController;
}

bool FPlayerSession::IsQualityController() const
{
	return VideoEncoder != nullptr && VideoEncoder.Load()->IsQualityController();
}

void FPlayerSession::SetQualityController(bool bControlsQuality)
{
	if (!VideoEncoder || !DataChannel)
	{
		return;
	}

	VideoEncoder.Load()->SetQualityController(bControlsQuality);
	rtc::CopyOnWriteBuffer Buf(2);
	Buf[0] = static_cast<uint8_t>(PixelStreamingProtocol::EToPlayerMsg::QualityControlOwnership);
	Buf[1] = bControlsQuality ? 1 : 0;
	DataChannel->Send(webrtc::DataBuffer(Buf, true));
}

void FPlayerSession::SendMessage(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor)
{
	if (!DataChannel)
	{
		return;
	}

	rtc::CopyOnWriteBuffer Buffer(Descriptor.Len() * sizeof(TCHAR) + 1); //-V727
	Buffer[0] = static_cast<uint8_t>(Type);
	FMemory::Memcpy(&Buffer[1], reinterpret_cast<const uint8_t*>(*Descriptor), Descriptor.Len() * sizeof(TCHAR));
	DataChannel->Send(webrtc::DataBuffer(Buffer, true));
}

void FPlayerSession::SendFreezeFrame(const TArray<uint8>& JpegBytes)
{
	if (!DataChannel)
	{
		UE_LOG(PixelStreamer, Error, TEXT("Freeze frame can't be sent as data channel is null"));
		return;
	}

	UE_LOG(PixelStreamer, VeryVerbose, TEXT("player %d: %s"), PlayerId, TEXT(__FUNCTION__));

	// just a sanity check. WebRTC buffer size is 16MB, which translates to 3840Mbps for 30fps video. It's not expected
	// that freeze frame size will come close to this limit and it's not expected we'll send freeze frames too often.
	// if this fails check that you send compressed image or that you don't do this too often (like 30fps or more)
	if (DataChannel->buffered_amount() + JpegBytes.Num() >= 16 * 1024 * 1024)
	{
		UE_LOG(PixelStreamer, Error, TEXT("Freeze frame too big: image size %d, buffered amount %d"), JpegBytes.Num(), DataChannel->buffered_amount());
		return;
	}

	int32 JpegSize = JpegBytes.Num();
	rtc::CopyOnWriteBuffer Buffer(1 /* packetId */ + sizeof(JpegSize) + JpegSize);
	Buffer[0] = static_cast<uint8_t>(PixelStreamingProtocol::EToPlayerMsg::FreezeFrame);
	FMemory::Memcpy(&Buffer[1], reinterpret_cast<const uint8_t*>(&JpegSize), sizeof(JpegSize));
	FMemory::Memcpy(&Buffer[1 + sizeof(JpegSize)], JpegBytes.GetData(), JpegBytes.Num());
	if (!DataChannel->Send(webrtc::DataBuffer(Buffer, true)))
	{
		UE_LOG(PixelStreamer, Error, TEXT("failed to send freeze frame"));
	}
}

void FPlayerSession::SendUnfreezeFrame()
{
	if (!DataChannel)
	{
		UE_LOG(PixelStreamer, Error, TEXT("Freeze frame can't be sent as data channel is null"));
		return;
	}

	UE_LOG(PixelStreamer, VeryVerbose, TEXT("player %d: %s"), PlayerId, TEXT(__FUNCTION__));

	rtc::CopyOnWriteBuffer Buffer(2);
	Buffer[0] = static_cast<uint8_t>(PixelStreamingProtocol::EToPlayerMsg::UnfreezeFrame);
	DataChannel->Send(webrtc::DataBuffer(Buffer, true));
}

void FPlayerSession::OnBufferedAmountChange(uint64 PreviousAmount)
{
	UE_LOG(PixelStreamer, VeryVerbose, TEXT("player %d: OnBufferedAmountChanged: prev %d, cur %d"), PlayerId, PreviousAmount, DataChannel->buffered_amount());
}

void FPlayerSession::SetVideoEncoder(FVideoEncoder* InVideoEncoder)
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
