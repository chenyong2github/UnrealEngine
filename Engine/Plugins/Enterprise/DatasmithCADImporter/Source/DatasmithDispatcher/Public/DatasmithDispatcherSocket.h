// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Sockets.h"
#include "GenericPlatform/GenericPlatformProcess.h"

#define DEFAULT_DATASMITHSERVER_PORT 0xCAFE
#define MAX_DATASMITHSERVER_PORT 0xFFFF 

namespace DatasmithDispatcher
{

enum SocketErrorCode
{
	NoError = 0,
	UnableToReadOnSocket = 1,
	UnableToSendData = 2,
	CouldNotStartWSA = 3,
	UnableToGetLocalAddress = 4,
	ConnectionToServerFailed = 5
};

class FDatasmithDispatcherSocket 
{

public:
	FDatasmithDispatcherSocket(const TCHAR* ServerAddress);
	FDatasmithDispatcherSocket();
	FDatasmithDispatcherSocket& operator=(const FDatasmithDispatcherSocket&) = delete;
			
	void SetSocket(FSocket* InSocket);

	int32 GetErrorCode() const
	{
		return ErrorCode;
	}

	void Connect(const int32& ServerPort);

	bool IsConnected();

	void Bind();

	bool Listen();
	FSocket* Accept();

	int32 GetPort() const
	{
		if (!Socket)
		{
			return -1;
		}
		return Socket->GetPortNo();
	}

	void Close();

	template <typename T>
	void operator >>(T &val)
	{
		Read(sizeof(val), (uint8*)&val);
	}

	template <typename T>
	void operator <<(T val)
	{
		Write(sizeof(val), (uint8*)&val);
	}

	void operator >>(FString& str);
	void operator <<(const FString& str);

	bool IsClosed() const 
	{
		return !bOpen;
	}

	bool IsOpen() const
	{
		return bOpen;
	}

	FSocket* GetSocket()
	{
		return Socket;
	}

	bool HasPendingData(uint32& DataSize);

	/**
		* Actually send the buffered output cache
		*/
	void SendData();

private:
	// low-level reading function
	void Read(int32 Num, uint8 *Buffer);

	// low-level writing function
	void Write(int32 Num, uint8 *Buffer);

private:
	TArray<uint8> Cache;

	FString SocketAdress;
	int32 SocketId;
	FSocket* Socket;

	bool bServerSide;
	bool bOpen;

	int32 ErrorCode;
};

} //namespace DatasmithDispatcher
