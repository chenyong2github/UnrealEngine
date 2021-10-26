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
    SUBMIT_TASK_WITH_RETURN(int, GetNumPlayers_SignallingThread);
}

IPixelStreamingAudioSink* FThreadSafePlayerSessions::GetAudioSink(FPlayerId PlayerId) const
{
    SUBMIT_TASK_WITH_PARAMS_AND_RETURN(IPixelStreamingAudioSink*, GetAudioSink_SignallingThread, PlayerId)
}

IPixelStreamingAudioSink* FThreadSafePlayerSessions::GetUnlistenedAudioSink() const
{
    SUBMIT_TASK_WITH_RETURN(IPixelStreamingAudioSink*, GetUnlistenedAudioSink_SignallingThread)
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
    {
        FScopeLock Lock(&QualityControllerCS);
        this->QualityControllingPlayer = PlayerId;
    }

    SUBMIT_TASK_WITH_PARAMS(SetQualityController_SignallingThread, PlayerId)
}

bool FThreadSafePlayerSessions::SendMessage(FPlayerId PlayerId, PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const
{
    SUBMIT_TASK_WITH_PARAMS_AND_RETURN(bool, SendMessage_SignallingThread, PlayerId, Type, Descriptor)
}

void FThreadSafePlayerSessions::SendLatestQP(FPlayerId PlayerId, int LatestQP) const
{
    SUBMIT_TASK_WITH_PARAMS(SendLatestQP_SignallingThread, PlayerId, LatestQP)
}

void FThreadSafePlayerSessions::SendFreezeFrameTo(FPlayerId PlayerId, const TArray64<uint8>& JpegBytes) const
{
    SUBMIT_TASK_WITH_PARAMS(SendFreezeFrameTo_SignallingThread, PlayerId, JpegBytes)
}

void FThreadSafePlayerSessions::SendFreezeFrame(const TArray64<uint8>& JpegBytes)
{
    SUBMIT_TASK_WITH_PARAMS(SendFreezeFrame_SignallingThread, JpegBytes)
}

void FThreadSafePlayerSessions::SendUnfreezeFrame()
{
    SUBMIT_TASK_NO_PARAMS(SendUnfreezeFrame_SignallingThread)
}

webrtc::PeerConnectionInterface* FThreadSafePlayerSessions::CreatePlayerSession(FPlayerId PlayerId, 
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory,
    webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig,
    FSignallingServerConnection* SignallingServerConnection)
{
    SUBMIT_TASK_WITH_PARAMS_AND_RETURN(webrtc::PeerConnectionInterface*, CreatePlayerSession_SignallingThread, PlayerId, PeerConnectionFactory, PeerConnectionConfig, SignallingServerConnection)
}

void FThreadSafePlayerSessions::DeleteAllPlayerSessions()
{
    SUBMIT_TASK_NO_PARAMS(DeleteAllPlayerSessions_SignallingThread)
}

int FThreadSafePlayerSessions::DeletePlayerSession(FPlayerId PlayerId)
{
    SUBMIT_TASK_WITH_PARAMS_AND_RETURN(int, DeletePlayerSession_SignallingThread, PlayerId)
}

void FThreadSafePlayerSessions::DisconnectPlayer(FPlayerId PlayerId, const FString& Reason)
{
    SUBMIT_TASK_WITH_PARAMS(DisconnectPlayer_SignallingThread, PlayerId, Reason)
}

FPixelStreamingDataChannelObserver* FThreadSafePlayerSessions::GetDataChannelObserver(FPlayerId PlayerId)
{
    SUBMIT_TASK_WITH_PARAMS_AND_RETURN(FPixelStreamingDataChannelObserver*, GetDataChannelObserver_SignallingThread, PlayerId)
}

void FThreadSafePlayerSessions::SendMessageAll(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const
{
    SUBMIT_TASK_WITH_PARAMS(SendMessageAll_SignallingThread, Type, Descriptor)
}

void FThreadSafePlayerSessions::SendLatestQPAllPlayers(int LatestQP) const
{
    SUBMIT_TASK_WITH_PARAMS(SendLatestQPAllPlayers_SignallingThread, LatestQP)
}

void FThreadSafePlayerSessions::OnRemoteIceCandidate(FPlayerId PlayerId, const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp)
{
    SUBMIT_TASK_WITH_PARAMS(OnRemoteIceCandidate_SignallingThread, PlayerId, SdpMid, SdpMLineIndex, Sdp)
}

/////////////////////////
// Internal methods
/////////////////////////

void FThreadSafePlayerSessions::OnRemoteIceCandidate_SignallingThread(FPlayerId PlayerId, const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp)
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    FPlayerSession* Player = this->GetPlayerSession_SignallingThread(PlayerId);
	if(Player)
	{
		Player->OnRemoteIceCandidate(SdpMid, SdpMLineIndex, Sdp);
	}
	else
	{
		UE_LOG(PixelStreamer, Log, TEXT("Could not pass remote ice candidate to player because Player %s no available."), *PlayerId);
	}
}

IPixelStreamingAudioSink* FThreadSafePlayerSessions::GetUnlistenedAudioSink_SignallingThread() const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    for (auto& Entry : Players)
	{
		FPlayerSession* Session = Entry.Value;
		FPixelStreamingAudioSink& AudioSink = Session->GetAudioSink();
		if(!AudioSink.HasAudioConsumers())
		{
            FPixelStreamingAudioSink* AudioSinkPtr = &AudioSink;
			return static_cast<IPixelStreamingAudioSink*>(AudioSinkPtr);
		}
	}
	return nullptr;
}

