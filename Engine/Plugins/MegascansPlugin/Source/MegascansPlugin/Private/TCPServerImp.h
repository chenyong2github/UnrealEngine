// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once


#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include <string>
#include "Runtime/Sockets/Public/Sockets.h"
#include "Runtime/Core/Public/Misc/DateTime.h"
#include "Runtime/Core/Public/Async/Async.h"

#include "MSPythonBridge.h"
#include "UI/QMSUIManager.h"
#include "AssetsImportController.h"


struct FIPv4Endpoint;
class FSocket;
class FTcpListener;

class FTCPServer : public FRunnable
{

public:	
	FTCPServer();
	~FTCPServer();	
	virtual bool Init() override;	
	virtual uint32 Run() override;

	bool IsActive() const
	{
		return (!Stopping);
	}

	virtual void Stop() override
	{
		Stopping = true;
	}

	virtual void Exit() override { }

	FString recievedMessage(FString message);

	bool RecvMessage(FSocket *Socket, uint32 DataSize, FString& Message);

	void RecvEncryptedData(FSocket *sock, TArray<uint8>& data, bool& success);

	bool HandleListenerConnectionAccepted(FSocket *ClientSocket, const FIPv4Endpoint& ClientEndpoint);

public:

	FSocket* ListenerSocket;

	FString ipAddressIn = "127.0.0.1";
	
	int32 portNum = 13429; // 13428
	FString GetMessage();
	void ClearMessage();
	int32 connectionTimeout;
	bool ParseMessage(const FString& message, TArray<FString>& Tokens);
	TArray<FSocket*> Clients;
	bool SocketCheckPendingData(FSocket* Sock);

private:	
	TQueue<FSocket*, EQueueMode::Mpsc> PendingClients;
	FString CreateClientID();
	bool Stopping;

	FRunnableThread* ClientThread = NULL;

	FTcpListener *Listener = NULL;

	FString _LoginInfo;

	FString _tempString;

	FDateTime *getRealTime = NULL;

	TArray<int32> connectionTimer;


};


