//
// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
//

#if PLATFORM_WINDOWS
#include "Trace/Recorder.h"
#include "Trace/Store.h"
#include "DataStream.h"
#include "Templates/UniquePtr.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#define _WINSOCK_DEPRECATED_NO_WARNINGS  
#include <winsock2.h>
#include <ws2tcpip.h>
#include "Windows/HideWindowsPlatformTypes.h"
#pragma comment(lib, "ws2_32.lib")

#include <thread>

namespace Trace
{

class FSocketStream
	: public IInDataStream
{
public:
					FSocketStream(SOCKET InSocket);
	virtual int32	Read(void* Data, uint32 Size) override;

private:
	SOCKET			Socket;
};

FSocketStream::FSocketStream(SOCKET InSocket)
: Socket(InSocket)
{
}

int32 FSocketStream::Read(void* Data, uint32 Size)
{
	return recv(Socket, (char*)Data, Size, 0);
}

static void Recorder_Record(TSharedRef<IStore> TraceStore, IInDataStream& DataStream)
{
	static const uint32 BufferSize = 1 << 20;
	void* Buffer = FPlatformMemory::MemoryRangeReserve(BufferSize, true /*Commit*/);

	TUniquePtr<IOutDataStream> File = TUniquePtr<IOutDataStream>(TraceStore->CreateNewSession());
	
	while (true)
	{
		int32 ReadSize = DataStream.Read(Buffer, BufferSize);
		if (ReadSize <= 0)
		{
			break;
		}

		File->Write(Buffer, ReadSize);
	}

	FPlatformMemory::MemoryRangeFree(Buffer, BufferSize);
}

////////////////////////////////////////////////////////////////////////////////
static void Recorder_SocketServer(TSharedRef<IStore> TraceStore)
{
	WSADATA WsaData;
	int Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
	check(Result == 0);

	SOCKET ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	check(ListenSocket != INVALID_SOCKET);

	sockaddr_in ServerDesc;
	ServerDesc.sin_family = AF_INET;
	ServerDesc.sin_addr.s_addr = {};
	ServerDesc.sin_port = htons(1980);
	Result = bind(ListenSocket, (SOCKADDR*)&ServerDesc, sizeof(ServerDesc));
	check(Result != SOCKET_ERROR);

	Result = listen(ListenSocket, 1);
	check(Result != SOCKET_ERROR);

	while (true)
	{
		sockaddr_in SockAddr = {};
		int SockAddrSize = sizeof(SockAddr);
		SOCKET Socket = accept(ListenSocket, (sockaddr*)&SockAddr, &SockAddrSize);
		check(Socket != INVALID_SOCKET);

		auto ClientLambda = [] (TSharedRef<IStore> TraceStore, SOCKET Socket)
		{
			uint32 Magic;
			int Recvd = recv(Socket, (char*)&Magic, sizeof(Magic), 0);
			if (Recvd == sizeof(Magic) && Magic == 'TRCE')
			{
				FSocketStream Stream(Socket);
				Recorder_Record(TraceStore, Stream);
			}

			shutdown(Socket, SD_BOTH);
			closesocket(Socket);
		};

		std::thread(ClientLambda, TraceStore, Socket);
	}

	closesocket(ListenSocket);
}

void Recorder_StartServer(TSharedRef<IStore> TraceStore)
{
	std::thread(Recorder_SocketServer, TraceStore);
}

}

#else

namespace Trace
{

void Recorder_StartServer(TSharedRef<IStore> TraceStore) {}

}

#endif