IPixelStreamingAudioSink* FThreadSafePlayerSessions::GetAudioSink_SignallingThread(FPlayerId PlayerId) const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    FPlayerSession* PlayerSession = this->GetPlayerSession_SignallingThread(PlayerId);

    if(PlayerSession)
    {
        FPixelStreamingAudioSink* AudioSink = &PlayerSession->GetAudioSink();
        return static_cast<IPixelStreamingAudioSink*>(AudioSink);
    }
    return nullptr;
}

void FThreadSafePlayerSessions::SendLatestQPAllPlayers_SignallingThread(int LatestQP) const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    for(auto& PlayerEntry : this->Players)
    {
        PlayerEntry.Value->SendVideoEncoderQP(LatestQP);
    }
}

void FThreadSafePlayerSessions::SendLatestQP_SignallingThread(FPlayerId PlayerId, int LatestQP) const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	FPlayerSession* Session = this->GetPlayerSession_SignallingThread(PlayerId);
	if(Session)
	{
		return Session->SendVideoEncoderQP(LatestQP);
	}
	else
	{
		UE_LOG(PixelStreamer, Log, TEXT("Could not send latest QP for PlayerId=%s because that player was not found."), *PlayerId);
	}
}

bool FThreadSafePlayerSessions::SendMessage_SignallingThread(FPlayerId PlayerId, PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    UE_LOG(PixelStreamer, Log, TEXT("SendMessage to: %s | Type: %d | Message: %s"), *PlayerId, static_cast<int32>(Type), *Descriptor);

    FPlayerSession* PlayerSession = this->GetPlayerSession_SignallingThread(PlayerId);

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

void FThreadSafePlayerSessions::SendMessageAll_SignallingThread(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    UE_LOG(PixelStreamer, Log, TEXT("SendMessageAll: %d - %s"), static_cast<int32>(Type), *Descriptor);

    for(auto& PlayerEntry : this->Players)
    {
        PlayerEntry.Value->SendMessage(Type, Descriptor);
    }
}

FPixelStreamingDataChannelObserver* FThreadSafePlayerSessions::GetDataChannelObserver_SignallingThread(FPlayerId PlayerId)
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    FPlayerSession* Player = this->GetPlayerSession_SignallingThread(PlayerId);
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

void FThreadSafePlayerSessions::DisconnectPlayer_SignallingThread(FPlayerId PlayerId, const FString& Reason)
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    FPlayerSession* Player = this->GetPlayerSession_SignallingThread(PlayerId);
    if(Player != nullptr)
    {
        Player->DisconnectPlayer(Reason);
    }
    else
    {
        UE_LOG(PixelStreamer, Log, TEXT("Cannot disconnect player: %s - player does not exist."), *PlayerId);
    }
}

