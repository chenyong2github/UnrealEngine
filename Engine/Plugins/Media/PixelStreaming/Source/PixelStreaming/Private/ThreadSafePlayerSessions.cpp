// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThreadSafePlayerSessions.h"
#include "rtc_base/location.h"
#include "PixelStreamingSettings.h"
#include "PixelStreamerDelegates.h"

#define SUBMIT_TASK_WITH_RETURN(ReturnType, FuncWithReturnType) \
    if(this->IsInSignallingThread()) \
    { \
        return this->FuncWithReturnType(); \
    } \
    else \
    { \
        return this->WebRtcSignallingThread->Invoke<ReturnType>(RTC_FROM_HERE, [this]() -> ReturnType { return this->FuncWithReturnType(); } ); \
    } \

#define SUBMIT_TASK_WITH_PARAMS_AND_RETURN(ReturnType, FuncWithReturnType, ...) \
    if(this->IsInSignallingThread()) \
    { \
        return this->FuncWithReturnType(__VA_ARGS__); \
    } \
    else \
    { \
        return this->WebRtcSignallingThread->Invoke<ReturnType>(RTC_FROM_HERE, [this, __VA_ARGS__]() -> ReturnType { return this->FuncWithReturnType(__VA_ARGS__); } ); \
    } \

#define SUBMIT_TASK_NO_PARAMS(Func) \
    if(this->IsInSignallingThread()) \
    { \
        this->Func(); \
    } \
    else \
    { \
        this->WebRtcSignallingThread->PostTask(RTC_FROM_HERE, [this]() { this->Func(); } ); \
    } \

#define SUBMIT_TASK_WITH_PARAMS(Func, ...) \
    if(this->IsInSignallingThread()) \
    { \
        this->Func(__VA_ARGS__); \
    } \
    else \
    { \
        this->WebRtcSignallingThread->PostTask(RTC_FROM_HERE, [this, __VA_ARGS__]() { this->Func(__VA_ARGS__); } ); \
    } \

// NOTE: All public methods should be one-liner functions that call one of the above macros to ensure threadsafety of this class.

FThreadSafePlayerSessions::FThreadSafePlayerSessions(rtc::Thread* InWebRtcSignallingThread)
    : WebRtcSignallingThread(InWebRtcSignallingThread)
{

}

bool FThreadSafePlayerSessions::IsInSignallingThread() const
{
    if(this->WebRtcSignallingThread == nullptr)
    {
        return false;
    }

	return this->WebRtcSignallingThread->IsCurrent();
}


int FThreadSafePlayerSessions::GetNumPlayers() const
{
    SUBMIT_TASK_WITH_RETURN(int, GetNumPlayers_Internal);
}

FPixelStreamingAudioSink* FThreadSafePlayerSessions::GetAudioSink(FPlayerId PlayerId) const
{
    SUBMIT_TASK_WITH_PARAMS_AND_RETURN(FPixelStreamingAudioSink*, GetAudioSink_Internal, PlayerId)
}

FPixelStreamingAudioSink* FThreadSafePlayerSessions::GetUnlistenedAudioSink() const
{
    SUBMIT_TASK_WITH_RETURN(FPixelStreamingAudioSink*, GetUnlistenedAudioSink_Internal)
}

bool FThreadSafePlayerSessions::IsQualityController(FPlayerId PlayerId) const
{
    // Due to how many theads this particular method is called on we choose not to schedule reading it as a new task and instead just made the
    // the variable atomic. Additionally some of the calling threads were deadlocking waiting for each other to finish other methods while calling this method.
    // Not scheduling this as a task is the exception to the general rule of this class though.
    FScopeLock Lock(&QualityControllerCS);
    return this->QualityControllingPlayer == PlayerId;
}

void FThreadSafePlayerSessions::SetQualityController(FPlayerId PlayerId)
{
    FScopeLock Lock(&QualityControllerCS);
    this->QualityControllingPlayer = PlayerId;
}

