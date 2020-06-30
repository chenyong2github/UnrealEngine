#pragma once


#include "CoreMinimal.h"
#include <string>
#include "Runtime/Sockets/Public/Sockets.h"
#include "Runtime/Core/Public/Misc/DateTime.h"
#include "Runtime/Networking/Public/Networking.h"
#include "Runtime/Core/Public/Async/Async.h"

#include "MSPythonBridge.h"
#include "UI/QMSUIManager.h"
#include "AssetsImportController.h"



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

	bool HandleListenerConnectionAccepted(class FSocket *ClientSocket, const FIPv4Endpoint& ClientEndpoint);

public:

	FSocket* ListenerSocket;

	FString ipAddressIn = "127.0.0.1";
	
	int32 portNum = 13429; // 13428
	FString GetMessage();
	void ClearMessage();
	int32 connectionTimeout;
	bool ParseMessage(const FString& message, TArray<FString>& Tokens);
	TArray<class FSocket*> Clients;
	bool SocketCheckPendingData(FSocket* Sock);

private:	
	TQueue<class FSocket*, EQueueMode::Mpsc> PendingClients;
	FString CreateClientID();
	bool Stopping;

	FRunnableThread* ClientThread = NULL;

	class FTcpListener *Listener = NULL;

	FString _LoginInfo;

	FString _tempString;

	FDateTime *getRealTime = NULL;

	TArray<int32> connectionTimer;


};


