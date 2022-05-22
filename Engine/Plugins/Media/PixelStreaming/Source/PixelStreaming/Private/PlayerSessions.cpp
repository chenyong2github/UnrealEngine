// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerSessions.h"
#include "PixelStreamingDelegates.h"
#include "PixelStreamingPrivate.h"
#include "PlayerSession.h"
#include "PlayerSessionDataOnly.h"
#include "PlayerSessionSFU.h"
#include "Settings.h"
#include "Stats.h"

namespace UE::PixelStreaming
{
	TSharedPtr<IPlayerSession> FPlayerSessions::CreatePlayerSession(
		FPixelStreamingPlayerId PlayerId,
		TSharedPtr<IPixelStreamingInputDevice> InputDevice,
		rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory,
		webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig,
		FPixelStreamingSignallingConnection* SignallingServerConnection, int Flags)
	{
		check(PeerConnectionFactory);

		// We don't want to create players we already have.
		if (Players.Contains(PlayerId))
		{
			return nullptr;
		}

		UE_LOG(LogPixelStreaming, Log, TEXT("Creating player session for PlayerId=%s"), *PlayerId);

		bool bIsSFU = (Flags & Protocol::EPlayerFlags::PSPFlag_IsSFU) != 0;

		TSharedPtr<IPlayerSession> Session;
		if (bIsSFU)
		{
			Session = MakeShared<FPlayerSessionSFU>(this, SignallingServerConnection, PlayerId, InputDevice);
		}
		else
		{
			Session = MakeShared<FPlayerSession>(this, SignallingServerConnection, PlayerId, InputDevice);
		}

		rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection = CreatePeerConnection(PeerConnectionFactory, PeerConnectionConfig, Session);
		Session->SetPeerConnection(PeerConnection);

		// Create a datachannel, if needed.
		const bool bSupportsDataChannel = (Flags & Protocol::EPlayerFlags::PSPFlag_SupportsDataChannel) != 0;
		if (bSupportsDataChannel)
		{
			Session->SetDataChannel(CreateDataChannel(PeerConnection, bIsSFU));
		}

		AddSession(PlayerId, Session);
		return Session;
	}

	rtc::scoped_refptr<webrtc::DataChannelInterface> FPlayerSessions::CreateDataChannel(
		rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection,
		bool bIsSFU) const
	{
		webrtc::DataChannelInit DataChannelConfig;
		DataChannelConfig.reliable = true;
		DataChannelConfig.ordered = true;
		if (bIsSFU)
		{
			DataChannelConfig.negotiated = true;
			DataChannelConfig.id = 1023;
		}

		return PeerConnection->CreateDataChannel("datachannel", &DataChannelConfig);
	}

	void FPlayerSessions::AddSession(FPixelStreamingPlayerId PlayerId, TSharedPtr<IPlayerSession> Session)
	{
		bool bIsSFU = Session->GetSessionType() == FPlayerSessionSFU::Type;

		// Note: this flag is very specifically calculated BEFORE the new session is added.
		// P2P peer only becomes quality controller if there is no other P2P peers already connected.
		bool bMakeQualityController = !bIsSFU && NumP2PPeers() == 0 && Session->GetSessionType() == FPlayerSession::Type;

		// Only time we automatically make a new peer the input controlling host is if they are the first peer (and not the SFU).
		bool bHostControlsInput = UE::PixelStreaming::Settings::GetInputControllerMode() == UE::PixelStreaming::Settings::EInputControllerMode::Host;
		bool bMakeInputController = bHostControlsInput && !bIsSFU && NumNonSFUPeers() == 0;

		{
			FScopeLock Lock(&PlayersCS);
			Players.Add(PlayerId, Session);
		}

		if (bMakeQualityController)
		{
			SetQualityController(PlayerId);
		}

		if (bMakeInputController)
		{
			SetInputController(PlayerId);
		}

		if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
		{
			Delegates->OnNewConnection.Broadcast(PlayerId, bMakeQualityController);
			Delegates->OnNewConnectionNative.Broadcast(PlayerId, bMakeQualityController);
		}
	}

	rtc::scoped_refptr<webrtc::PeerConnectionInterface> FPlayerSessions::CreatePeerConnection(
		rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory,
		webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig,
		TSharedPtr<IPlayerSession> Session) const
	{
		rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection =
			PeerConnectionFactory->CreatePeerConnection(PeerConnectionConfig, webrtc::PeerConnectionDependencies{ Session.Get() });

		if (!PeerConnection)
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to created PeerConnection. This may indicate you passed malformed peerConnectionOptions."));
			return nullptr;
		}

