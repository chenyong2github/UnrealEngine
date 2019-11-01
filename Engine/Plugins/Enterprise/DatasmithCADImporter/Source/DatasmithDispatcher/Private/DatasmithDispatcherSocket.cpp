// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "DatasmithDispatcherSocket.h"

#include "DatasmithDispatcherLog.h"  //#ueent_CAD
#include "DatasmithDispatcherModule.h"

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

DEFINE_LOG_CATEGORY(LogDatasmithDispatcher);


namespace DatasmithDispatcher
{

FDatasmithDispatcherSocket::FDatasmithDispatcherSocket(const TCHAR* ServerAddress)
	: SocketAdress(ServerAddress)
	, SocketId(-1)
	, Socket(nullptr)
	, bServerSide(true)
	, bOpen(false)
	, ErrorCode(0)
{
	Cache.Reserve(1024 * 128);
}

FDatasmithDispatcherSocket::FDatasmithDispatcherSocket()
	: SocketId(-1)
	, Socket(nullptr)
	, bServerSide(true)
	, bOpen(false)
	, ErrorCode(0)
{
	Cache.Reserve(1024 * 128);
}

void FDatasmithDispatcherSocket::SetSocket(FSocket* InSocket)
{
	Socket = InSocket;
	bOpen = true;
}

bool FDatasmithDispatcherSocket::Listen()
{
	return Socket->Listen(1);
}

FSocket* FDatasmithDispatcherSocket::Accept()
{
	FString Message = TEXT("DispatcherSocket");
	return Socket->Accept(Message);
}

void FDatasmithDispatcherSocket::Bind()
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get();
	if (nullptr == SocketSubsystem)
	{
		return;
	}

	TSharedPtr<FInternetAddr> InternetAddress = SocketSubsystem->CreateInternetAddr();

	bool bIsValid = true;
	InternetAddress->SetIp(*SocketAdress, bIsValid);
	if (!bIsValid)
	{
		return;
	}

	Socket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("FNetworkFileServer tcp-listen"), InternetAddress->GetProtocolType());
	if (nullptr == Socket)
	{
		return;
	}

	int32 SocketPort = SocketSubsystem->BindNextPort(Socket, *InternetAddress, 5000, 1);
	if (SocketPort == 0)
	{
		return;
	}

	bOpen = true;
}

bool FDatasmithDispatcherSocket::IsConnected()
{
	uint32 DataSize;
	Socket->HasPendingData(DataSize);

	bool bHasPendingConnection;
	Socket->HasPendingConnection(bHasPendingConnection);
	return !bHasPendingConnection || DataSize;
}

bool FDatasmithDispatcherSocket::HasPendingData(uint32& DataSize)
{
	return Socket->HasPendingData(DataSize);
}


void FDatasmithDispatcherSocket::Connect(const int32& InServerPort)
{
	bServerSide = false;
	bOpen = false;

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get();
	if (nullptr == SocketSubsystem)
	{
		return;
	}

	TSharedPtr<FInternetAddr> InternetAddress = SocketSubsystem->CreateInternetAddr();
	if(!InternetAddress.IsValid())
	{
		return;
	}

	bool bIsValid = true;
	InternetAddress->SetIp(*SocketAdress, bIsValid);
	if (!bIsValid)
	{
		return;
	}
	InternetAddress->SetPort(InServerPort);

	Socket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("FNetworkFileServer tcp-listen"), InternetAddress->GetProtocolType());
	if (nullptr == Socket)
	{
		return;
	}
	Socket->SetNonBlocking(true);

	if (!Socket->Connect(*InternetAddress))
	{
		return;
	}
	UE_LOG(LogDatasmithDispatcher, Display, TEXT("Is connected"));

	bool bHasPendingConnection = true;
	Socket->WaitForPendingConnection(bHasPendingConnection, FTimespan(0, 0, 5));

	UE_LOG(LogDatasmithDispatcher, Display, TEXT("WaitForPendingConnection return = %d"), bHasPendingConnection);
	bOpen = !bHasPendingConnection;
}

void FDatasmithDispatcherSocket::Close()
{
	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get();
		SocketSubsystem->DestroySocket(Socket);
		Socket = nullptr;
	}
}


void FDatasmithDispatcherSocket::Read(int32 DataSize, uint8* Data)
{
	if (!bOpen) return;

	int32 Total = 0;
	memset(Data, 0, DataSize);
	int32 BytesRead;

	while (Total < DataSize)
	{
		bool bStatus = Socket->Recv(Data + Total, DataSize - Total, BytesRead);
		if (!bStatus)
		{
			UE_LOG(LogDatasmithDispatcher, Display, TEXT("Close socket Read %d %d "), DataSize, Total);

			Socket->Close();
			ErrorCode = UnableToReadOnSocket;
			bOpen = false;
		}
		Total += BytesRead;
	}
}

void FDatasmithDispatcherSocket::Write(int32 DataSize, uint8 *Data)
{
	Cache.Append(Data, DataSize);
}

void FDatasmithDispatcherSocket::SendData()
{
	if (!bOpen) return;
	int32 BytesSend;

	uint32 BufferSize = Cache.Num();
	uint8 *BufferSizeData = (uint8*)&BufferSize;
	uint32 Total = 0;
	while (Total < sizeof(uint32))
	{
		bool bStatus = Socket->Send(BufferSizeData + Total, sizeof(uint32) - Total, BytesSend);
		Total += (uint32)BytesSend;
	}

	uint8 *Data = Cache.GetData();
	Total = 0;
	while (Total < BufferSize)
	{
		bool bStatus = Socket->Send(Data + Total, BufferSize - Total, BytesSend);
		if (!bStatus)
		{
			Socket->Close();
			ErrorCode = UnableToSendData;
			bOpen = false;
			UE_LOG(LogDatasmithDispatcher, Display, TEXT("Close socket Write %d %d "), BufferSize, Total);
		}
		Total += (uint32) BytesSend;
	}
	Cache.Empty();
}

void FDatasmithDispatcherSocket::operator >>(FString& Data)
{
	int Size;
	*this >> Size;

	Data.Empty(Size);
	Data.Reserve(Size);
	for (int k = 0; k < Size; k++)
	{
		TCHAR Char;
		*this >> Char;
		Data.AppendChar(Char);
	}
}

void FDatasmithDispatcherSocket::operator <<(const FString& Data)
{
	*this << (int32)Data.Len();
	for (int32 k = 0; k < Data.Len(); k++)
	{
		*this << Data[k];
	}
}
}