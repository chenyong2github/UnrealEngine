// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThreadSafePlayerSessions.h"
#include "rtc_base/location.h"
#include "Settings.h"
#include "PixelStreamingDelegates.h"
#include "Streamer.h"

#define SUBMIT_TASK_WITH_RETURN(ReturnType, FuncWithReturnType)                                                                    \
	if (IsInSignallingThread())                                                                                                    \
	{                                                                                                                              \
		return FuncWithReturnType();                                                                                               \
	}                                                                                                                              \
	else                                                                                                                           \
	{                                                                                                                              \
		return WebRtcSignallingThread->Invoke<ReturnType>(RTC_FROM_HERE, [this]() -> ReturnType { return FuncWithReturnType(); }); \
	}

#define SUBMIT_TASK_WITH_PARAMS_AND_RETURN(ReturnType, FuncWithReturnType, ...)                                                                            \
	if (IsInSignallingThread())                                                                                                                            \
	{                                                                                                                                                      \
		return FuncWithReturnType(__VA_ARGS__);                                                                                                            \
	}                                                                                                                                                      \
	else                                                                                                                                                   \
	{                                                                                                                                                      \
		return WebRtcSignallingThread->Invoke<ReturnType>(RTC_FROM_HERE, [this, __VA_ARGS__]() -> ReturnType { return FuncWithReturnType(__VA_ARGS__); }); \
	}

#define SUBMIT_TASK_NO_PARAMS(Func)                                            \
	if (IsInSignallingThread())                                                \
	{                                                                          \
		Func();                                                                \
	}                                                                          \
	else                                                                       \
	{                                                                          \
		WebRtcSignallingThread->PostTask(RTC_FROM_HERE, [this]() { Func(); }); \
	}

#define SUBMIT_TASK_WITH_PARAMS(Func, ...)                                                             \
	if (IsInSignallingThread())                                                                        \
	{                                                                                                  \
		Func(__VA_ARGS__);                                                                             \
	}                                                                                                  \
	else                                                                                               \
	{                                                                                                  \
		WebRtcSignallingThread->PostTask(RTC_FROM_HERE, [this, __VA_ARGS__]() { Func(__VA_ARGS__); }); \
	}

// NOTE: All public methods should be one-liner functions that call one of the above macros to ensure threadsafety of this class.

UE::PixelStreaming::FThreadSafePlayerSessions::FThreadSafePlayerSessions(rtc::Thread* InWebRtcSignallingThread)
	: WebRtcSignallingThread(InWebRtcSignallingThread)
{
}

bool UE::PixelStreaming::FThreadSafePlayerSessions::IsInSignallingThread() const
{
	if (WebRtcSignallingThread == nullptr)
	{
		return false;
	}

	return WebRtcSignallingThread->IsCurrent();
}

int UE::PixelStreaming::FThreadSafePlayerSessions::GetNumPlayers() const
{
	SUBMIT_TASK_WITH_RETURN(int, GetNumPlayers_SignallingThread);
}

IPixelStreamingAudioSink* UE::PixelStreaming::FThreadSafePlayerSessions::GetAudioSink(FPixelStreamingPlayerId PlayerId) const {
	SUBMIT_TASK_WITH_PARAMS_AND_RETURN(IPixelStreamingAudioSink*, GetAudioSink_SignallingThread, PlayerId)
}

IPixelStreamingAudioSink* UE::PixelStreaming::FThreadSafePlayerSessions::GetUnlistenedAudioSink() const
{
	SUBMIT_TASK_WITH_RETURN(IPixelStreamingAudioSink*, GetUnlistenedAudioSink_SignallingThread)
}

