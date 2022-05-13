// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IPlayerSession.h"

namespace UE::PixelStreaming
{
	class FPlayerSessions
	{
	public:
		FPlayerSessions() = default;
		~FPlayerSessions() = default;

		// prevent copies
		FPlayerSessions(const FPlayerSessions&) = delete;
		FPlayerSessions& operator=(const FPlayerSessions&) = delete;

		TSharedPtr<IPlayerSession> CreatePlayerSession(FPixelStreamingPlayerId PlayerId,
			rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory,
			webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig,
			FSignallingServerConnection* SignallingServerConnection,
			int Flags);

		void CreateNewDataChannel(FPixelStreamingPlayerId SFUId,
			FPixelStreamingPlayerId PlayerId,
			int32 SendStreamId,
			int32 RecvStreamId,
			FSignallingServerConnection* SignallingServerConnection);

		int DeletePlayerSession(FPixelStreamingPlayerId PlayerId);
		void DeleteAllPlayerSessions(bool bEnginePreExit = false);

		int GetNumPlayers() const;

		bool IsQualityController(FPixelStreamingPlayerId PlayerId) const;
		void SetQualityController(FPixelStreamingPlayerId PlayerId);

		// do something with a single session
		void ForSession(FPixelStreamingPlayerId PlayerId, const TFunction<void(TSharedPtr<IPlayerSession>)>& Func) const
		{
			if (TSharedPtr<IPlayerSession> Session = GetPlayerSession(PlayerId))
			{
				Func(Session);
			}
		}

		// get something from a single session
		template <typename R, typename T>
		R ForSession(FPixelStreamingPlayerId PlayerId, T&& Func) const
		{
			if (TSharedPtr<IPlayerSession> Session = GetPlayerSession(PlayerId))
			{
				return Func(Session);
			}
			return R();
		}

		// do something with every session
		void ForEachSession(const TFunction<void(TSharedPtr<IPlayerSession>)>& Func);

	private:
		void AddSession(FPixelStreamingPlayerId PlayerId, TSharedPtr<IPlayerSession> Session);

		rtc::scoped_refptr<webrtc::PeerConnectionInterface> CreatePeerConnection(
			rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory,
			webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig,
			TSharedPtr<IPlayerSession> Session) const;

		int NumP2PPeers() const;
		bool GetFirstP2PPeer(FPixelStreamingPlayerId& OutPlayerId) const;
		TSharedPtr<IPlayerSession> GetPlayerSession(FPixelStreamingPlayerId PlayerId) const;

		mutable FCriticalSection PlayersCS;
		TMap<FPixelStreamingPlayerId, TSharedPtr<IPlayerSession>> Players;

		mutable FCriticalSection QualityControllerCS;
		FPixelStreamingPlayerId QualityControllingPlayer = INVALID_PLAYER_ID;
	};
} // namespace UE::PixelStreaming
