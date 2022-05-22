// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IPlayerSession.h"
#include "InputDevice.h"

class FPixelStreamingSignallingConnection;

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
			TSharedPtr<IPixelStreamingInputDevice> InputDevice,
			rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory,
			webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig,
			FPixelStreamingSignallingConnection* SignallingServerConnection,
			int Flags);

		void CreateSFUDataOnlyPeer(FPixelStreamingPlayerId SFUId,
			FPixelStreamingPlayerId PlayerId,
			TSharedPtr<IPixelStreamingInputDevice> InputDevice,
			int32 SendStreamId,
			int32 RecvStreamId,
			FPixelStreamingSignallingConnection* SignallingServerConnection);

		int DeletePlayerSession(FPixelStreamingPlayerId PlayerId);
		void DeleteAllPlayerSessions(bool bEnginePreExit = false);

		int GetNumPlayers() const;

		bool IsQualityController(FPixelStreamingPlayerId PlayerId) const;
		void SetQualityController(FPixelStreamingPlayerId PlayerId);
		bool IsInputController(FPixelStreamingPlayerId PlayerId) const;
		void SetInputController(FPixelStreamingPlayerId PlayerId);

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

		rtc::scoped_refptr<webrtc::DataChannelInterface> CreateDataChannel(
			rtc::scoped_refptr<webrtc::PeerConnectionInterface> PeerConnection,
			bool bIsSFU) const;

		int GetNumPeersMatching(TFunction<bool(FName)> SessionTypeMatchFunc) const;
		int NumNonSFUPeers() const;
		int NumP2PPeers() const;
		bool GetFirstPeerMatching(TFunction<bool(FName)> SessionTypeMatchFunc, FPixelStreamingPlayerId& OutPlayerId) const;
		bool GetFirstP2PPeer(FPixelStreamingPlayerId& OutPlayerId) const;
		bool GetFirstNonSFUPeer(FPixelStreamingPlayerId& OutPlayerId) const;
		TSharedPtr<IPlayerSession> GetPlayerSession(FPixelStreamingPlayerId PlayerId) const;

		mutable FCriticalSection PlayersCS;
		TMap<FPixelStreamingPlayerId, TSharedPtr<IPlayerSession>> Players;

		mutable FCriticalSection QualityControllerCS;
		FPixelStreamingPlayerId QualityControllingPlayer = INVALID_PLAYER_ID;

		mutable FCriticalSection InputControllerCS;
		FPixelStreamingPlayerId InputControllingPlayer = INVALID_PLAYER_ID;
	};
} // namespace UE::PixelStreaming