bool UE::PixelStreaming::FThreadSafePlayerSessions::IsQualityController(FPixelStreamingPlayerId PlayerId) const
{
	// Due to how many theads this particular method is called on we choose not to schedule reading it as a new task and instead just made the
	// the variable atomic. Additionally some of the calling threads were deadlocking waiting for each other to finish other methods while calling this method.
	// Not scheduling this as a task is the exception to the general rule of this class though.
	FScopeLock Lock(&QualityControllerCS);
	return QualityControllingPlayer == PlayerId;
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SetQualityController(FPixelStreamingPlayerId PlayerId)
{
	{
		FScopeLock Lock(&QualityControllerCS);
		QualityControllingPlayer = PlayerId;
	}

	SUBMIT_TASK_WITH_PARAMS(SetQualityController_SignallingThread, PlayerId)
}

bool UE::PixelStreaming::FThreadSafePlayerSessions::SendMessage(FPixelStreamingPlayerId PlayerId, UE::PixelStreaming::Protocol::EToPlayerMsg Type, const FString& Descriptor) const
{
	SUBMIT_TASK_WITH_PARAMS_AND_RETURN(bool, SendMessage_SignallingThread, PlayerId, Type, Descriptor)
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SendLatestQP(FPixelStreamingPlayerId PlayerId, int LatestQP) const
{
	SUBMIT_TASK_WITH_PARAMS(SendLatestQP_SignallingThread, PlayerId, LatestQP)
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SendFreezeFrameTo(FPixelStreamingPlayerId PlayerId, const TArray64<uint8>& JpegBytes) const
{
	SUBMIT_TASK_WITH_PARAMS(SendFreezeFrameTo_SignallingThread, PlayerId, JpegBytes)
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SendFreezeFrame(const TArray64<uint8>& JpegBytes)
{
	SUBMIT_TASK_WITH_PARAMS(SendFreezeFrame_SignallingThread, JpegBytes)
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SendUnfreezeFrame()
{
	SUBMIT_TASK_NO_PARAMS(SendUnfreezeFrame_SignallingThread)
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SendFileData(const TArray<uint8>& ByteData, const FString& MimeType, const FString& FileExtension){
	SUBMIT_TASK_WITH_PARAMS(SendFileData_SignallingThread, ByteData, MimeType, FileExtension)
}

webrtc::PeerConnectionInterface* UE::PixelStreaming::FThreadSafePlayerSessions::CreatePlayerSession(
	FPixelStreamingPlayerId PlayerId,
	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory,
	webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig,
	UE::PixelStreaming::FSignallingServerConnection* SignallingServerConnection,
	int Flags)
{
	SUBMIT_TASK_WITH_PARAMS_AND_RETURN(webrtc::PeerConnectionInterface*, CreatePlayerSession_SignallingThread, PlayerId, PeerConnectionFactory, PeerConnectionConfig, SignallingServerConnection, Flags)
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SetPlayerSessionDataChannel(FPixelStreamingPlayerId PlayerId, const rtc::scoped_refptr<webrtc::DataChannelInterface>& DataChannel)
{
	SUBMIT_TASK_WITH_PARAMS(SetPlayerSessionDataChannel_SignallingThread, PlayerId, DataChannel)
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SetVideoSource(FPixelStreamingPlayerId PlayerId, const rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>& VideoSource)
{
	SUBMIT_TASK_WITH_PARAMS(SetVideoSource_SignallingThread, PlayerId, VideoSource)
}

void UE::PixelStreaming::FThreadSafePlayerSessions::DeleteAllPlayerSessions()
{
	SUBMIT_TASK_NO_PARAMS(DeleteAllPlayerSessions_SignallingThread)
}

int UE::PixelStreaming::FThreadSafePlayerSessions::DeletePlayerSession(FPixelStreamingPlayerId PlayerId)
{
	SUBMIT_TASK_WITH_PARAMS_AND_RETURN(int, DeletePlayerSession_SignallingThread, PlayerId)
}

void UE::PixelStreaming::FThreadSafePlayerSessions::DisconnectPlayer(FPixelStreamingPlayerId PlayerId, const FString& Reason){
	SUBMIT_TASK_WITH_PARAMS(DisconnectPlayer_SignallingThread, PlayerId, Reason)
}

UE::PixelStreaming::FDataChannelObserver* UE::PixelStreaming::FThreadSafePlayerSessions::GetDataChannelObserver(FPixelStreamingPlayerId PlayerId)
{
	SUBMIT_TASK_WITH_PARAMS_AND_RETURN(UE::PixelStreaming::FDataChannelObserver*, GetDataChannelObserver_SignallingThread, PlayerId)
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SendMessageAll(UE::PixelStreaming::Protocol::EToPlayerMsg Type, const FString& Descriptor) const
{
	SUBMIT_TASK_WITH_PARAMS(SendMessageAll_SignallingThread, Type, Descriptor)
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SendLatestQPAllPlayers(int LatestQP) const
{
	SUBMIT_TASK_WITH_PARAMS(SendLatestQPAllPlayers_SignallingThread, LatestQP)
}

void UE::PixelStreaming::FThreadSafePlayerSessions::OnRemoteIceCandidate(FPixelStreamingPlayerId PlayerId, const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp)
{
	SUBMIT_TASK_WITH_PARAMS(OnRemoteIceCandidate_SignallingThread, PlayerId, SdpMid, SdpMLineIndex, Sdp)
}

void UE::PixelStreaming::FThreadSafePlayerSessions::OnAnswer(FPixelStreamingPlayerId PlayerId, FString Sdp)
{
	SUBMIT_TASK_WITH_PARAMS(OnAnswer_SignallingThread, PlayerId, Sdp)
}

void UE::PixelStreaming::FThreadSafePlayerSessions::PollWebRTCStats() const
{
	SUBMIT_TASK_NO_PARAMS(PollWebRTCStats_SignallingThread)
}

/////////////////////////
// Internal methods
/////////////////////////

void UE::PixelStreaming::FThreadSafePlayerSessions::PollWebRTCStats_SignallingThread() const
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	for (auto& Entry : Players)
	{
		UE::PixelStreaming::FPlayerSession* Player = Entry.Value;
		if (Player)
		{
			Player->PollWebRTCStats();
		}
		else
		{
			UE_LOG(LogPixelStreaming, Log, TEXT("Could not poll WebRTC stats for peer because peer was nullptr."));
		}
	}
}

void UE::PixelStreaming::FThreadSafePlayerSessions::OnRemoteIceCandidate_SignallingThread(FPixelStreamingPlayerId PlayerId, const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp)
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	UE::PixelStreaming::FPlayerSession* Player = GetPlayerSession_SignallingThread(PlayerId);
	if (Player)
	{
		Player->OnRemoteIceCandidate(SdpMid, SdpMLineIndex, Sdp);
	}
	else
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("Could not pass remote ice candidate to player because Player %s no available."), *PlayerId);
	}
}

void UE::PixelStreaming::FThreadSafePlayerSessions::OnAnswer_SignallingThread(FPixelStreamingPlayerId PlayerId, FString Sdp)
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	UE::PixelStreaming::FPlayerSession* Player = GetPlayerSession_SignallingThread(PlayerId);
	if (Player)
	{
		Player->OnAnswer(Sdp);
	}
	else
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("Could not pass answer to player because Player %s no available."), *PlayerId);
	}
}

IPixelStreamingAudioSink* UE::PixelStreaming::FThreadSafePlayerSessions::GetUnlistenedAudioSink_SignallingThread() const
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	for (auto& Entry : Players)
	{
		UE::PixelStreaming::FPlayerSession* Session = Entry.Value;
		UE::PixelStreaming::FAudioSink& AudioSink = Session->GetAudioSink();
		if (!AudioSink.HasAudioConsumers())
		{
			UE::PixelStreaming::FAudioSink* AudioSinkPtr = &AudioSink;
			return static_cast<IPixelStreamingAudioSink*>(AudioSinkPtr);
		}
	}
	return nullptr;
}

IPixelStreamingAudioSink* UE::PixelStreaming::FThreadSafePlayerSessions::GetAudioSink_SignallingThread(FPixelStreamingPlayerId PlayerId) const
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	UE::PixelStreaming::FPlayerSession* PlayerSession = GetPlayerSession_SignallingThread(PlayerId);

	if (PlayerSession)
	{
		UE::PixelStreaming::FAudioSink* AudioSink = &PlayerSession->GetAudioSink();
		return static_cast<IPixelStreamingAudioSink*>(AudioSink);
	}
	return nullptr;
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SendLatestQPAllPlayers_SignallingThread(int LatestQP) const
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	for (auto& PlayerEntry : Players)
	{
		PlayerEntry.Value->SendVideoEncoderQP(LatestQP);
	}
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SendLatestQP_SignallingThread(FPixelStreamingPlayerId PlayerId, int LatestQP) const
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	UE::PixelStreaming::FPlayerSession* Session = GetPlayerSession_SignallingThread(PlayerId);
	if (Session)
	{
		return Session->SendVideoEncoderQP(LatestQP);
	}
	else
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("Could not send latest QP for PlayerId=%s because that player was not found."), *PlayerId);
	}
}