bool FThreadSafePlayerSessions::SendMessage(FPlayerId PlayerId, PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const
{
    SUBMIT_TASK_WITH_PARAMS_AND_RETURN(bool, SendMessage_Internal, PlayerId, Type, Descriptor)
}

void FThreadSafePlayerSessions::SendLatestQP(FPlayerId PlayerId, int LatestQP) const
{
    SUBMIT_TASK_WITH_PARAMS(SendLatestQP_Internal, PlayerId, LatestQP)
}

void FThreadSafePlayerSessions::SendFreezeFrameTo(FPlayerId PlayerId, const TArray64<uint8>& JpegBytes) const
{
    SUBMIT_TASK_WITH_PARAMS(SendFreezeFrameTo_Internal, PlayerId, JpegBytes)
}

void FThreadSafePlayerSessions::SendFreezeFrame(const TArray64<uint8>& JpegBytes)
{
    SUBMIT_TASK_WITH_PARAMS(SendFreezeFrame_Internal, JpegBytes)
}

void FThreadSafePlayerSessions::SendUnfreezeFrame()
{
    SUBMIT_TASK_NO_PARAMS(SendUnfreezeFrame_Internal)
}

webrtc::PeerConnectionInterface* FThreadSafePlayerSessions::CreatePlayerSession(FPlayerId PlayerId, 
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory,
    webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig,
    FSignallingServerConnection* SignallingServerConnection)
{
    SUBMIT_TASK_WITH_PARAMS_AND_RETURN(webrtc::PeerConnectionInterface*, CreatePlayerSession_Internal, PlayerId, PeerConnectionFactory, PeerConnectionConfig, SignallingServerConnection)
}

void FThreadSafePlayerSessions::DeleteAllPlayerSessions()
{
    SUBMIT_TASK_NO_PARAMS(DeleteAllPlayerSessions_Internal)
}

int FThreadSafePlayerSessions::DeletePlayerSession(FPlayerId PlayerId)
{
    SUBMIT_TASK_WITH_PARAMS_AND_RETURN(int, DeletePlayerSession_Internal, PlayerId)
}

void FThreadSafePlayerSessions::DisconnectPlayer(FPlayerId PlayerId, const FString& Reason)
{
    SUBMIT_TASK_WITH_PARAMS(DisconnectPlayer_Internal, PlayerId, Reason)
}

FPixelStreamingDataChannelObserver* FThreadSafePlayerSessions::GetDataChannelObserver(FPlayerId PlayerId)
{
    SUBMIT_TASK_WITH_PARAMS_AND_RETURN(FPixelStreamingDataChannelObserver*, GetDataChannelObserver_Internal, PlayerId)
}

void FThreadSafePlayerSessions::SendMessageAll(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const
{
    SUBMIT_TASK_WITH_PARAMS(SendMessageAll_Internal, Type, Descriptor)
}

void FThreadSafePlayerSessions::SendLatestQPAllPlayers(int LatestQP) const
{
    SUBMIT_TASK_WITH_PARAMS(SendLatestQPAllPlayers_Internal, LatestQP)
}

void FThreadSafePlayerSessions::OnRemoteIceCandidate(FPlayerId PlayerId, const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp)
{
    SUBMIT_TASK_WITH_PARAMS(OnRemoteIceCandidate_Internal, PlayerId, SdpMid, SdpMLineIndex, Sdp)
}

/////////////////////////
// Internal methods
/////////////////////////

void FThreadSafePlayerSessions::OnRemoteIceCandidate_Internal(FPlayerId PlayerId, const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp)
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    FPlayerSession* Player = this->GetPlayerSession_Internal(PlayerId);
	if(Player)
	{
		Player->OnRemoteIceCandidate(SdpMid, SdpMLineIndex, Sdp);
	}
	else
	{
		UE_LOG(PixelStreamer, Log, TEXT("Could not pass remote ice candidate to player because Player %s no available."), *PlayerId);
	}
}

FPixelStreamingAudioSink* FThreadSafePlayerSessions::GetUnlistenedAudioSink_Internal() const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

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

FPixelStreamingAudioSink* FThreadSafePlayerSessions::GetAudioSink_Internal(FPlayerId PlayerId) const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    FPlayerSession* PlayerSession = this->GetPlayerSession_Internal(PlayerId);

    if(PlayerSession)
    {
        return &PlayerSession->GetAudioSink();
    }
    return nullptr;
}

void FThreadSafePlayerSessions::SendLatestQPAllPlayers_Internal(int LatestQP) const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    for(auto& PlayerEntry : this->Players)
    {
        PlayerEntry.Value->SendVideoEncoderQP(LatestQP);
    }
}

void FThreadSafePlayerSessions::SendLatestQP_Internal(FPlayerId PlayerId, int LatestQP) const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	FPlayerSession* Session = this->GetPlayerSession_Internal(PlayerId);
	if(Session)
	{
		return Session->SendVideoEncoderQP(LatestQP);
	}
	else
	{
		UE_LOG(PixelStreamer, Log, TEXT("Could not send latest QP for PlayerId=%s because that player was not found."), *PlayerId);
	}
}

