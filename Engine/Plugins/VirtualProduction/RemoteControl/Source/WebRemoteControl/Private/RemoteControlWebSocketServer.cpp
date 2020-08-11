// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlWebSocketServer.h"
#include "IWebSocketNetworkingModule.h"
#include "WebSocketNetworkingDelegates.h"
#include "WebRemoteControlUtils.h"
#include "IRemoteControlModule.h"
#include "RemoteControlRequest.h"
#include "Containers/Ticker.h"

#define LOCTEXT_NAMESPACE "RCWebSocketServer"

struct FWebSocketMessage
{
	FString MessageName;
	int32 Id = -1;
	TArrayView<uint8> RequestPayload;
};

namespace RemoteControlWebSocketServer
{
	static const FString MessageNameFieldName = TEXT("MessageName");
	static const FString PayloadFieldName = TEXT("Parameters");

	TOptional<FWebSocketMessage> ParseWebsocketMessage(TArrayView<uint8> InPayload)
	{
		FRCWebSocketRequest Request;
		WebRemoteControlUtils::DeserializeRequestPayload(InPayload, nullptr, Request);

		FString ErrorText;
		if (Request.MessageName.IsEmpty())
		{
			ErrorText = TEXT("Missing MessageName field.");
		}

		FBlockDelimiters PayloadDelimiters = Request.GetParameterDelimiters(FRCWebSocketRequest::ParametersFieldLabel());
		if (PayloadDelimiters.BlockStart == PayloadDelimiters.BlockEnd)
		{
			ErrorText = FString::Printf(TEXT("Missing %s field."), *FRCWebSocketRequest::ParametersFieldLabel());
		}

		TOptional<FWebSocketMessage> ParsedMessage;
		if (!ErrorText.IsEmpty())
		{
			UE_LOG(LogRemoteControl, Error, TEXT("%s"), *FString::Format(TEXT("Encountered error while deserializing websocket message. \r\n{0}"), { *ErrorText }));
		}
		else
		{
			FWebSocketMessage Message = { MoveTemp(Request.MessageName), Request.Id, MakeArrayView(InPayload).Slice(PayloadDelimiters.BlockStart, PayloadDelimiters.BlockEnd - PayloadDelimiters.BlockStart)};
			ParsedMessage = MoveTemp(Message);
		}

		return ParsedMessage;
	}
}

void FWebsocketMessageRouter::Dispatch(const FWebSocketMessage& Message)
{
	if (FWebSocketMessageDelegate* Callback = DispatchTable.Find(Message.MessageName))
	{
		Callback->ExecuteIfBound(Message.Id, Message.RequestPayload);
	}
}

void FWebsocketMessageRouter::BindRoute(const FString& MessageName, FWebSocketMessageDelegate OnMessageReceived)
{
	DispatchTable.Add(MessageName, MoveTemp(OnMessageReceived));
}

void FWebsocketMessageRouter::UnbindRoute(const FString& MessageName)
{
	DispatchTable.Remove(MessageName);
}

bool FRCWebSocketServer::Start(uint32 Port, TSharedPtr<FWebsocketMessageRouter> InRouter)
{
	FWebSocketClientConnectedCallBack CallBack;
	CallBack.BindRaw(this, &FRCWebSocketServer::OnWebSocketClientConnected);
	
	Server = FModuleManager::Get().LoadModuleChecked<IWebSocketNetworkingModule>(TEXT("WebSocketNetworking")).CreateServer();
	
	if (!Server || !Server->Init(Port, CallBack))
	{
		Server.Reset();
		return false;
	}

	Router = MoveTemp(InRouter);
	TickerHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FRCWebSocketServer::Tick));

	return true;
}

void FRCWebSocketServer::Stop()
{
	if (TickerHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	}
	Router.Reset();
	Server.Reset();
}

FRCWebSocketServer::~FRCWebSocketServer()
{
	Stop();
}

void FRCWebSocketServer::Broadcast(const FString& Message)
{
	TArray<uint8> SendBuffer;
	WebRemoteControlUtils::ConvertToUTF8(Message, SendBuffer);

	for (FWebSocketConnection& Connection : Connections)
	{
		if (Connection.Socket)
		{
			Connection.Socket->Send(SendBuffer.GetData(), SendBuffer.Num());
		}
	}
}

bool FRCWebSocketServer::IsRunning() const
{
	return !!Server;
}

bool FRCWebSocketServer::Tick(float DeltaTime)
{
	Server->Tick();
	return true;
}

void FRCWebSocketServer::OnWebSocketClientConnected(INetworkingWebSocket* Socket)
{
	if (ensureMsgf(Socket, TEXT("Socket was null while creating a new websocket connection.")))
	{
		FWebSocketConnection Connection = FWebSocketConnection{ Socket };
			
		FWebSocketPacketReceivedCallBack ReceiveCallBack;
		ReceiveCallBack.BindRaw(this, &FRCWebSocketServer::ReceivedRawPacket);
		Socket->SetReceiveCallBack(ReceiveCallBack);

		FWebSocketInfoCallBack CloseCallback;
		CloseCallback.BindRaw(this, &FRCWebSocketServer::OnSocketClose, Socket);
		Socket->SetSocketClosedCallBack(CloseCallback);

		Connections.Add(MoveTemp(Connection));
	}
}

void FRCWebSocketServer::ReceivedRawPacket(void* Data, int32 Size)
{
	if (!Router)
	{
		return;
	}

	TArray<uint8> Payload;
	WebRemoteControlUtils::ConvertToTCHAR(MakeArrayView(static_cast<uint8*>(Data), Size), Payload);

	if (TOptional<FWebSocketMessage> Message = RemoteControlWebSocketServer::ParseWebsocketMessage(Payload))
	{
		Router->Dispatch(*Message);
	}
}

void FRCWebSocketServer::OnSocketClose(INetworkingWebSocket* Socket)
{
	int32 Index = Connections.IndexOfByPredicate([Socket](const FWebSocketConnection& Connection) { return Connection.Socket == Socket; });
	if (Index != INDEX_NONE)
	{
		Connections.RemoveAtSwap(Index);
	}
}


#undef LOCTEXT_NAMESPACE /* FRCWebSocketServer */