bool UE::PixelStreaming::FThreadSafePlayerSessions::SendMessage_SignallingThread(FPixelStreamingPlayerId PlayerId, UE::PixelStreaming::Protocol::EToPlayerMsg Type, const FString& Descriptor) const
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	UE_LOG(LogPixelStreaming, Log, TEXT("SendMessage to: %s | Type: %d | Message: %s"), *PlayerId, static_cast<int32>(Type), *Descriptor);

	UE::PixelStreaming::FPlayerSession* PlayerSession = GetPlayerSession_SignallingThread(PlayerId);

	if (PlayerSession)
	{
		return PlayerSession->SendMessage(Type, Descriptor);
	}
	else
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("Cannot send message to player: %s - player does not exist."), *PlayerId);
		return false;
	}
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SendMessageAll_SignallingThread(UE::PixelStreaming::Protocol::EToPlayerMsg Type, const FString& Descriptor) const
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	UE_LOG(LogPixelStreaming, Log, TEXT("SendMessageAll: %d - %s"), static_cast<int32>(Type), *Descriptor);

	for (auto& PlayerEntry : Players)
	{
		PlayerEntry.Value->SendMessage(Type, Descriptor);
	}
}

UE::PixelStreaming::FDataChannelObserver* UE::PixelStreaming::FThreadSafePlayerSessions::GetDataChannelObserver_SignallingThread(FPixelStreamingPlayerId PlayerId)
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	UE::PixelStreaming::FPlayerSession* Player = GetPlayerSession_SignallingThread(PlayerId);
	if (Player)
	{
		return &Player->GetDataChannelObserver();
	}
	else
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("Cannot get data channel observer for player: %s - player does not exist."), *PlayerId);
		return nullptr;
	}
}

