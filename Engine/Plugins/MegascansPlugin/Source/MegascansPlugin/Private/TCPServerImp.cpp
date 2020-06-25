
#include "TCPServerImp.h"
#include <string>
#include "Engine.h"
#include "NetworkMessage.h"
#include "Runtime/Core/Public/Misc/ScopedSlowTask.h"


FTCPServer::FTCPServer() 
{
	FThreadSafeCounter  WorkerCounter;
	FString ThreadName(FString::Printf(TEXT("MyThreadName%i"), WorkerCounter.Increment()));
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
		FSocket *Client = NULL;

		while (PendingClients.Dequeue(Client))
		{
			Client->Close();
			delete Client;
			Client = NULL;
		}
	}

	for (TArray<class FSocket*>::TIterator ClientIt(Clients); ClientIt; ++ClientIt)
	{
		(*ClientIt)->Close();
		delete (*ClientIt);
		(*ClientIt) = NULL;
	}
	
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

		FIPv4Address::Parse(ipAddressIn, address);

		FIPv4Endpoint endPoint = FIPv4Endpoint(address, portNum);

		Listener = new FTcpListener(endPoint, FTimespan::FromMilliseconds(300));

		Listener->OnConnectionAccepted().BindRaw(this, &FTCPServer::HandleListenerConnectionAccepted);

		Stopping = false;
	}

	TArray<FString> Tokens;

	bool sucessful = ParseMessage("32,54,78|.56,.34,.78,1", Tokens);
	
	return (Listener != NULL);
}


uint32  FTCPServer::Run()
{
	while (!Stopping)
	{

		if (!PendingClients.IsEmpty())
		{
			
			for (TArray<class FSocket*>::TIterator ClientIt(Clients); ClientIt; ++ClientIt)
			{
				(*ClientIt)->Close();
				delete (*ClientIt);
				(*ClientIt) = NULL;
			}

			FSocket *Client = NULL;
			while (PendingClients.Dequeue(Client))
			{
				
				Clients.Empty();
				Clients.Add(Client);
				connectionTimer.AddZeroed(1);
			}
		}

		

		if (Clients.Num() > 0)
		{
			
			FString RecievedJson = TEXT("");
			uint32 DataSize = 0;

			for (TArray<class FSocket*>::TIterator ClientIt(Clients); ClientIt; ++ClientIt)
			{
				
				FSocket *Client = *ClientIt;

				
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
						ClearMessage();
						Request.Empty();
						haveMessage = false;
					}

				}		

			}

			if (RecievedJson != TEXT(""))
			{
				if (!IsGarbageCollecting() && !GIsSavingPackage)
				{			

					AsyncTask(ENamedThreads::GameThread, [this, RecievedJson]() {
						FAssetsImportController::Get()->DataReceived(RecievedJson);
					});
					RecievedJson.Empty();
				}
			}
			
		}
		
		
		FPlatformProcess::Sleep(0.3f); //0.3f
	}

	return 0;
}

bool FTCPServer::ParseMessage(const FString& message, TArray<FString>& Tokens)
{
	
	if (message.ParseIntoArray(Tokens, TEXT("|"), false) == 5)
	{
		return true;
	}

	return false;
}


FString FTCPServer::CreateClientID()
{
	return FGuid::NewGuid().ToString();
}


void FTCPServer::RecvEncryptedData(FSocket *sock, TArray<uint8>& data, bool& success)
{

	check(sock);
	uint32 DataSize;
	FArrayReaderPtr Datagram = MakeShareable(new FArrayReader(true));
	bool hasData = sock->HasPendingData(DataSize);

	if (hasData)
	{
		success = true;
		Datagram->Init(FMath::Min(DataSize, 65507u), 128);

		int32 BytesRead = 0;

		success = sock->Recv(Datagram->GetData(), Datagram->Num(), BytesRead);

		data = *Datagram;
		return;
	}

	success = false;

}

FString FTCPServer::recievedMessage(FString message)
{
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("From Client ~> %s"), *message));
	return message;
}



bool FTCPServer::RecvMessage(FSocket *Socket, uint32 DataSize, FString& Message)
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


bool FTCPServer::HandleListenerConnectionAccepted(class FSocket *ClientSocket, const FIPv4Endpoint& ClientEndpoint)
{
	

	PendingClients.Enqueue(ClientSocket);
	return true;
}


FString FTCPServer::GetMessage()
{
	return _tempString;
}



void FTCPServer::ClearMessage()
{
	_tempString.Empty();
}



bool FTCPServer::SocketCheckPendingData(FSocket* Sock)
{
	uint32 dataSize = 0;
	return Sock->HasPendingData(dataSize);
}

