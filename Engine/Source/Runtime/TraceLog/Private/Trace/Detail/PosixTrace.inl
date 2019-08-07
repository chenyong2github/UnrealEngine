// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
UPTRINT ThreadCreate(const ANSICHAR* Name, void (*Entry)())
{
	auto Thunk = [] (void* Param) -> void*
	{
		typedef void (*EntryType)(void);
		(EntryType(Param))();
		return nullptr;
	};

	pthread_t Thread;
	if (pthread_create(&Thread, nullptr, Thunk, (void*)Entry) != 0)
	{
		return 0;
	}

	return UPTRINT(Thread);
}

////////////////////////////////////////////////////////////////////////////////
uint32 ThreadGetCurrentId()
{
	return pthread_self();
}

////////////////////////////////////////////////////////////////////////////////
void ThreadSleep(uint32 Milliseconds)
{
	sleep(Milliseconds << 10);
}

////////////////////////////////////////////////////////////////////////////////
void ThreadJoin(UPTRINT Handle)
{
	pthread_t Inner = pthread_t(Handle);
	pthread_join(Inner, nullptr);
}

////////////////////////////////////////////////////////////////////////////////
void ThreadDestroy(UPTRINT Handle)
{
	pthread_t Inner = pthread_t(Handle);
	pthread_detach(Inner);
}



////////////////////////////////////////////////////////////////////////////////
UPTRINT TcpSocketConnect(const ANSICHAR* Host, uint16 Port)
{
	sockaddr_in Addr;
	Addr.sin_family = AF_INET;
	Addr.sin_port = htons(Port);
	inet_aton(Host, &Addr.sin_addr);

	int Socket = socket(AF_INET, SOCK_STREAM, 0);
	if (Socket < 0)
	{
		return 0;
	}

	int Result = connect(Socket, &(sockaddr&)Addr, sizeof(Addr));
	if (Result < 0)
	{
		close(Socket);
		return 0;
	}

	return UPTRINT(Socket + 1);
}

////////////////////////////////////////////////////////////////////////////////
UPTRINT TcpSocketListen(uint16 Port)
{
	int Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (Socket < 0)
	{
		return 0;
	}

	sockaddr_in SockAddr;
	SockAddr.sin_family = AF_INET;
	SockAddr.sin_addr.s_addr = 0;
	SockAddr.sin_port = htons(Port);
	int Result = bind(Socket, (sockaddr*)&SockAddr, sizeof(SockAddr));
	if (Result < 0)
	{
		close(Socket);
		return 0;
	}

	Result = listen(Socket, 1);
	if (Result < 0)
	{
		close(Socket);
		return 0;
	}

	int Flags = fcntl(Socket, F_GETFL, 0);
	if (Flags < 0 || !fcntl(Socket, F_SETFL, Flags|O_NONBLOCK))
	{
		close(Socket);
		return 0;
	}

	return UPTRINT(Socket + 1);
}

////////////////////////////////////////////////////////////////////////////////
int TcpSocketAccept(UPTRINT Socket, UPTRINT& Out)
{
	int Inner = int(Socket - 1);

	Inner = accept(Inner, nullptr, nullptr);
	if (Inner < 0)
	{
		if (Inner == EWOULDBLOCK || Inner == EAGAIN)
		{
			return 0;
		}

		return -1;
	}

	int Flags = fcntl(Inner, F_GETFL, 0);
	if (Flags < 0 || !fcntl(Inner, F_SETFL, Flags & ~O_NONBLOCK))
	{
		close(Socket);
		return 0;
	}

	Out = UPTRINT(Inner + 1);
	return 1;
}

////////////////////////////////////////////////////////////////////////////////
void TcpSocketClose(UPTRINT Socket)
{
	int Inner = int(Socket - 1);
	close(Inner);
}

////////////////////////////////////////////////////////////////////////////////
bool TcpSocketSelect(UPTRINT Socket)
{
	int Inner = int(Socket - 1);
	fd_set FdSet;
	FD_SET(Inner, &FdSet);
	timeval TimeVal = {};
	return (select(0, &FdSet, nullptr, nullptr, &TimeVal) != 0);
}

////////////////////////////////////////////////////////////////////////////////
bool TcpSocketSend(UPTRINT Socket, const void* Data, uint32 Size)
{
	int Inner = int(Socket - 1);
	return send(Inner, (const char*)Data, Size, 0) == Size;
}

////////////////////////////////////////////////////////////////////////////////
int32 TcpSocketRecv(UPTRINT Socket, void* Data, uint32 Size)
{
	int Inner = int(Socket - 1);
	return recv(Inner, (char*)Data, Size, 0);
}

} // namespace Trace