void UE::PixelStreaming::FThreadSafePlayerSessions::DisconnectPlayer_SignallingThread(FPixelStreamingPlayerId PlayerId, const FString& Reason)
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	UE::PixelStreaming::FPlayerSession* Player = GetPlayerSession_SignallingThread(PlayerId);
	if (Player != nullptr)
	{
		Player->DisconnectPlayer(Reason);
	}
	else
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("Cannot disconnect player: %s - player does not exist."), *PlayerId);
	}
}

int UE::PixelStreaming::FThreadSafePlayerSessions::GetNumPlayers_SignallingThread() const
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	return Players.Num();
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SendFreezeFrame_SignallingThread(const TArray64<uint8>& JpegBytes)
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	UE_LOG(LogPixelStreaming, Log, TEXT("Sending freeze frame to players: %d bytes"), JpegBytes.Num());
	{
		for (auto& PlayerEntry : Players)
		{
			PlayerEntry.Value->SendFreezeFrame(JpegBytes);
		}
	}
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SendFileData_SignallingThread(const TArray<uint8>& ByteData, const FString& MimeType, const FString& FileExtension)
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	UE_LOG(LogPixelStreaming, Log, TEXT("Sending file with Mime type %s to all players: %d bytes"), *MimeType, ByteData.Num());
	{
		for (auto& PlayerEntry : Players)
		{
			PlayerEntry.Value->SendFileData(ByteData, MimeType, FileExtension);
		}
	}
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SendUnfreezeFrame_SignallingThread()
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	UE_LOG(LogPixelStreaming, Log, TEXT("Sending unfreeze message to players"));

	for (auto& PlayerEntry : Players)
	{
		PlayerEntry.Value->SendUnfreezeFrame();
	}
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SendFreezeFrameTo_SignallingThread(FPixelStreamingPlayerId PlayerId, const TArray64<uint8>& JpegBytes) const
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	UE::PixelStreaming::FPlayerSession* Player = GetPlayerSession_SignallingThread(PlayerId);

	if (Player != nullptr)
	{
		Players[PlayerId]->SendFreezeFrame(JpegBytes);
	}
	else
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("Cannot send freeze frame to player: %s - player does not exist."), *PlayerId);
	}
}

