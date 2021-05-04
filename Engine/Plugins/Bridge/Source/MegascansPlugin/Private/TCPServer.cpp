// Copyright Epic Games, Inc. All Rights Reserved.
#include "TCPServer.h"
#include <string>
#include "NetworkMessage.h"
#include "Async/Async.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/GarbageCollection.h"
#include "AssetsImportController.h"



FTCPServer::FTCPServer()
{
	FThreadSafeCounter  WorkerCounter;
	FString ThreadName(FString::Printf(TEXT("MegascansPlugin%i"), WorkerCounter.Increment()));
	ClientThread = FRunnableThread::Create(this, *ThreadName, 8 * 1024, TPri_Normal);

}

FTCPServer::~FTCPServer()
{

	Stop();


	if (Listener != NULL)
	{
		Listener->Stop();
		delete Listener;
		Listener = NULL;
	}

	if (!PendingClients.IsEmpty())
	{
		FSocket* Client = NULL;

		while (PendingClients.Dequeue(Client))
		{
			Client->Close();
			delete Client;
			Client = NULL;
		}
	}

	/*for (TArray<class FSocket*>::TIterator ClientIt(Clients); ClientIt; ++ClientIt)
	{
		(*ClientIt)->Close();
		delete (*ClientIt);
		(*ClientIt) = NULL;
	}*/

	if (ClientThread != NULL)
	{
		ClientThread->Kill(true);
		delete ClientThread;
	}
}


bool FTCPServer::Init()
{

	if (Listener == NULL)
	{
		FIPv4Address address;
		FIPv4Address::Parse(LocalHostIP, address);
		FIPv4Endpoint endPoint = FIPv4Endpoint(address, PortNum);
		Listener = new FTcpListener(endPoint, FTimespan::FromMilliseconds(300));
		Listener->OnConnectionAccepted().BindRaw(this, &FTCPServer::HandleListenerConnectionAccepted);
		Stopping = false;
	}

	return (Listener != NULL);
}


uint32  FTCPServer::Run()
{
	while (!Stopping)
	{
		FPlatformProcess::Sleep(0.3f);

		if (!PendingClients.IsEmpty())
		{
			FPlatformProcess::Sleep(0.2f);

			FString RecievedJson = TEXT("");
			uint32 DataSize = 0;



			FSocket* Client;
			PendingClients.Dequeue(Client);
			DataSize = 0;
			FString Request;
			TArray<uint8> data;
			bool haveMessage = false;
			while (Client->HasPendingData(DataSize))
			{
				haveMessage = RecvMessage(Client, DataSize, Request);
				if (haveMessage)
				{

					int32 timer = 0;
					RecievedJson += Request;
					Request.Empty();
					haveMessage = false;
				}

			}

			
			Client->Close();
			delete Client;

			if (RecievedJson != TEXT(""))
			{
				ImportQueue.Enqueue(RecievedJson);
				RecievedJson.Empty();
				
			}

		}

		if (!ImportQueue.IsEmpty())
		{
			if (!IsGarbageCollecting() && !GIsSavingPackage)
			{
				FString ImportData;
				ImportQueue.Dequeue(ImportData);
				AsyncTask(ENamedThreads::GameThread, [this, ImportData]() {
					FAssetsImportController::Get()->DataReceived(ImportData);
				});


			}
		}


		
	}

	return 0;
}


bool FTCPServer::RecvMessage(FSocket* Socket, uint32 DataSize, FString& Message)
{
	check(Socket);
	FArrayReaderPtr Datagram = MakeShareable(new FArrayReader(true));
	int32 stuff = 16;
	Datagram->Init(FMath::Min(DataSize, 65507u), 81920);
	int32 BytesRead = 0;

	if (Socket->Recv(Datagram->GetData(), Datagram->Num(), BytesRead))
	{

		char* Data = (char*)Datagram->GetData();
		Data[BytesRead] = '\0';
		FString message = UTF8_TO_TCHAR(Data);
		Message = message;

		return true;
	}

	return false;
}

bool FTCPServer::HandleListenerConnectionAccepted(class FSocket* ClientSocket, const FIPv4Endpoint& ClientEndpoint)

{
	
	PendingClients.Enqueue(ClientSocket);
	return true;
}


