// Copyright Epic Games, Inc. All Rights Reserved.
#include "TCPServer.h"
#include <string>
#include "NetworkMessage.h"
#include "Async/Async.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/GarbageCollection.h"
#include "AssetsImportController.h"

TQueue<FString> FTCPServer::ImportQueue;

FTCPServer::FTCPServer()
{
	FThreadSafeCounter  WorkerCounter;
	FString ThreadName(FString::Printf(TEXT("MegascansPlugin%i"), WorkerCounter.Increment()));
	ClientThread = FRunnableThread::Create(this, *ThreadName, 8 * 1024, TPri_Normal);
}

FTCPServer::~FTCPServer()
{
	Stop();

	if (ClientThread != NULL)
	{
		ClientThread->Kill(true);
		delete ClientThread;
	}
}

bool FTCPServer::Init()
{
	Stopping = false;
	return true;
}

uint32  FTCPServer::Run()
{
	while (!Stopping)
	{
		FPlatformProcess::Sleep(0.3f);	
		

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