bool FThreadSafePlayerSessions::SendMessage_Internal(FPlayerId PlayerId, PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    UE_LOG(PixelStreamer, Log, TEXT("SendMessage to: %s | Type: %d | Message: %s"), *PlayerId, static_cast<int32>(Type), *Descriptor);

    FPlayerSession* PlayerSession = this->GetPlayerSession_Internal(PlayerId);

    if(PlayerSession)
    {
        return PlayerSession->SendMessage(Type, Descriptor);
    }
    else
    {
        UE_LOG(PixelStreamer, Log, TEXT("Cannot send message to player: %s - player does not exist."), *PlayerId);
        return false;
    }
}

void FThreadSafePlayerSessions::SendMessageAll_Internal(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    UE_LOG(PixelStreamer, Log, TEXT("SendMessageAll: %d - %s"), static_cast<int32>(Type), *Descriptor);

    for(auto& PlayerEntry : this->Players)
    {
        PlayerEntry.Value->SendMessage(Type, Descriptor);
    }
}

FPixelStreamingDataChannelObserver* FThreadSafePlayerSessions::GetDataChannelObserver_Internal(FPlayerId PlayerId)
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    FPlayerSession* Player = this->GetPlayerSession_Internal(PlayerId);
    if(Player)
    {
        return &Player->GetDataChannelObserver();
    }
    else
    {
        UE_LOG(PixelStreamer, Log, TEXT("Cannot get data channel observer for player: %s - player does not exist."), *PlayerId);
        return nullptr;
    }
}

void FThreadSafePlayerSessions::DisconnectPlayer_Internal(FPlayerId PlayerId, const FString& Reason)
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    FPlayerSession* Player = this->GetPlayerSession_Internal(PlayerId);
    if(Player != nullptr)
    {
        Player->DisconnectPlayer(Reason);
    }
    else
    {
        UE_LOG(PixelStreamer, Log, TEXT("Cannot disconnect player: %s - player does not exist."), *PlayerId);
    }
}

int FThreadSafePlayerSessions::GetNumPlayers_Internal() const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    return this->Players.Num();
}

void FThreadSafePlayerSessions::SendFreezeFrame_Internal(const TArray64<uint8>& JpegBytes)
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	UE_LOG(PixelStreamer, Log, TEXT("Sending freeze frame to players: %d bytes"), JpegBytes.Num());
	{
		for (auto& PlayerEntry : this->Players)
		{
			PlayerEntry.Value->SendFreezeFrame(JpegBytes);
		}
	}
}

void FThreadSafePlayerSessions::SendUnfreezeFrame_Internal()
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    UE_LOG(PixelStreamer, Log, TEXT("Sending unfreeze message to players"));

    for (auto& PlayerEntry : Players)
    {
        PlayerEntry.Value->SendUnfreezeFrame();
    }
}

void FThreadSafePlayerSessions::SendFreezeFrameTo_Internal(FPlayerId PlayerId, const TArray64<uint8>& JpegBytes) const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    FPlayerSession* Player = this->GetPlayerSession_Internal(PlayerId);

    if(Player != nullptr)
    {
        this->Players[PlayerId]->SendFreezeFrame(JpegBytes);
    }
    else
    {
        UE_LOG(PixelStreamer, Log, TEXT("Cannot send freeze frame to player: %s - player does not exist."), *PlayerId);
    }
}

FPlayerSession* FThreadSafePlayerSessions::GetPlayerSession_Internal(FPlayerId PlayerId) const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    if(this->Players.Contains(PlayerId))
    {
        return this->Players[PlayerId];
    }
    return nullptr;
}

void FThreadSafePlayerSessions::DeleteAllPlayerSessions_Internal()
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    UPixelStreamerDelegates* Delegates = UPixelStreamerDelegates::GetPixelStreamerDelegates();

    // Delete each player
    for(auto& Entry : this->Players)
    {
        FPlayerId PlayerId = Entry.Key;
        FPlayerSession* Player = Entry.Value;
        delete Player;
        bool bWasQualityController = this->QualityControllingPlayer == PlayerId;

        if(Delegates)
        {
            Delegates->OnClosedConnection.Broadcast(PlayerId, bWasQualityController);
        }
    }

    this->Players.Empty();
    this->QualityControllingPlayer = INVALID_PLAYER_ID;

    if(Delegates)
    {
        Delegates->OnAllConnectionsClosed.Broadcast();
    }
}



