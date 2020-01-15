// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Modules/ModuleManager.h"

class FWebSocket; 
class FWebSocketServer; 

typedef struct lws_context WebSocketInternalContext;
typedef struct lws WebSocketInternal;
typedef struct lws_protocols WebSocketInternalProtocol;

DECLARE_DELEGATE_TwoParams(FWebSocketPacketRecievedCallBack, void* /*Data*/, int32 /*Data Size*/);
DECLARE_DELEGATE_OneParam(FWebSocketClientConnectedCallBack, FWebSocket* /*Socket*/);
DECLARE_DELEGATE(FWebSocketInfoCallBack); 

DECLARE_LOG_CATEGORY_EXTERN(LogWebSocketNetworking, Warning, All);
