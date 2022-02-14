// Copyright Epic Games, Inc. All Rights Reserved.

#include "SignallingServerConnection.h"
#include "ToStringExtensions.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "Engine/World.h"
#include "Serialization/JsonSerializer.h"
#include "Settings.h"
#include "TimerManager.h"
#include "PixelStreamingDelegates.h"
#include "PixelStreamingProtocolDefs.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPixelStreamingSS, Log, VeryVerbose);
DEFINE_LOG_CATEGORY(LogPixelStreamingSS);

namespace UE::PixelStreaming
{
	FSignallingServerConnection::FSignallingServerConnection(FSignallingServerConnectionObserver& InObserver, FString InStreamerId)
		: Observer(InObserver), StreamerId(InStreamerId)
	{
		RegisterHandler("identify", [this](FJsonObjectPtr JsonMsg) { OnIdRequested(); });
		RegisterHandler("config", [this](FJsonObjectPtr JsonMsg) { OnConfig(JsonMsg); });
		RegisterHandler("offer", [this](FJsonObjectPtr JsonMsg) { OnSessionDescription(JsonMsg); });
		RegisterHandler("answer", [this](FJsonObjectPtr JsonMsg) { OnSessionDescription(JsonMsg); });
		RegisterHandler("iceCandidate", [this](FJsonObjectPtr JsonMsg) { OnIceCandidate(JsonMsg); });
		RegisterHandler("ping", [this](FJsonObjectPtr JsonMsg) { /* nothing */ });
		RegisterHandler("pong", [this](FJsonObjectPtr JsonMsg) { /* nothing */ });
		RegisterHandler("playerCount", [this](FJsonObjectPtr JsonMsg) { OnPlayerCount(JsonMsg); });
		RegisterHandler("playerConnected", [this](FJsonObjectPtr JsonMsg) { OnPlayerConnected(JsonMsg); });
		RegisterHandler("playerDisconnected", [this](FJsonObjectPtr JsonMsg) { OnPlayerDisconnected(JsonMsg); });
		RegisterHandler("streamerDataChannels", [this](FJsonObjectPtr JsonMsg) { OnStreamerDataChannels(JsonMsg); });
		RegisterHandler("peerDataChannels", [this](FJsonObjectPtr JsonMsg) { OnPeerDataChannels(JsonMsg); });
	}

	FSignallingServerConnection::~FSignallingServerConnection()
	{
		Disconnect();
	}