int FThreadSafePlayerSessions::GetNumPlayers_SignallingThread() const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    return this->Players.Num();
}

void FThreadSafePlayerSessions::SendFreezeFrame_SignallingThread(const TArray64<uint8>& JpegBytes)
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

void FThreadSafePlayerSessions::SendUnfreezeFrame_SignallingThread()
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    UE_LOG(PixelStreamer, Log, TEXT("Sending unfreeze message to players"));

    for (auto& PlayerEntry : Players)
    {
        PlayerEntry.Value->SendUnfreezeFrame();
    }
}

void FThreadSafePlayerSessions::SendFreezeFrameTo_SignallingThread(FPlayerId PlayerId, const TArray64<uint8>& JpegBytes) const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    FPlayerSession* Player = this->GetPlayerSession_SignallingThread(PlayerId);

    if(Player != nullptr)
    {
        this->Players[PlayerId]->SendFreezeFrame(JpegBytes);
    }
    else
    {
        UE_LOG(PixelStreamer, Log, TEXT("Cannot send freeze frame to player: %s - player does not exist."), *PlayerId);
    }
}

FPlayerSession* FThreadSafePlayerSessions::GetPlayerSession_SignallingThread(FPlayerId PlayerId) const
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    if(this->Players.Contains(PlayerId))
    {
        return this->Players[PlayerId];
    }
    return nullptr;
}

void FThreadSafePlayerSessions::DeleteAllPlayerSessions_SignallingThread()
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

        // Player deleted, tell all our C++ listeners.
        this->OnPlayerDeleted.Broadcast(PlayerId);
    }

    this->Players.Empty();
    this->QualityControllingPlayer = INVALID_PLAYER_ID;

    if(Delegates)
    {
        Delegates->OnAllConnectionsClosed.Broadcast();
    }
}



int FThreadSafePlayerSessions::DeletePlayerSession_SignallingThread(FPlayerId PlayerId)
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

    FPlayerSession* Player = this->GetPlayerSession_SignallingThread(PlayerId);
	if (!Player)
	{
		UE_LOG(PixelStreamer, VeryVerbose, TEXT("Failed to delete player %s - that player was not found."), *PlayerId);
		return this->GetNumPlayers_SignallingThread();
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

    // Player deleted, tell all our C++ listeners.
    this->OnPlayerDeleted.Broadcast(PlayerId);

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
            this->SetQualityController_SignallingThread(Entry.Key);
            break;
        }
		
	}

    return this->GetNumPlayers_SignallingThread();
}

webrtc::PeerConnectionInterface* FThreadSafePlayerSessions::CreatePlayerSession_SignallingThread(FPlayerId PlayerId, 
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
        this->SetQualityController_SignallingThread(PlayerId);
	}

	if (UPixelStreamerDelegates* Delegates = UPixelStreamerDelegates::GetPixelStreamerDelegates())
	{
		Delegates->OnNewConnection.Broadcast(PlayerId, bMakeQualityController);
	}

    return PeerConnection.get();
}

void FThreadSafePlayerSessions::SetQualityController_SignallingThread(FPlayerId PlayerId)
{
    checkf(this->IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	if (this->Players.Contains(PlayerId))
	{

        // The actual assignment of the quality controlling peeer
        {
            FScopeLock Lock(&QualityControllerCS);
            this->QualityControllingPlayer = PlayerId;
        }
		
        // Let any listeners know the quality controller has changed
        this->OnQualityControllerChanged.Broadcast(PlayerId);

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