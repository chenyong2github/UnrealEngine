// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once


#include "CoreMinimal.h"
#include <string>
#include "Sockets.h"
#include "Misc/DateTime.h"
#include "Async/AsyncWork.h"
#include "Sockets.h"
#include "Common/TcpListener.h"
#include "Sockets.h"
#include "Containers/Queue.h"
#include "Common/UdpSocketReceiver.h"



class FTCPServer : public FRunnable
{

public:	
	FTCPServer();
	~FTCPServer();	
	virtual bool Init() override;	
	virtual uint32 Run() override;



	virtual void Stop() override
	{
		Stopping = true;
	}

	virtual void Exit() override { }
	bool RecvMessage(FSocket *Socket, uint32 DataSize, FString& Message);
	bool HandleListenerConnectionAccepted(class FSocket *ClientSocket, const FIPv4Endpoint& ClientEndpoint);


	FSocket* ListenerSocket;
	FString LocalHostIP = "127.0.0.1";
	int32 PortNum = 13429; 
	int32 ConnectionTimeout;
	TArray<class FSocket*> Clients;

private:	
	TQueue<class FSocket*, EQueueMode::Mpsc> PendingClients;
	bool Stopping;
	FRunnableThread* ClientThread = NULL;
	class FTcpListener *Listener = NULL;	
	TArray<int32> ConnectionTimer;

};