UE::PixelStreaming::FPlayerSession* UE::PixelStreaming::FThreadSafePlayerSessions::GetPlayerSession_SignallingThread(FPixelStreamingPlayerId PlayerId) const
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	if (Players.Contains(PlayerId))
	{
		return Players[PlayerId];
	}
	return nullptr;
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SetPlayerSessionDataChannel_SignallingThread(FPixelStreamingPlayerId PlayerId, const rtc::scoped_refptr<webrtc::DataChannelInterface>& DataChannel)
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	UE::PixelStreaming::FPlayerSession* Player = GetPlayerSession_SignallingThread(PlayerId);
	if (Player)
	{
		Player->SetDataChannel(DataChannel);
	}
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SetVideoSource_SignallingThread(FPixelStreamingPlayerId PlayerId, const rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>& VideoSource)
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	UE::PixelStreaming::FPlayerSession* Player = GetPlayerSession_SignallingThread(PlayerId);
	if (Player)
	{
		Player->SetVideoSource(VideoSource);
	}
}

void UE::PixelStreaming::FThreadSafePlayerSessions::DeleteAllPlayerSessions_SignallingThread()
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates();

	// Delete each player
	for (auto& Entry : Players)
	{
		FPixelStreamingPlayerId PlayerId = Entry.Key;
		UE::PixelStreaming::FPlayerSession* Player = Entry.Value;
		delete Player;
		bool bWasQualityController = QualityControllingPlayer == PlayerId;

		if (Delegates && FModuleManager::Get().IsModuleLoaded("PixelStreaming"))
		{
			Delegates->OnClosedConnection.Broadcast(PlayerId, bWasQualityController);
			Delegates->OnClosedConnectionNative.Broadcast(PlayerId, bWasQualityController);
		}
	}

	Players.Empty();
	QualityControllingPlayer = INVALID_PLAYER_ID;

	if (Delegates && FModuleManager::Get().IsModuleLoaded("PixelStreaming"))
	{
		Delegates->OnAllConnectionsClosed.Broadcast();
		Delegates->OnAllConnectionsClosedNative.Broadcast();
	}
}

int UE::PixelStreaming::FThreadSafePlayerSessions::DeletePlayerSession_SignallingThread(FPixelStreamingPlayerId PlayerId)
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	UE::PixelStreaming::FPlayerSession* Player = GetPlayerSession_SignallingThread(PlayerId);
	if (!Player)
	{
		UE_LOG(LogPixelStreaming, VeryVerbose, TEXT("Failed to delete player %s - that player was not found."), *PlayerId);
		return GetNumPlayers_SignallingThread();
	}

	bool bWasQualityController = IsQualityController(PlayerId);

	// The actual modification to the players map
	Players.Remove(PlayerId);
	delete Player;
	UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates();
	if (Delegates)
	{
		Delegates->OnClosedConnection.Broadcast(PlayerId, bWasQualityController);
		Delegates->OnClosedConnectionNative.Broadcast(PlayerId, bWasQualityController);
	}

	// this is called from WebRTC signalling thread, the only thread were `Players` map is modified, so no need to lock it
	if (Players.Num() == 0)
	{
		// Inform the application-specific blueprint that nobody is viewing or
		// interacting with the app. This is an opportunity to reset the app.
		if (Delegates)
		{
			Delegates->OnAllConnectionsClosed.Broadcast();
			Delegates->OnAllConnectionsClosedNative.Broadcast();
		}
	}
	else if (bWasQualityController)
	{
		// Quality Controller session has been just removed, set quality control to any of remaining sessions
		for (auto& Entry : Players)
		{
			SetQualityController_SignallingThread(Entry.Key);
			break;
		}
	}

	return GetNumPlayers_SignallingThread();
}