int FThreadSafePlayerSessions::DeletePlayerSession_Internal(FPlayerId PlayerId)
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    FPlayerSession* Player = this->GetPlayerSession_Internal(PlayerId);
	if (!Player)
	{
		UE_LOG(PixelStreamer, VeryVerbose, TEXT("Failed to delete player %s - that player was not found."), *PlayerId);
		return this->GetNumPlayers_Internal();
	}

	bool bWasQualityController = this->IsQualityController(PlayerId);

    // The actual modification to the players map
    Players.Remove(PlayerId);
	delete Player;

	UPixelStreamerDelegates* Delegates = UPixelStreamerDelegates::GetPixelStreamerDelegates();
	if (Delegates)
	{
		Delegates->OnClosedConnection.Broadcast(PlayerId, bWasQualityController);
	}

	// this is called from WebRTC signalling thread, the only thread were `Players` map is modified, so no need to lock it
	if (Players.Num() == 0)
	{
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
		for(auto& Entry : this->Players)
        {
            this->SetQualityController_Internal(Entry.Key);
            break;
        }
		
	}

    return this->GetNumPlayers_Internal();
}

webrtc::PeerConnectionInterface* FThreadSafePlayerSessions::CreatePlayerSession_Internal(FPlayerId PlayerId, 
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory, 
    webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig,
    FSignallingServerConnection* SignallingServerConnection)
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));
	check(PeerConnectionFactory);

	// With unified plan, we get several calls to OnOffer, which in turn calls
	// this several times.
	// Therefore, we only try to create the player if not created already
	if (Players.Contains(PlayerId))
    {
        return nullptr;
    }

	UE_LOG(PixelStreamer, Log, TEXT("Creating player session for PlayerId=%s"), *PlayerId);
	
	// this is called from WebRTC signalling thread, the only thread were `Players` map is modified, so no need to lock it
	bool bMakeQualityController = Players.Num() == 0; // first player controls quality by default
	FPlayerSession* Session = new FPlayerSession(this, SignallingServerConnection, PlayerId);

	rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection = PeerConnectionFactory->CreatePeerConnection(PeerConnectionConfig, webrtc::PeerConnectionDependencies{ Session });
	check(PeerConnection);

	// Setup suggested bitrate settings on the Peer Connection based on our CVars
	webrtc::BitrateSettings BitrateSettings;
	BitrateSettings.min_bitrate_bps = PixelStreamingSettings::CVarPixelStreamingWebRTCMinBitrate.GetValueOnAnyThread();
	BitrateSettings.max_bitrate_bps = PixelStreamingSettings::CVarPixelStreamingWebRTCMaxBitrate.GetValueOnAnyThread();
	BitrateSettings.start_bitrate_bps = PixelStreamingSettings::CVarPixelStreamingWebRTCStartBitrate.GetValueOnAnyThread();
	PeerConnection->SetBitrate(BitrateSettings);

	Session->SetPeerConnection(PeerConnection);

    // The actual modification of the players map
	Players.Add(PlayerId, Session);

	if(bMakeQualityController)
	{
        this->SetQualityController_Internal(PlayerId);
	}

	if (UPixelStreamerDelegates* Delegates = UPixelStreamerDelegates::GetPixelStreamerDelegates())
	{
		Delegates->OnNewConnection.Broadcast(PlayerId, bMakeQualityController);
	}

    return PeerConnection.get();
}

void FThreadSafePlayerSessions::SetQualityController_Internal(FPlayerId PlayerId)
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	if (this->Players.Contains(PlayerId))
	{

        // The actual assignment of the quality controlling peeer
        {
            FScopeLock Lock(&QualityControllerCS);
            this->QualityControllingPlayer = PlayerId;
        }
		

		UE_LOG(PixelStreamer, Log, TEXT("Quality controller is now PlayerId=%s."), *PlayerId);

		// Update quality controller status on the browser side too
		for (auto& Entry : Players)
		{
			FPlayerSession* Session = Entry.Value;
            bool bIsQualityController = Entry.Key == this->QualityControllingPlayer;
			if(Session)
			{
				Session->SendQualityControlStatus(bIsQualityController);
			}
		}

	}
    else
    {
        UE_LOG(PixelStreamer, Log, TEXT("Could not set quality controller for PlayerId=%s - that player does not exist."), *PlayerId);
    }
}