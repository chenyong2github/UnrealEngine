// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerSessions.h"
#include "PixelStreamingDelegates.h"
#include "PixelStreamingPrivate.h"
#include "PlayerSession.h"
#include "PlayerSessionDataOnly.h"
#include "PlayerSessionSFU.h"
#include "Settings.h"

namespace UE::PixelStreaming
{
	TSharedPtr<IPlayerSession> FPlayerSessions::CreatePlayerSession(
		FPixelStreamingPlayerId PlayerId,
		rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory,
		webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig,
		FSignallingServerConnection* SignallingServerConnection, int Flags)
	{
		check(PeerConnectionFactory);

		// With unified plan, we get several calls to OnOffer, which in turn calls
		// this several times.
		// Therefore, we only try to create the player if not created already
		if (Players.Contains(PlayerId))
		{
			return nullptr;
		}

		UE_LOG(LogPixelStreaming, Log, TEXT("Creating player session for PlayerId=%s"), *PlayerId);

		// this is called from WebRTC signalling thread, the only thread where
		// `Players` map is modified, so no need to lock it
		bool bIsSFU = (Flags & Protocol::EPlayerFlags::PSPFlag_IsSFU) != 0;

		TSharedPtr<IPlayerSession> Session;
		if (bIsSFU)
		{
			Session = MakeShared<FPlayerSessionSFU>(this, SignallingServerConnection, PlayerId);
		}
		else
		{
			Session = MakeShared<FPlayerSession>(this, SignallingServerConnection, PlayerId);
		}

		// Note: this flag is very specifically calculated BEFORE the new session is added.
		// Peer only becomes quality controller if there is no other P2P peers already connected.
		bool bMakeQualityController = !bIsSFU && NumP2PPeers() == 0 && Session->GetSessionType() == FPlayerSession::Type;

		Session->SetPeerConnection(CreatePeerConnection(PeerConnectionFactory, PeerConnectionConfig, Session));
		AddSession(PlayerId, Session);

		if (bMakeQualityController)
		{
			SetQualityController(PlayerId);
		}

		if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
		{
			Delegates->OnNewConnection.Broadcast(PlayerId, bMakeQualityController);
			Delegates->OnNewConnectionNative.Broadcast(PlayerId, bMakeQualityController);
		}

		return Session;
	}

	void FPlayerSessions::AddSession(FPixelStreamingPlayerId PlayerId, TSharedPtr<IPlayerSession> Session)
	{
		FScopeLock Lock(&PlayersCS);
		Players.Add(PlayerId, Session);
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

	void FPlayerSessions::CreateNewDataChannel(
		FPixelStreamingPlayerId SFUId, FPixelStreamingPlayerId PlayerId,
		int32 SendStreamId, int32 RecvStreamId,
		FSignallingServerConnection* SignallingServerConnection)
	{
		if (auto ParentSession = StaticCastSharedPtr<FPlayerSessionSFU>(GetPlayerSession(SFUId)))
		{
			auto PeerConnection = &ParentSession->GetPeerConnection();
			auto NewSession = MakeShared<FPlayerSessionDataOnly>(this, SignallingServerConnection, PlayerId, PeerConnection, SendStreamId, RecvStreamId);
			ParentSession->AddChildSession(NewSession);

			{
				FScopeLock Lock(&PlayersCS);
				Players.Add(PlayerId, NewSession);
			}
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

		// The actual modification to the players map
		int RemainingCount = 0;
		{
			FScopeLock Lock(&PlayersCS);
			Players.Remove(PlayerId);
			RemainingCount = Players.Num();
		}

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

		return RemainingCount;
	}

	int FPlayerSessions::NumP2PPeers() const
	{
		FScopeLock Lock(&PlayersCS);
		int Count = 0;
		for (auto& Entry : Players)
		{
			FPixelStreamingPlayerId Id = Entry.Key;
			TSharedPtr<IPlayerSession> Session = Entry.Value;
			if (Session.IsValid() && Session->GetSessionType() == FPlayerSession::Type)
			{
				Count++;
			}
		}
		return Count;
	}

	bool FPlayerSessions::GetFirstP2PPeer(FPixelStreamingPlayerId& OutPlayerId) const
	{
		FScopeLock Lock(&PlayersCS);

		for (auto& Entry : Players)
		{
			FPixelStreamingPlayerId Id = Entry.Key;
			TSharedPtr<IPlayerSession> Session = Entry.Value;
			if (Session.IsValid() && Session->GetSessionType() == FPlayerSession::Type)
			{
				OutPlayerId = Id;
				return true;
			}
		}
		return false;
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
		}

		// Let any listeners know the quality controller has changed
		if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
		{
			Delegates->OnQualityControllerChangedNative.Broadcast(PlayerId);
		}

		UE_LOG(LogPixelStreaming, Log, TEXT("Quality controller is now PlayerId=%s."), *PlayerId);

		// Update quality controller status on the browser side too
		ForEachSession([&](TSharedPtr<IPlayerSession> Session) {
			bool bIsQualityController = Session->GetPlayerId() == QualityControllingPlayer;
			Session->SendQualityControlStatus(bIsQualityController); });
	}

	void FPlayerSessions::ForEachSession(const TFunction<void(TSharedPtr<IPlayerSession>)>& Func)
	{
		// We have to be careful here
		// We dont lock here because we can end up in a situation where Func
		// dispatches to another thread and that thread is already waiting to acquire
		// this lock and bam we have a deadlock. Instead we grab the current list of
		// keys and then iterate over our copy of those. We DO lock while checking and
		// pulling the session just in case Players is being modified between Contains
		// and when we actually pull it. luckily GetPlayerSession handles that for us.
		TSet<FPixelStreamingPlayerId> KeySet;
		Players.GetKeys(KeySet);
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
