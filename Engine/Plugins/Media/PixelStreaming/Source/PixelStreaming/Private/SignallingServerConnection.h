// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRTCIncludes.h"
#include "Dom/JsonObject.h"
#include "Engine/EngineTypes.h"
#include "PixelStreamingPlayerId.h"

class IWebSocket;

namespace UE::PixelStreaming
{
	// callback interface for `FSignallingServerConnection`
	class FSignallingServerConnectionObserver
	{
	public:
		virtual ~FSignallingServerConnectionObserver() {}

		virtual void OnSignallingServerConnected() = 0;
		virtual void OnSignallingServerDisconnected() = 0;

		virtual void OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config) = 0;

		// Streamer-only
		virtual void OnSessionDescription(FPixelStreamingPlayerId PlayerId, webrtc::SdpType Type, const FString& Sdp) { unimplemented(); }
		virtual void OnRemoteIceCandidate(FPixelStreamingPlayerId PlayerId, const FString& SdpMid, int SdpMLineIndex, const FString& Sdp) { unimplemented(); }
		virtual void OnPlayerConnected(FPixelStreamingPlayerId PlayerId, int Flags) { unimplemented(); }
		virtual void OnPlayerDisconnected(FPixelStreamingPlayerId PlayerId) { unimplemented(); }
		virtual void OnStreamerDataChannels(FPixelStreamingPlayerId SFUId, FPixelStreamingPlayerId PlayerId, int32 SendStreamId, int32 RecvStreamId) { unimplemented(); }

		// Player-only
		virtual void OnSessionDescription(webrtc::SdpType Type, const FString& Sdp) { unimplemented(); }
		virtual void OnRemoteIceCandidate(const FString& SdpMid, int SdpMLineIndex, const FString& Sdp) { unimplemented(); }
		virtual void OnPlayerCount(uint32 Count) { unimplemented(); }
		virtual void OnPeerDataChannels(int32 SendStreamId, int32 RecvStreamId) { unimplemented(); }
	};

	class FSignallingServerConnection final
	{
	public:
		explicit FSignallingServerConnection(FSignallingServerConnectionObserver& Observer, FString InStreamerId);
		~FSignallingServerConnection();

		void Connect(const FString& Url);
		void Disconnect();
		bool IsConnected() const;

		// Streamer interface
		void SendOffer(FPixelStreamingPlayerId PlayerId, const webrtc::SessionDescriptionInterface& SDP);
		void SendAnswer(FPixelStreamingPlayerId PlayerId, const webrtc::SessionDescriptionInterface& SDP);
		void SendIceCandidate(const webrtc::IceCandidateInterface& IceCandidate);
		void SendIceCandidate(FPixelStreamingPlayerId PlayerId, const webrtc::IceCandidateInterface& IceCandidate);
		void SendDisconnectPlayer(FPixelStreamingPlayerId PlayerId, const FString& Reason);

		// Player interface
		void SendAnswer(const webrtc::SessionDescriptionInterface& SDP);

	private:
		void KeepAlive();

		void OnConnected();
		void OnConnectionError(const FString& Error);
		void OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
		void OnMessage(const FString& Msg);

		using FJsonObjectPtr = TSharedPtr<FJsonObject>;
		void RegisterHandler(const FString& messageType, const TFunction<void(FJsonObjectPtr)>& handler);

		void OnIdRequested();
		void OnConfig(const FJsonObjectPtr& Json);
		void OnSessionDescription(const FJsonObjectPtr& Json);
		void OnIceCandidate(const FJsonObjectPtr& Json);
		void OnPlayerCount(const FJsonObjectPtr& Json);
		void OnPlayerConnected(const FJsonObjectPtr& Json);
		void OnPlayerDisconnected(const FJsonObjectPtr& Json);
		void OnStreamerDataChannels(const FJsonObjectPtr& Json);
		void OnPeerDataChannels(const FJsonObjectPtr& Json);
		void SetPlayerIdJson(FJsonObjectPtr& JsonObject, FPixelStreamingPlayerId PlayerId);
		bool GetPlayerIdJson(const FJsonObjectPtr& Json, FPixelStreamingPlayerId& OutPlayerId, const FString& FieldId = TEXT("playerId"));

		template <typename FmtType, typename... T>
		void PlayerError(FPixelStreamingPlayerId PlayerId, const FmtType& Msg, T... args)
		{
			const FString FormattedMsg = FString::Printf(Msg, args...);
			PlayerError(PlayerId, FormattedMsg);
		}
		void PlayerError(FPixelStreamingPlayerId PlayerId, const FString& Msg);

		template <typename FmtType, typename... T>
		void FatalError(const FmtType& Msg, T... args)
		{
			const FString FormattedMsg = FString::Printf(Msg, args...);
			FatalError(FormattedMsg);
		}
		void FatalError(const FString& Msg);

	private:
		TMap<FString, TFunction<void(FJsonObjectPtr)>> MessageHandlers;

		FSignallingServerConnectionObserver& Observer;

		FDelegateHandle OnConnectedHandle;
		FDelegateHandle OnConnectionErrorHandle;
		FDelegateHandle OnClosedHandle;
		FDelegateHandle OnMessageHandle;

		/** Handle for efficient management of KeepAlive timer */
		FTimerHandle TimerHandle_KeepAlive;
		const float KEEP_ALIVE_INTERVAL = 60.0f;

		TSharedPtr<IWebSocket> WS;

		FString StreamerId;
	};
} // namespace UE::PixelStreaming