webrtc::PeerConnectionInterface* UE::PixelStreaming::FThreadSafePlayerSessions::CreatePlayerSession_SignallingThread(
	FPixelStreamingPlayerId PlayerId,
	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory,
	webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig,
	UE::PixelStreaming::FSignallingServerConnection* SignallingServerConnection,
	int Flags)
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));
	check(PeerConnectionFactory);

	// With unified plan, we get several calls to OnOffer, which in turn calls
	// this several times.
	// Therefore, we only try to create the player if not created already
	if (Players.Contains(PlayerId))
	{
		return nullptr;
	}

	UE_LOG(LogPixelStreaming, Log, TEXT("Creating player session for PlayerId=%s"), *PlayerId);

	// this is called from WebRTC signalling thread, the only thread where `Players` map is modified, so no need to lock it
	bool bMakeQualityController = PlayerId != SFU_PLAYER_ID && Players.Num() == 0; // first player controls quality by default
	UE::PixelStreaming::FPlayerSession* Session = new UE::PixelStreaming::FPlayerSession(this, SignallingServerConnection, PlayerId);

	rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection = PeerConnectionFactory->CreatePeerConnection(PeerConnectionConfig, webrtc::PeerConnectionDependencies{ Session });
	if (!PeerConnection)
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("Failed to created PeerConnection. This may indicate you passed malformed peerConnectionOptions."));
		return nullptr;
	}

	// Setup suggested bitrate settings on the Peer Connection based on our CVars
	webrtc::BitrateSettings BitrateSettings;
	BitrateSettings.min_bitrate_bps = UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCMinBitrate.GetValueOnAnyThread();
	BitrateSettings.max_bitrate_bps = UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCMaxBitrate.GetValueOnAnyThread();
	BitrateSettings.start_bitrate_bps = UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCStartBitrate.GetValueOnAnyThread();
	PeerConnection->SetBitrate(BitrateSettings);

	Session->SetPeerConnection(PeerConnection);

	// The actual modification of the players map
	Players.Add(PlayerId, Session);

	if (bMakeQualityController)
	{
		SetQualityController_SignallingThread(PlayerId);
	}

	if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
	{
		Delegates->OnNewConnection.Broadcast(PlayerId, bMakeQualityController);
		Delegates->OnNewConnectionNative.Broadcast(PlayerId, bMakeQualityController);
	}

	return PeerConnection.get();
}

void UE::PixelStreaming::FThreadSafePlayerSessions::SetQualityController_SignallingThread(FPixelStreamingPlayerId PlayerId)
{
	checkf(IsInSignallingThread(), TEXT("This method must be called on the signalling thread."));

	if (Players.Contains(PlayerId))
	{

		// The actual assignment of the quality controlling peeer
		{
			FScopeLock Lock(&QualityControllerCS);
			QualityControllingPlayer = PlayerId;
		}

		// Let any listeners know the quality controller has changed
		if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
		{
			Delegates->OnQualityControllerChangedNative.Broadcast(PlayerId);
		}

		UE_LOG(LogPixelStreaming, Log, TEXT("Quality controller is now PlayerId=%s."), *PlayerId);

		// Update quality controller status on the browser side too
		for (auto& Entry : Players)
		{
			UE::PixelStreaming::FPlayerSession* Session = Entry.Value;
			bool bIsQualityController = Entry.Key == QualityControllingPlayer;
			if (Session)
			{
				Session->SendQualityControlStatus(bIsQualityController);
			}
		}
	}
	else
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("Could not set quality controller for PlayerId=%s - that player does not exist."), *PlayerId);
	}
}

#undef SUBMIT_TASK_WITH_RETURN
#undef SUBMIT_TASK_WITH_PARAMS_AND_RETURN
#undef SUBMIT_TASK_NO_PARAMS
#undef SUBMIT_TASK_WITH_PARAMS
