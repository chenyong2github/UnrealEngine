// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"
#include "WebRTCIncludes.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "Containers/UnrealString.h"
#include "Containers/Map.h"
#include "Dom/JsonObject.h"
#include "Delegates/IDelegateInstance.h"
#include "Engine/EngineTypes.h"
#include "PixelStreamingPlayerId.h"

class IWebSocket;

namespace UE {
	namespace PixelStreaming {

		// callback interface for `FSignallingServerConnection`
		class FSignallingServerConnectionObserver
		{
		public:
			virtual ~FSignallingServerConnectionObserver()
			{
			}

			virtual void OnSignallingServerDisconnected() = 0;
			virtual void OnConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config) = 0;
			virtual void OnSessionDescription(FPixelStreamingPlayerId PlayerId, webrtc::SdpType Type, const FString& Sdp)
			{
				unimplemented();
			}

			// Streamer-only
			virtual void OnRemoteIceCandidate(FPixelStreamingPlayerId PlayerId, const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp)
			{
				unimplemented();
			}
			virtual void OnPlayerConnected(FPixelStreamingPlayerId PlayerId, int Flags)
			{
				unimplemented();
			}
			virtual void OnPlayerDisconnected(FPixelStreamingPlayerId PlayerId)
			{
				unimplemented();
			}

			// Player-only
			virtual void OnRemoteIceCandidate(TUniquePtr<webrtc::IceCandidateInterface> Candidate)
			{
				unimplemented();
			}
			virtual void OnPlayerCount(uint32 Count)
			{
				unimplemented();
			}
		};

		class FSignallingServerConnection final
		{
		public:
			explicit FSignallingServerConnection(FSignallingServerConnectionObserver& Observer, const FString& StreamerId);
			~FSignallingServerConnection();
			void Connect(const FString& Url);
			void Disconnect();

			void SendOffer(FPixelStreamingPlayerId PlayerId, const webrtc::SessionDescriptionInterface& SDP);
			void SendAnswer(FPixelStreamingPlayerId PlayerId, const webrtc::SessionDescriptionInterface& SDP);
			void SendIceCandidate(const webrtc::IceCandidateInterface& IceCandidate);
			void SendIceCandidate(FPixelStreamingPlayerId PlayerId, const webrtc::IceCandidateInterface& IceCandidate);
			void SendDisconnectPlayer(FPixelStreamingPlayerId PlayerId, const FString& Reason);

		private:
			void KeepAlive();

			void OnConnected();
			void OnConnectionError(const FString& Error);
			void OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
			void OnMessage(const FString& Msg);

			using FJsonObjectPtr = TSharedPtr<FJsonObject>;

			void OnIdRequested();
			void OnConfig(const FJsonObjectPtr& Json);
			void OnSessionDescription(const FJsonObjectPtr& Json);
			void OnStreamerIceCandidate(const FJsonObjectPtr& Json);
			void OnPlayerIceCandidate(const FJsonObjectPtr& Json);
			void OnPlayerCount(const FJsonObjectPtr& Json);
			void OnPlayerConnected(const FJsonObjectPtr& Json);
			void OnPlayerDisconnected(const FJsonObjectPtr& Json);
			void SetPlayerIdJson(FJsonObjectPtr& JsonObject, FPixelStreamingPlayerId PlayerId);
			bool GetPlayerIdJson(const FJsonObjectPtr& Json, FPixelStreamingPlayerId& OutPlayerId);

		private:
			FSignallingServerConnectionObserver& Observer;
			FString StreamerId;

			FDelegateHandle OnConnectedHandle;
			FDelegateHandle OnConnectionErrorHandle;
			FDelegateHandle OnClosedHandle;
			FDelegateHandle OnMessageHandle;

			/** Handle for efficient management of KeepAlive timer */
			FTimerHandle TimerHandle_KeepAlive;
			const float KEEP_ALIVE_INTERVAL = 60.0f;

			TSharedPtr<IWebSocket> WS;
		};
	}
}