		// Setup suggested bitrate settings on the Peer Connection based on our CVars
		webrtc::BitrateSettings BitrateSettings;
		BitrateSettings.min_bitrate_bps = Settings::CVarPixelStreamingWebRTCMinBitrate.GetValueOnAnyThread();
		BitrateSettings.max_bitrate_bps = Settings::CVarPixelStreamingWebRTCMaxBitrate.GetValueOnAnyThread();
		BitrateSettings.start_bitrate_bps = Settings::CVarPixelStreamingWebRTCStartBitrate.GetValueOnAnyThread();
		PeerConnection->SetBitrate(BitrateSettings);

		return PeerConnection;
	}

	void FPlayerSessions::CreateSFUDataOnlyPeer(
		FPixelStreamingPlayerId SFUId,
		FPixelStreamingPlayerId PlayerId,
		TSharedPtr<IPixelStreamingInputDevice> InputDevice,
		int32 SendStreamId,
		int32 RecvStreamId,
		FPixelStreamingSignallingConnection* SignallingServerConnection)
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("PlayerId=%s - Creating new SFU dataonly peer with stream ids (sendId=%d recvId=%d)"), *PlayerId, SendStreamId, RecvStreamId);

		if (auto ParentSession = StaticCastSharedPtr<FPlayerSessionSFU>(GetPlayerSession(SFUId)))
		{
			TSharedPtr<FPlayerSessionDataOnly> NewSession = MakeShared<FPlayerSessionDataOnly>(this, PlayerId, InputDevice, ParentSession->GetPeerConnection(), SendStreamId, RecvStreamId);
			ParentSession->AddChildSession(NewSession);

			// This calls input/quality controller setters and any delegates that are required.
			AddSession(PlayerId, NewSession);

			// In FPlayerSessionDataOnly sessions the datachannel starts "open" and therefore we need to manually call state changed to
			// fire the relevant calls when open happens, such as sending input/quality control messages.
			NewSession->GetDataChannelObserver()->OnStateChange();
		}
	}

	int FPlayerSessions::DeletePlayerSession(FPixelStreamingPlayerId PlayerId)
	{
		TSharedPtr<IPlayerSession> PlayerSession = GetPlayerSession(PlayerId);
		if (!PlayerSession)
		{
			UE_LOG(LogPixelStreaming, VeryVerbose, TEXT("Failed to delete player %s - that player was not found."), *PlayerId);
			return GetNumPlayers();
		}

		bool bWasQualityController = IsQualityController(PlayerId);
		bool bWasInputController = IsInputController(PlayerId);
		bool bInputRestrictingMode = UE::PixelStreaming::Settings::GetInputControllerMode() == UE::PixelStreaming::Settings::EInputControllerMode::Host;

		// The actual modification to the players map
		int RemainingCount = 0;
		{
			// webrtc tries to call a method on the signalling thread when the peer connection
			// is destroyed in the session destructor, but if the signalling thread is waiting
			// for PlayersCS then this can cause a deadlock.
			// This temporary is just to delay the actual deletion until AFTER we release the CS
			TSharedPtr<IPlayerSession> ToBeDeleted;
			{
				FScopeLock Lock(&PlayersCS);
				ToBeDeleted = Players[PlayerId];
				Players.Remove(PlayerId);
				RemainingCount = Players.Num();
			}
		}

		if (bWasQualityController && RemainingCount > 0)
		{
			// Quality Controller session has been just removed, set quality control to
			// any of remaining player sessions (not SFU!)
			FPixelStreamingPlayerId OutPlayerId = INVALID_PLAYER_ID;
			bool bHasPlayer = GetFirstP2PPeer(OutPlayerId);
			if (bHasPlayer)
			{
				SetQualityController(OutPlayerId);
			}
		}

		if (bInputRestrictingMode && bWasInputController && RemainingCount > 0)
		{
			// Input controlling session has been just removed, set the input controller to
			// any of remaining player sessions (not SFU!)
			FPixelStreamingPlayerId OutPlayerId = INVALID_PLAYER_ID;
			bool bHasPlayer = GetFirstNonSFUPeer(OutPlayerId);
			if (bHasPlayer)
			{
				SetInputController(OutPlayerId);
			}
		}

		FStats* Stats = FStats::Get();
		if (Stats)
		{
			Stats->RemovePeersStats(PlayerId);
		}

		// Calling of delegates should be last to avoid side effects such as setting stats for removed peers after the delegate is fired
		UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates();
		if (Delegates && FModuleManager::Get().IsModuleLoaded("PixelStreaming"))
		{
			Delegates->OnClosedConnection.Broadcast(PlayerId, bWasQualityController);
			Delegates->OnClosedConnectionNative.Broadcast(PlayerId, bWasQualityController);

			if (RemainingCount == 0)
			{
				// Inform the application-specific blueprint that nobody is viewing or
				// interacting with the app. This is an opportunity to reset the app.
				Delegates->OnAllConnectionsClosed.Broadcast();
				Delegates->OnAllConnectionsClosedNative.Broadcast();
			}
		}

		return RemainingCount;
	}

	int FPlayerSessions::NumP2PPeers() const
	{
		return GetNumPeersMatching([](FName InSessionType) { return InSessionType == FPlayerSession::Type; });
	}

	int FPlayerSessions::NumNonSFUPeers() const
	{
		return GetNumPeersMatching([](FName InSessionType) { return InSessionType != FPlayerSessionSFU::Type; });
	}

	int FPlayerSessions::GetNumPeersMatching(TFunction<bool(FName)> SessionTypeMatchFunc) const
	{
		FScopeLock Lock(&PlayersCS);
		int Count = 0;
		for (auto& Entry : Players)
		{
			FPixelStreamingPlayerId Id = Entry.Key;
			TSharedPtr<IPlayerSession> Session = Entry.Value;
			if (Session.IsValid() && SessionTypeMatchFunc(Session->GetSessionType()))
			{
				Count++;
			}
		}
		return Count;
	}

	bool FPlayerSessions::GetFirstPeerMatching(TFunction<bool(FName)> SessionTypeMatchFunc, FPixelStreamingPlayerId& OutPlayerId) const
	{
		FScopeLock Lock(&PlayersCS);

		for (auto& Entry : Players)
		{
			FPixelStreamingPlayerId Id = Entry.Key;
			TSharedPtr<IPlayerSession> Session = Entry.Value;
			if (Session.IsValid() && SessionTypeMatchFunc(Session->GetSessionType()))
			{
				OutPlayerId = Id;
				return true;
			}
		}
		return false;
	}

	bool FPlayerSessions::GetFirstP2PPeer(FPixelStreamingPlayerId& OutPlayerId) const
	{
		return GetFirstPeerMatching([](FName InSessionType) { return InSessionType == FPlayerSession::Type; }, OutPlayerId);
	}

	bool FPlayerSessions::GetFirstNonSFUPeer(FPixelStreamingPlayerId& OutPlayerId) const
	{
		return GetFirstPeerMatching([](FName InSessionType) { return InSessionType != FPlayerSessionSFU::Type; }, OutPlayerId);
	}

	void FPlayerSessions::DeleteAllPlayerSessions(bool bEnginePreExit)
	{
		const FPixelStreamingPlayerId OldQualityController = QualityControllingPlayer;
		TSet<FPixelStreamingPlayerId> OldPlayerIds;
		Players.GetKeys(OldPlayerIds);

		// Do the modification to the critical parts separately since quality
		// controller lock is requested in the destructor of the video source base.
		{
			FScopeLock QualityControllerLock(&QualityControllerCS);
			QualityControllingPlayer = INVALID_PLAYER_ID;
		}
		{
			FScopeLock PlayersLock(&PlayersCS);
			Players.Empty();
		}

		// Notify all delegates of all the closed players
		bool bEngineShuttingDown = (bEnginePreExit || IsEngineExitRequested());
		UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates();
		if (Delegates && FModuleManager::Get().IsModuleLoaded("PixelStreaming") && !bEngineShuttingDown)
		{
			for (auto&& PlayerId : OldPlayerIds)
			{
				bool bWasQualityController = (OldQualityController == PlayerId);

				Delegates->OnClosedConnection.Broadcast(PlayerId, bWasQualityController);
				Delegates->OnClosedConnectionNative.Broadcast(PlayerId, bWasQualityController);
			}

			Delegates->OnAllConnectionsClosed.Broadcast();
			Delegates->OnAllConnectionsClosedNative.Broadcast();
		}
	}

	int FPlayerSessions::GetNumPlayers() const { return Players.Num(); }

	bool FPlayerSessions::IsQualityController(FPixelStreamingPlayerId PlayerId) const
	{
		FScopeLock Lock(&QualityControllerCS);
		return QualityControllingPlayer == PlayerId;
	}

	void FPlayerSessions::SetQualityController(FPixelStreamingPlayerId PlayerId)
	{
		TSharedPtr<IPlayerSession> PlayerSession = GetPlayerSession(PlayerId);

		if (!PlayerSession.IsValid())
		{
			return;
		}

		if (PlayerSession->GetSessionType() != FPlayerSession::Type)
		{
			return;
		}

		// The actual assignment of the quality controlling peer
		{
			FScopeLock Lock(&QualityControllerCS);
			QualityControllingPlayer = PlayerId;

			UE_LOG(LogPixelStreaming, Log, TEXT("Quality controller is now PlayerId=%s."), *PlayerId);

			// Update quality controller status on the browser side too
			ForEachSession([&QualityControllingPlayer = QualityControllingPlayer](TSharedPtr<IPlayerSession> Session) {
				bool bIsQualityController = Session->GetPlayerId() == QualityControllingPlayer;
				if (Session->GetDataChannelObserver() && Session->GetDataChannelObserver()->IsDataChannelOpen())
				{
					// Only send message if datachannel is already open (in dc observer we also send this message when channel opens)
					Session->SendQualityControlStatus(bIsQualityController);
				}
			});
		}

		// Let any listeners know the quality controller has changed
		if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
		{
			Delegates->OnQualityControllerChangedNative.Broadcast(PlayerId);
		}
	}

	bool FPlayerSessions::IsInputController(FPixelStreamingPlayerId PlayerId) const
	{
		UE::PixelStreaming::Settings::EInputControllerMode InputControllerMode = PixelStreaming::Settings::GetInputControllerMode();

		if (InputControllerMode == UE::PixelStreaming::Settings::EInputControllerMode::Host)
		{
			// Only in "Host" mode do we actually have to check if the peer can control input.
			FScopeLock Lock(&InputControllerCS);
			return InputControllingPlayer == PlayerId;
		}
		else
		{
			return true;
		}
	}

	void FPlayerSessions::SetInputController(FPixelStreamingPlayerId InInputControllingPlayerId)
	{
		UE::PixelStreaming::Settings::EInputControllerMode InputControllerMode = PixelStreaming::Settings::GetInputControllerMode();

		// No point setting input controller when running in "Any" mode where any peer can control input.
		if (InputControllerMode == UE::PixelStreaming::Settings::EInputControllerMode::Any)
		{
			return;
		}

		TSharedPtr<IPlayerSession> PlayerSession = GetPlayerSession(InInputControllingPlayerId);

		if (!PlayerSession.IsValid())
		{
			return;
		}

		// SFU cannot be the input controller
		if (PlayerSession->GetSessionType() == FPlayerSessionSFU::Type)
		{
			return;
		}

		// The actual assignment of the input controlling peer
		{
			FScopeLock Lock(&InputControllerCS);
			InputControllingPlayer = InInputControllingPlayerId;

			UE_LOG(LogPixelStreaming, Log, TEXT("Input controller is now PlayerId=%s."), *InInputControllingPlayerId);


			// Update quality controller status on the browser side too
			ForEachSession([&InputControllingPlayer = InputControllingPlayer](TSharedPtr<IPlayerSession> Session) {
				bool bIsInputController = Session->GetPlayerId() == InputControllingPlayer;
				if (Session->GetDataChannelObserver() && Session->GetDataChannelObserver()->IsDataChannelOpen())
				{
					// Only send message if datachannel is already open (in dc observer we also send this message when channel opens)
					Session->SendInputControlStatus(bIsInputController);
				}
			});
		}

		// Todo (Luke): Call delegates to inform them about the new "host"
		// if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
		// {
		// 	Delegates->OnInputControllerChangedNative.Broadcast(PlayerId);
		// }
	}

	void FPlayerSessions::ForEachSession(const TFunction<void(TSharedPtr<IPlayerSession>)>& Func)
	{
		// We have to be careful here
		// We dont lock when calling `Func` because we can end up in a situation where Func
		// dispatches to another thread and that thread is already waiting to acquire
		// this lock and bam we have a deadlock. Instead we grab the current list of
		// keys and then iterate over our copy of those. We DO lock while checking and
		// pulling the session just in case Players is being modified between Contains
		// and when we actually pull it. luckily GetPlayerSession handles that for us.
		TSet<FPixelStreamingPlayerId> KeySet;
		{
			FScopeLock Lock(&PlayersCS);
			Players.GetKeys(KeySet);
		}
		for (auto&& PlayerId : KeySet)
		{
			if (TSharedPtr<IPlayerSession> Session = GetPlayerSession(PlayerId))
			{
				Func(Session);
			}
		}
	}

	TSharedPtr<IPlayerSession> FPlayerSessions::GetPlayerSession(FPixelStreamingPlayerId PlayerId) const
	{
		FScopeLock Lock(&PlayersCS);
		if (Players.Contains(PlayerId))
		{
			return Players[PlayerId];
		}
		return nullptr;
	}
} // namespace UE::PixelStreaming