	// This function returns the instance ID to the signalling server. This is useful for identifying individual instances in scalable cloud deployments
	void FSignallingServerConnection::OnIdRequested()
	{
		FJsonObjectPtr Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("type"), TEXT("endpointId"));
		Json->SetStringField(TEXT("id"), StreamerId);
		FString Msg = ToString(Json, false);
		UE_LOG(LogPixelStreamingSS, Verbose, TEXT("-> SS: endpointId\n%s"), *Msg);
		WS->Send(Msg);
	}

	void FSignallingServerConnection::Connect(const FString& Url)
	{
		// Already have a websocket connection, no need to make another one
		if (WS)
		{
			return;
		}

		WS = FWebSocketsModule::Get().CreateWebSocket(Url, TEXT(""));

		OnConnectedHandle = WS->OnConnected().AddLambda([this]() { OnConnected(); });
		OnConnectionErrorHandle = WS->OnConnectionError().AddLambda([this](const FString& Error) { OnConnectionError(Error); });
		OnClosedHandle = WS->OnClosed().AddLambda([this](int32 StatusCode, const FString& Reason, bool bWasClean) { OnClosed(StatusCode, Reason, bWasClean); });
		OnMessageHandle = WS->OnMessage().AddLambda([this](const FString& Msg) { OnMessage(Msg); });

		UE_LOG(LogPixelStreamingSS, Log, TEXT("Connecting to SS %s"), *Url);
		WS->Connect();
	}

	void FSignallingServerConnection::Disconnect()
	{
		if (!WS)
		{
			return;
		}

		if (!IsEngineExitRequested())
		{
			GWorld->GetTimerManager().ClearTimer(TimerHandle_KeepAlive);
		}

		WS->OnConnected().Remove(OnConnectedHandle);
		WS->OnConnectionError().Remove(OnConnectionErrorHandle);
		WS->OnClosed().Remove(OnClosedHandle);
		WS->OnMessage().Remove(OnMessageHandle);

		WS->Close();
		WS = nullptr;
	}

	bool FSignallingServerConnection::IsConnected() const
	{
		return WS != nullptr && WS.IsValid() && WS->IsConnected();
	}

	void FSignallingServerConnection::SendOffer(FPixelStreamingPlayerId PlayerId, const webrtc::SessionDescriptionInterface& SDP)
	{
		FJsonObjectPtr OfferJson = MakeShared<FJsonObject>();
		OfferJson->SetStringField(TEXT("type"), TEXT("offer"));
		SetPlayerIdJson(OfferJson, PlayerId);

		std::string SdpAnsi;
		SDP.ToString(&SdpAnsi);
		FString SdpStr = ToString(SdpAnsi);
		OfferJson->SetStringField(TEXT("sdp"), SdpStr);

		UE_LOG(LogPixelStreamingSS, Verbose, TEXT("-> SS: offer\n%s"), *SdpStr);

		WS->Send(ToString(OfferJson, false));
	}

	void FSignallingServerConnection::SendAnswer(FPixelStreamingPlayerId PlayerId, const webrtc::SessionDescriptionInterface& SDP)
	{
		FJsonObjectPtr AnswerJson = MakeShared<FJsonObject>();
		AnswerJson->SetStringField(TEXT("type"), TEXT("answer"));
		SetPlayerIdJson(AnswerJson, PlayerId);

		std::string SdpAnsi;
		verifyf(SDP.ToString(&SdpAnsi), TEXT("Failed to serialise local SDP"));
		FString SdpStr = ToString(SdpAnsi);
		AnswerJson->SetStringField(TEXT("sdp"), SdpStr);

		UE_LOG(LogPixelStreamingSS, Verbose, TEXT("-> SS: answer\n%s"), *SdpStr);

		WS->Send(ToString(AnswerJson, false));
	}

	void FSignallingServerConnection::SetPlayerIdJson(FJsonObjectPtr& JsonObject, FPixelStreamingPlayerId PlayerId)
	{
		bool bSendAsInteger = Settings::CVarSendPlayerIdAsInteger.GetValueOnAnyThread();
		if (bSendAsInteger)
		{
			int32 PlayerIdAsInt = PlayerIdToInt(PlayerId);
			JsonObject->SetNumberField(TEXT("playerId"), PlayerIdAsInt);
		}
		else
		{
			JsonObject->SetStringField(TEXT("playerId"), PlayerId);
		}
	}

	bool FSignallingServerConnection::GetPlayerIdJson(const FJsonObjectPtr& Json, FPixelStreamingPlayerId& OutPlayerId, const FString& FieldId)
	{
		bool bSendAsInteger = Settings::CVarSendPlayerIdAsInteger.GetValueOnAnyThread();
		if (bSendAsInteger)
		{
			uint32 PlayerIdInt;
			if (Json->TryGetNumberField(FieldId, PlayerIdInt))
			{
				OutPlayerId = ToPlayerId(PlayerIdInt);
				return true;
			}
		}
		else if (Json->TryGetStringField(FieldId, OutPlayerId))
		{
			return true;
		}
		return false;
	}

	void FSignallingServerConnection::SendIceCandidate(const webrtc::IceCandidateInterface& IceCandidate)
	{
		FJsonObjectPtr IceCandidateJson = MakeShared<FJsonObject>();

		IceCandidateJson->SetStringField(TEXT("type"), TEXT("iceCandidate"));

		FJsonObjectPtr CandidateJson = MakeShared<FJsonObject>();
		CandidateJson->SetStringField(TEXT("sdpMid"), IceCandidate.sdp_mid().c_str());
		CandidateJson->SetNumberField(TEXT("sdpMLineIndex"), static_cast<double>(IceCandidate.sdp_mline_index()));

		std::string CandidateAnsi;
		verifyf(IceCandidate.ToString(&CandidateAnsi), TEXT("Failed to serialize IceCandidate"));
		FString CandidateStr = ToString(CandidateAnsi);
		CandidateJson->SetStringField(TEXT("candidate"), CandidateStr);

		IceCandidateJson->SetObjectField(TEXT("candidate"), CandidateJson);

		UE_LOG(LogPixelStreamingSS, Verbose, TEXT("-> SS: ice-candidate\n%s"), *ToString(IceCandidateJson));

		WS->Send(ToString(IceCandidateJson, false));
	}

	void FSignallingServerConnection::SendIceCandidate(FPixelStreamingPlayerId PlayerId, const webrtc::IceCandidateInterface& IceCandidate)
	{
		FJsonObjectPtr IceCandidateJson = MakeShared<FJsonObject>();

		IceCandidateJson->SetStringField(TEXT("type"), TEXT("iceCandidate"));
		SetPlayerIdJson(IceCandidateJson, PlayerId);

		FJsonObjectPtr CandidateJson = MakeShared<FJsonObject>();
		CandidateJson->SetStringField(TEXT("sdpMid"), IceCandidate.sdp_mid().c_str());
		CandidateJson->SetNumberField(TEXT("sdpMLineIndex"), static_cast<double>(IceCandidate.sdp_mline_index()));

		std::string CandidateAnsi;
		verifyf(IceCandidate.ToString(&CandidateAnsi), TEXT("Failed to serialize IceCandidate"));
		FString CandidateStr = ToString(CandidateAnsi);
		CandidateJson->SetStringField(TEXT("candidate"), CandidateStr);

		IceCandidateJson->SetObjectField(TEXT("candidate"), CandidateJson);

		UE_LOG(LogPixelStreamingSS, Verbose, TEXT("-> SS: iceCandidate\n%s"), *ToString(IceCandidateJson));

		WS->Send(ToString(IceCandidateJson, false));
	}

	void FSignallingServerConnection::KeepAlive()
	{
		FJsonObjectPtr Json = MakeShared<FJsonObject>();
		double unixTime = FDateTime::UtcNow().ToUnixTimestamp();
		Json->SetStringField(TEXT("type"), TEXT("ping"));
		Json->SetNumberField(TEXT("time"), unixTime);
		FString Msg = ToString(Json, false);
		if (WS != nullptr && WS.IsValid() && WS->IsConnected())
		{
			WS->Send(Msg);
		}
	}

	void FSignallingServerConnection::SendDisconnectPlayer(FPixelStreamingPlayerId PlayerId, const FString& Reason)
	{
		FJsonObjectPtr Json = MakeShared<FJsonObject>();

		Json->SetStringField(TEXT("type"), TEXT("disconnectPlayer"));
		SetPlayerIdJson(Json, PlayerId);
		Json->SetStringField(TEXT("reason"), Reason);

		FString Msg = ToString(Json, false);
		UE_LOG(LogPixelStreamingSS, Verbose, TEXT("-> SS: iceCandidate\n%s"), *Msg);

		WS->Send(Msg);
	}

	void FSignallingServerConnection::SendAnswer(const webrtc::SessionDescriptionInterface& SDP)
	{
		FJsonObjectPtr AnswerJson = MakeShared<FJsonObject>();
		AnswerJson->SetStringField(TEXT("type"), TEXT("answer"));

		FString SdpStr = ToString(SDP);
		AnswerJson->SetStringField(TEXT("sdp"), ToString(SDP));

		UE_LOG(LogPixelStreamingSS, Verbose, TEXT("-> SS: answer\n%s"), *SdpStr);

		WS->Send(ToString(AnswerJson, false));
	}

	void FSignallingServerConnection::OnConnected()
	{
		UE_LOG(LogPixelStreamingSS, Log, TEXT("Connected to SS"));

		Observer.OnSignallingServerConnected();

		//Send message to keep connection alive every 60 seconds
		GWorld->GetTimerManager().SetTimer(TimerHandle_KeepAlive, std::bind(&FSignallingServerConnection::KeepAlive, this), KEEP_ALIVE_INTERVAL, true);

		if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
		{
			Delegates->OnConnecedToSignallingServer.Broadcast();
			Delegates->OnConnecedToSignallingServerNative.Broadcast();
		}
	}

	void FSignallingServerConnection::OnConnectionError(const FString& Error)
	{
		UE_LOG(LogPixelStreamingSS, Error, TEXT("Failed to connect to SS: %s"), *Error);

		Observer.OnSignallingServerDisconnected();

		GWorld->GetTimerManager().ClearTimer(TimerHandle_KeepAlive);

		if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
		{
			Delegates->OnDisconnectedFromSignallingServer.Broadcast();
			Delegates->OnDisconnectedFromSignallingServerNative.Broadcast();
		}
	}

	void FSignallingServerConnection::OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
	{
		UE_LOG(LogPixelStreamingSS, Log, TEXT("Connection to SS closed: \n\tstatus %d\n\treason: %s\n\twas clean: %s"), StatusCode, *Reason, bWasClean ? TEXT("true") : TEXT("false"));

		Observer.OnSignallingServerDisconnected();

		GWorld->GetTimerManager().ClearTimer(TimerHandle_KeepAlive);

		if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
		{
			Delegates->OnDisconnectedFromSignallingServer.Broadcast();
			Delegates->OnDisconnectedFromSignallingServerNative.Broadcast();
		}
	}

	void FSignallingServerConnection::OnMessage(const FString& Msg)
	{
		FJsonObjectPtr JsonMsg;
		const auto JsonReader = TJsonReaderFactory<TCHAR>::Create(Msg);

		if (!FJsonSerializer::Deserialize(JsonReader, JsonMsg))
		{
			FatalError(TEXT("Failed to parse SS message:\n%s"), *Msg);
			return;
		}

		FString MsgType;
		if (!JsonMsg->TryGetStringField(TEXT("type"), MsgType))
		{
			FatalError(TEXT("Cannot find `type` field in SS message:\n%s"), *Msg);
			return;
		}

		TFunction<void(FJsonObjectPtr)>* Handler = MessageHandlers.Find(MsgType);
		if (Handler != nullptr)
		{
			(*Handler)(JsonMsg);
		}
		else
		{
			FatalError(TEXT("Unsupported message `%s` received from SS"), *MsgType);
		}
	}

	void FSignallingServerConnection::RegisterHandler(const FString& messageType, const TFunction<void(FJsonObjectPtr)>& handler)
	{
		MessageHandlers.Add(messageType, handler);
	}

	void FSignallingServerConnection::OnConfig(const FJsonObjectPtr& Json)
	{
		// SS sends `config` that looks like:
		// `{peerConnectionOptions: { 'iceServers': [{'urls': ['stun:34.250.222.95:19302', 'turn:34.250.222.95:19303']}] }}`
		// where `peerConnectionOptions` is `RTCConfiguration` (except in native `RTCConfiguration` "iceServers" = "servers").
		// As `RTCConfiguration` doesn't implement parsing from a string (or `ToString` method),
		// we just get `stun`/`turn` URLs from it and ignore other options

		const TSharedPtr<FJsonObject>* PeerConnectionOptions;
		if (!Json->TryGetObjectField(TEXT("peerConnectionOptions"), PeerConnectionOptions))
		{
			FatalError(TEXT("Cannot find `peerConnectionOptions` field in SS config\n%s"), *ToString(Json));
		}

		webrtc::PeerConnectionInterface::RTCConfiguration RTCConfig;

		const TArray<TSharedPtr<FJsonValue>>* IceServers;
		if ((*PeerConnectionOptions)->TryGetArrayField(TEXT("iceServers"), IceServers))
		{
			for (const TSharedPtr<FJsonValue>& IceServerVal : *IceServers)
			{
				const TSharedPtr<FJsonObject>* IceServerJson;
				if (!IceServerVal->TryGetObject(IceServerJson))
				{
					FatalError(TEXT("Failed to parse SS config: `iceServer` - not an object\n%s"), *ToString(*PeerConnectionOptions));
				}

				RTCConfig.servers.push_back(webrtc::PeerConnectionInterface::IceServer{});
				webrtc::PeerConnectionInterface::IceServer& IceServer = RTCConfig.servers.back();

				TArray<FString> Urls;
				if ((*IceServerJson)->TryGetStringArrayField(TEXT("urls"), Urls))
				{
					for (const FString& Url : Urls)
					{
						IceServer.urls.push_back(ToString(Url));
					}
				}
				else
				{
					// in the RTC Spec, "urls" can be an array or a single string
					// https://www.w3.org/TR/webrtc/#dictionary-rtciceserver-members
					FString UrlsSingle;
					if ((*IceServerJson)->TryGetStringField(TEXT("urls"), UrlsSingle))
					{
						IceServer.urls.push_back(ToString(UrlsSingle));
					}
				}

				FString Username;
				if ((*IceServerJson)->TryGetStringField(TEXT("username"), Username))
				{
					IceServer.username = ToString(Username);
				}

				FString Credential;
				if ((*IceServerJson)->TryGetStringField(TEXT("credential"), Credential))
				{
					IceServer.password = ToString(Credential);
				}
			}
		}

		// force `UnifiedPlan` as we control both ends of WebRTC streaming
		RTCConfig.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;

		Observer.OnConfig(RTCConfig);
	}

	void FSignallingServerConnection::OnSessionDescription(const FJsonObjectPtr& Json)
	{
		webrtc::SdpType Type = Json->GetStringField(TEXT("type")) == TEXT("offer") ? webrtc::SdpType::kOffer : webrtc::SdpType::kAnswer;

		FString Sdp;
		if (!Json->TryGetStringField(TEXT("sdp"), Sdp))
		{
			FatalError(TEXT("Cannot find `sdp` in Streamer's answer\n%s"), *ToString(Json));
		}

		FPixelStreamingPlayerId PlayerId;
		bool bGotPlayerId = GetPlayerIdJson(Json, PlayerId);
		if (!bGotPlayerId)
		{
			Observer.OnSessionDescription(Type, Sdp);
		}
		else
		{
			Observer.OnSessionDescription(PlayerId, Type, Sdp);
		}
	}

	void FSignallingServerConnection::OnIceCandidate(const FJsonObjectPtr& Json)
	{
		FPixelStreamingPlayerId PlayerId;
		bool bGotPlayerId = GetPlayerIdJson(Json, PlayerId);

		const FJsonObjectPtr* CandidateJson;
		if (!Json->TryGetObjectField(TEXT("candidate"), CandidateJson))
		{
			PlayerError(PlayerId, TEXT("Failed to get `candiate` from remote `iceCandidate` message\n%s"), *ToString(Json));
		}

		FString SdpMid;
		if (!(*CandidateJson)->TryGetStringField(TEXT("sdpMid"), SdpMid))
		{
			PlayerError(PlayerId, TEXT("Failed to get `sdpMid` from remote `iceCandidate` message\n%s"), *ToString(Json));
		}

		int32 SdpMLineIndex = 0;
		if (!(*CandidateJson)->TryGetNumberField(TEXT("sdpMlineIndex"), SdpMLineIndex))
		{
			PlayerError(PlayerId, TEXT("Failed to get `sdpMlineIndex` from remote `iceCandidate` message\n%s"), *ToString(Json));
		}

		FString CandidateStr;
		if (!(*CandidateJson)->TryGetStringField(TEXT("candidate"), CandidateStr))
		{
			PlayerError(PlayerId, TEXT("Failed to get `candidate` from remote `iceCandidate` message\n%s"), *ToString(Json));
		}

		if (bGotPlayerId)
		{
			Observer.OnRemoteIceCandidate(PlayerId, SdpMid, SdpMLineIndex, CandidateStr);
		}
		else
		{
			Observer.OnRemoteIceCandidate(SdpMid, SdpMLineIndex, CandidateStr);
		}
	}

	void FSignallingServerConnection::OnPlayerCount(const FJsonObjectPtr& Json)
	{
		uint32 Count;
		if (!Json->TryGetNumberField(TEXT("count"), Count))
		{
			FatalError(TEXT("Failed to get `count` from `playerCount` message\n%s"), *ToString(Json));
		}

		Observer.OnPlayerCount(Count);
	}

	void FSignallingServerConnection::OnPlayerConnected(const FJsonObjectPtr& Json)
	{
		FPixelStreamingPlayerId PlayerId;
		bool bGotPlayerId = GetPlayerIdJson(Json, PlayerId);
		if (!bGotPlayerId)
		{
			FatalError(TEXT("Failed to get `playerId` from `join` message\n%s"), *ToString(Json));
		}
		int Flags = 0;

		// Default to always making datachannel, unless explicitly set to false.
		bool bMakeDataChannel = true;
		Json->TryGetBoolField(TEXT("datachannel"), bMakeDataChannel);

		// Default peer is not an SFU, unless explictly set as SFU
		bool bIsSFU = false;
		Json->TryGetBoolField(TEXT("sfu"), bIsSFU);

		Flags |= bMakeDataChannel ? Protocol::EPlayerFlags::PSPFlag_SupportsDataChannel : 0;
		Flags |= bIsSFU ? Protocol::EPlayerFlags::PSPFlag_IsSFU : 0;
		Observer.OnPlayerConnected(PlayerId, Flags);
	}

	void FSignallingServerConnection::OnPlayerDisconnected(const FJsonObjectPtr& Json)
	{
		FPixelStreamingPlayerId PlayerId;
		bool bGotPlayerId = GetPlayerIdJson(Json, PlayerId);
		if (!bGotPlayerId)
		{
			FatalError(TEXT("Failed to get `playerId` from `playerDisconnected` message\n%s"), *ToString(Json));
		}

		Observer.OnPlayerDisconnected(PlayerId);
	}

	void FSignallingServerConnection::OnStreamerDataChannels(const FJsonObjectPtr& Json)
	{
		FPixelStreamingPlayerId SFUId;
		bool bSuccess = GetPlayerIdJson(Json, SFUId, TEXT("sfuId"));
		if (!bSuccess)
		{
			FatalError(TEXT("Failed to get `sfuId` from `streamerDataChannels` message\n%s"), *ToString(Json));
		}

		FPixelStreamingPlayerId PlayerId;
		bSuccess = GetPlayerIdJson(Json, PlayerId, TEXT("playerId"));
		if (!bSuccess)
		{
			FatalError(TEXT("Failed to get `playerId` from `streamerDataChannels` message\n%s"), *ToString(Json));
		}

		int32 SendStreamId;
		bSuccess = Json->TryGetNumberField(TEXT("sendStreamId"), SendStreamId);
		if (!bSuccess)
		{
			FatalError(TEXT("Failed to get `sendStreamId` from `streamerDataChannels` message\n%s"), *ToString(Json));
		}

		int32 RecvStreamId;
		bSuccess = Json->TryGetNumberField(TEXT("recvStreamId"), RecvStreamId);
		if (!bSuccess)
		{
			FatalError(TEXT("Failed to get `recvStreamId` from `streamerDataChannels` message\n%s"), *ToString(Json));
		}

		Observer.OnStreamerDataChannels(SFUId, PlayerId, SendStreamId, RecvStreamId);
	}

	void FSignallingServerConnection::OnPeerDataChannels(const FJsonObjectPtr& Json)
	{
		int32 SendStreamId = 0;
		if (!Json->TryGetNumberField(TEXT("sendStreamId"), SendStreamId))
		{
			FatalError(TEXT("Failed to get `sendStreamId` from remote `peerDataChannels` message\n%s"), *ToString(Json));
			return;
		}
		int32 RecvStreamId = 0;
		if (!Json->TryGetNumberField(TEXT("recvStreamId"), RecvStreamId))
		{
			FatalError(TEXT("Failed to get `recvStreamId` from remote `peerDataChannels` message\n%s"), *ToString(Json));
			return;
		}
		Observer.OnPeerDataChannels(SendStreamId, RecvStreamId);
	}

	void FSignallingServerConnection::PlayerError(FPixelStreamingPlayerId PlayerId, const FString& Msg)
	{
		UE_LOG(LogPixelStreamingSS, Error, TEXT("player %s: %s"), *PlayerId, *Msg);
		SendDisconnectPlayer(PlayerId, Msg);
	}

	void FSignallingServerConnection::FatalError(const FString& Msg)
	{
		UE_LOG(LogPixelStreamingSS, Error, TEXT("%s"), *Msg);
		WS->Close(4000, Msg);
	}
} // namespace UE::PixelStreaming
