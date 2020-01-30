// Copyright Epic Games, Inc. All Rights Reserved.

#include "SignallingServerConnection.h"
#include "Utils.h"

#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "DOM/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Misc/AssertionMacros.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPixelStreamingSS, Log, VeryVerbose);
DEFINE_LOG_CATEGORY(LogPixelStreamingSS);

// This handles errors in Signalling Server (SS) messaging by logging them and disconnecting from SS
#define HANDLE_SS_ERROR(ErrorMsg, ...)\
	do\
	{\
		UE_LOG(LogPixelStreamingSS, Error, ErrorMsg, ##__VA_ARGS__);\
		WS->Close(4000, FString::Printf(ErrorMsg, ##__VA_ARGS__));\
		return;\
	}\
	while(false);

#define HANDLE_PLAYER_SS_ERROR(PlayerId, ErrorMsg, ...)\
	do\
	{\
		UE_LOG(LogPixelStreamingSS, Error, TEXT("player %d: ") ErrorMsg, PlayerId, ##__VA_ARGS__);\
		SendDisconnectPlayer(PlayerId, FString::Printf(ErrorMsg, ##__VA_ARGS__));\
		return;\
	}\
	while(false);

FSignallingServerConnection::FSignallingServerConnection(const FString& Url, FSignallingServerConnectionObserver& InObserver)
	: Observer(InObserver)
{
	WS = FWebSocketsModule::Get().CreateWebSocket(Url, TEXT("ws"));

	OnConnectedHandle = WS->OnConnected().AddLambda([this]() { OnConnected(); });
	OnConnectionErrorHandle = WS->OnConnectionError().AddLambda([this](const FString& Error) { OnConnectionError(Error); });
	OnClosedHandle = WS->OnClosed().AddLambda([this](int32 StatusCode, const FString& Reason, bool bWasClean) { OnClosed(StatusCode, Reason, bWasClean); });
	OnMessageHandle = WS->OnMessage().AddLambda([this](const FString& Msg) { OnMessage(Msg); });

	UE_LOG(LogPixelStreamingSS, Log, TEXT("Connecting to SS %s"), *Url);
	WS->Connect();
}

FSignallingServerConnection::~FSignallingServerConnection()
{
	if (!WS)
	{
		return;
	}

	WS->OnConnected().Remove(OnConnectedHandle);
	WS->OnConnectionError().Remove(OnConnectionErrorHandle);
	WS->OnClosed().Remove(OnClosedHandle);
	WS->OnMessage().Remove(OnMessageHandle);

	WS->Close();
}

void FSignallingServerConnection::SendOffer(const webrtc::SessionDescriptionInterface& SDP)
{
	auto OfferJson = MakeShared<FJsonObject>();
	OfferJson->SetStringField(TEXT("type"), TEXT("offer"));

	std::string SdpAnsi;
	SDP.ToString(&SdpAnsi);
	FString SdpStr = ToString(SdpAnsi);
	OfferJson->SetStringField(TEXT("sdp"), SdpStr);

	UE_LOG(LogPixelStreamingSS, Verbose, TEXT("-> SS: offer\n%s"), *SdpStr);

	WS->Send(ToString(OfferJson, false));
}

void FSignallingServerConnection::SendAnswer(uint32 PlayerId, const webrtc::SessionDescriptionInterface& SDP)
{
	auto AnswerJson = MakeShared<FJsonObject>();
	AnswerJson->SetStringField(TEXT("type"), TEXT("answer"));
	AnswerJson->SetNumberField(TEXT("playerId"), PlayerId);

	std::string SdpAnsi;
	verifyf(SDP.ToString(&SdpAnsi), TEXT("Failed to serialise local SDP"));
	FString SdpStr = ToString(SdpAnsi);
	AnswerJson->SetStringField(TEXT("sdp"), SdpStr);

	UE_LOG(LogPixelStreamingSS, Verbose, TEXT("-> SS: answer\n%s"), *SdpStr);

	WS->Send(ToString(AnswerJson, false));
}

void FSignallingServerConnection::SendIceCandidate(const webrtc::IceCandidateInterface& IceCandidate)
{
	auto IceCandidateJson = MakeShared<FJsonObject>();

	IceCandidateJson->SetStringField(TEXT("type"), TEXT("iceCandidate"));

	auto CandidateJson = MakeShared<FJsonObject>();
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

void FSignallingServerConnection::SendIceCandidate(uint32 PlayerId, const webrtc::IceCandidateInterface& IceCandidate)
{
	auto IceCandidateJson = MakeShared<FJsonObject>();

	IceCandidateJson->SetStringField(TEXT("type"), TEXT("iceCandidate"));
	IceCandidateJson->SetNumberField(TEXT("playerId"), PlayerId);

	auto CandidateJson = MakeShared<FJsonObject>();
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

void FSignallingServerConnection::SendDisconnectPlayer(uint32 PlayerId, const FString& Reason)
{
	auto Json = MakeShared<FJsonObject>();

	Json->SetStringField(TEXT("type"), TEXT("disconnectPlayer"));
	Json->SetNumberField(TEXT("playerId"), PlayerId);
	Json->SetStringField(TEXT("reason"), Reason);

	FString Msg = ToString(Json, false);
	UE_LOG(LogPixelStreamingSS, Verbose, TEXT("-> SS: iceCandidate\n%s"), *Msg);

	WS->Send(Msg);
}

void FSignallingServerConnection::OnConnected()
{
	UE_LOG(LogPixelStreamingSS, Log, TEXT("Connected to SS"));
}

void FSignallingServerConnection::OnConnectionError(const FString& Error)
{
	UE_LOG(LogPixelStreamingSS, Error, TEXT("Failed to connect to SS: %s"), *Error);
	Observer.OnSignallingServerDisconnected();
}

void FSignallingServerConnection::OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	UE_LOG(LogPixelStreamingSS, Log, TEXT("Connection to SS closed: \n\tstatus %d\n\treason: %s\n\twas clean: %d"), StatusCode, *Reason, bWasClean);
	Observer.OnSignallingServerDisconnected();
}

void FSignallingServerConnection::OnMessage(const FString& Msg)
{
	UE_LOG(LogPixelStreamingSS, Log, TEXT("<- SS: %s"), *Msg);

	TSharedPtr<FJsonObject> JsonMsg;
	auto JsonReader = TJsonReaderFactory<TCHAR>::Create(Msg);
	if (!FJsonSerializer::Deserialize(JsonReader, JsonMsg))
	{
		HANDLE_SS_ERROR(TEXT("Failed to parse SS message:\n%s"), *Msg);
	}

	FString MsgType;
	if (!JsonMsg->TryGetStringField(TEXT("type"), MsgType))
	{
		HANDLE_SS_ERROR(TEXT("Cannot find `type` field in SS message:\n%s"), *Msg);
	}

	if (MsgType == TEXT("config"))
	{
		OnConfig(JsonMsg);
	}
	else if (MsgType == TEXT("offer") || MsgType == TEXT("answer"))
	{
		OnSessionDescription(JsonMsg);
	}
	else if (MsgType == TEXT("iceCandidate"))
	{
		if (JsonMsg->HasField(TEXT("playerId")))
		{
			OnPlayerIceCandidate(JsonMsg);
		}
		else
		{
			OnStreamerIceCandidate(JsonMsg);
		}
	}
	else if (MsgType == TEXT("playerCount"))
	{
		OnPlayerCount(JsonMsg);
	}
	else if (MsgType == TEXT("playerDisconnected"))
	{
		OnPlayerDisconnected(JsonMsg);
	}
	else
	{
		UE_LOG(LogPixelStreamingSS, Error, TEXT("Unsupported message `%s` received from SS"), *MsgType);
		WS->Close(4001, TEXT("Unsupported message received: ") + MsgType);
	}
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
		HANDLE_SS_ERROR(TEXT("Cannot find `peerConnectionOptions` field in SS config\n%s"), *ToString(Json));
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
				HANDLE_SS_ERROR(TEXT("Failed to parse SS config: `iceServer` - not an object\n%s"), *ToString(*PeerConnectionOptions));
			}

			RTCConfig.servers.push_back(webrtc::PeerConnectionInterface::IceServer{});
			webrtc::PeerConnectionInterface::IceServer& IceServer = RTCConfig.servers.back();

			TArray<FString> Urls;
			if ((*IceServerJson)->TryGetStringArrayField(TEXT("urls"), Urls))
			{
				for (const FString& Url : Urls)
				{
					IceServer.urls.push_back(to_string(Url));
				}
			}

			FString Username;
			if ((*IceServerJson)->TryGetStringField(TEXT("username"), Username))
			{
				IceServer.username = to_string(Username);
			}

			FString Credential;
			if ((*IceServerJson)->TryGetStringField(TEXT("credential"), Credential))
			{
				IceServer.password = to_string(Credential);
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
		HANDLE_SS_ERROR(TEXT("Cannot find `sdp` in Streamer's answer\n%s"), *ToString(Json));
	}

	webrtc::SdpParseError Error;
	std::unique_ptr<webrtc::SessionDescriptionInterface> SessionDesc =
		webrtc::CreateSessionDescription(Type, to_string(Sdp), &Error);
	if (!SessionDesc)
	{
		HANDLE_SS_ERROR(TEXT("Failed to parse answer's SDP\n%s"), *Sdp);
	}

	if (Type == webrtc::SdpType::kOffer)
	{
		uint32 PlayerId;
		if (!Json->TryGetNumberField(TEXT("playerId"), PlayerId))
		{
			HANDLE_SS_ERROR(TEXT("Failed to get `playerId`from `offer` message\n%s"), *ToString(Json));
		}

		Observer.OnOffer(PlayerId, TUniquePtr<webrtc::SessionDescriptionInterface>{SessionDesc.release()});
	}
	else
	{
		Observer.OnAnswer(TUniquePtr<webrtc::SessionDescriptionInterface>{SessionDesc.release()});
	}
}

void FSignallingServerConnection::OnStreamerIceCandidate(const FJsonObjectPtr& Json)
{
	const FJsonObjectPtr* CandidateJson;
	if (!Json->TryGetObjectField(TEXT("candidate"), CandidateJson))
	{
		HANDLE_SS_ERROR(TEXT("Failed to get `candiate` from remote `iceCandidate` message\n%s"), *ToString(Json));
	}

	FString SdpMid;
	if (!(*CandidateJson)->TryGetStringField(TEXT("sdpMid"), SdpMid))
	{
		HANDLE_SS_ERROR(TEXT("Failed to get `sdpMid` from remote `iceCandidate` message\n%s"), *ToString(Json));
	}

	int32 SdpMLineIndex = 0;
	if (!(*CandidateJson)->TryGetNumberField(TEXT("sdpMlineIndex"), SdpMLineIndex))
	{
		HANDLE_SS_ERROR(TEXT("Failed to get `sdpMlineIndex` from remote `iceCandidate` message\n%s"), *ToString(Json));
	}

	FString CandidateStr;
	if (!(*CandidateJson)->TryGetStringField(TEXT("candidate"), CandidateStr))
	{
		HANDLE_SS_ERROR(TEXT("Failed to get `candidate` from remote `iceCandidate` message\n%s"), *ToString(Json));
	}

	webrtc::SdpParseError Error;
	std::unique_ptr<webrtc::IceCandidateInterface> Candidate(webrtc::CreateIceCandidate(to_string(SdpMid), SdpMLineIndex, to_string(CandidateStr), &Error));
	if (!Candidate)
	{
		HANDLE_SS_ERROR(TEXT("Failed to parse remote `iceCandidate` message\n%s"), *ToString(Json));
	}

	Observer.OnRemoteIceCandidate(TUniquePtr<webrtc::IceCandidateInterface>{Candidate.release()});
}

void FSignallingServerConnection::OnPlayerIceCandidate(const FJsonObjectPtr& Json)
{
	uint32 PlayerId = Json->GetNumberField(TEXT("playerId"));

	const FJsonObjectPtr* CandidateJson;
	if (!Json->TryGetObjectField(TEXT("candidate"), CandidateJson))
	{
		HANDLE_PLAYER_SS_ERROR(PlayerId, TEXT("Failed to get `candiate` from remote `iceCandidate` message\n%s"), *ToString(Json));
	}

	FString SdpMid;
	if (!(*CandidateJson)->TryGetStringField(TEXT("sdpMid"), SdpMid))
	{
		HANDLE_PLAYER_SS_ERROR(PlayerId, TEXT("Failed to get `sdpMid` from remote `iceCandidate` message\n%s"), *ToString(Json));
	}

	int32 SdpMLineIndex = 0;
	if (!(*CandidateJson)->TryGetNumberField(TEXT("sdpMlineIndex"), SdpMLineIndex))
	{
		HANDLE_PLAYER_SS_ERROR(PlayerId, TEXT("Failed to get `sdpMlineIndex` from remote `iceCandidate` message\n%s"), *ToString(Json));
	}

	FString CandidateStr;
	if (!(*CandidateJson)->TryGetStringField(TEXT("candidate"), CandidateStr))
	{
		HANDLE_PLAYER_SS_ERROR(PlayerId, TEXT("Failed to get `candidate` from remote `iceCandidate` message\n%s"), *ToString(Json));
	}

	webrtc::SdpParseError Error;
	std::unique_ptr<webrtc::IceCandidateInterface> Candidate(webrtc::CreateIceCandidate(to_string(SdpMid), SdpMLineIndex, to_string(CandidateStr), &Error));
	if (!Candidate)
	{
		HANDLE_PLAYER_SS_ERROR(PlayerId, TEXT("Failed to parse remote `iceCandidate` message\n%s"), *ToString(Json));
	}

	Observer.OnRemoteIceCandidate(PlayerId, TUniquePtr<webrtc::IceCandidateInterface>{Candidate.release()});
}

void FSignallingServerConnection::OnPlayerCount(const FJsonObjectPtr& Json)
{
	uint32 Count;
	if (!Json->TryGetNumberField(TEXT("count"), Count))
	{
		HANDLE_SS_ERROR(TEXT("Failed to get `count` from `playerCount` message\n%s"), *ToString(Json));
	}

	Observer.OnPlayerCount(Count);
}

void FSignallingServerConnection::OnPlayerDisconnected(const FJsonObjectPtr& Json)
{
	uint32 PlayerId;
	if (!Json->TryGetNumberField(TEXT("playerId"), PlayerId))
	{
		HANDLE_SS_ERROR(TEXT("Failed to get `playerId`from `playerDisconnected` message\n%s"), *ToString(Json));
	}

	Observer.OnPlayerDisconnected(PlayerId);
}

#undef HANDLE_PLAYER_SS_ERROR
#undef HANDLE_SS_ERROR