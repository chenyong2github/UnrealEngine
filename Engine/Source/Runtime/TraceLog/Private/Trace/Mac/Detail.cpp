// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Trace/Trace.h"

#if UE_TRACE_ENABLED

#include <CoreServices/CoreServices.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
uint8* MemoryReserve(SIZE_T Size)
{
	void* Ptr = mmap(nullptr, Size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	return (Ptr != MAP_FAILED) ? reinterpret_cast<uint8*>(Ptr) : nullptr;
}

////////////////////////////////////////////////////////////////////////////////
void MemoryFree(void* Address, SIZE_T Size)
{
	munmap(Address, Size);
}

////////////////////////////////////////////////////////////////////////////////
void MemoryMap(void* Address, SIZE_T Size)
{
	// no-op if mmap()ed with R/W
}

////////////////////////////////////////////////////////////////////////////////
void MemoryUnmap(void* Address, SIZE_T Size)
{
	madvise(Address, Size, MADV_DONTNEED);
}



////////////////////////////////////////////////////////////////////////////////
UPTRINT ThreadCreate(const ANSICHAR* Name, void (*Entry)())
{
	void* (*PthreadThunk)(void*) = [] (void* Param) -> void * {
		typedef void (*EntryType)(void);
		(EntryType(Param))();
		return nullptr;
	};

	pthread_t ThreadHandle;
	if (pthread_create(&ThreadHandle, nullptr, PthreadThunk, reinterpret_cast<void *>(Entry)) != 0)
	{
		return 0;
	}
	return reinterpret_cast<UPTRINT>(ThreadHandle);
}

////////////////////////////////////////////////////////////////////////////////
uint32 ThreadGetCurrentId()
{
	uint64_t ThreadId = 0;
	pthread_threadid_np(nullptr, &ThreadId);
	return static_cast<uint32>(ThreadId);
}

////////////////////////////////////////////////////////////////////////////////
void ThreadSleep(uint32 Milliseconds)
{
	usleep(Milliseconds * 1000U);
}

////////////////////////////////////////////////////////////////////////////////
void ThreadJoin(UPTRINT Handle)
{
	pthread_join(reinterpret_cast<pthread_t>(Handle), nullptr);
}

////////////////////////////////////////////////////////////////////////////////
void ThreadDestroy(UPTRINT Handle)
{
	// no-op
}



////////////////////////////////////////////////////////////////////////////////
const mach_timebase_info_data_t& TimeGetInfo()
{
    static mach_timebase_info_data_t Info;
    static dispatch_once_t onceToken;

	dispatch_once(&onceToken, ^{
		   (void) mach_timebase_info(&Info);
	   });

	return Info;
}

////////////////////////////////////////////////////////////////////////////////
uint64 TimeGetFrequency()
{
	const mach_timebase_info_data_t& Info = TimeGetInfo();
	return (uint64)((double)Info.numer / (1e-9 * (double)Info.denom) + 0.5);
}

////////////////////////////////////////////////////////////////////////////////
uint64 TimeGetTimestamp()
{
	return mach_absolute_time();
}



////////////////////////////////////////////////////////////////////////////////
UPTRINT TcpSocketConnect(const ANSICHAR* Host, uint16 Port)
{
	struct FAddrInfoPtr
	{
					~FAddrInfoPtr()	{ freeaddrinfo(Value); }
		addrinfo*	operator -> ()	{ return Value; }
		addrinfo**	operator & ()	{ return &Value; }
		addrinfo*	Value;
	};

	FAddrInfoPtr Info;
	addrinfo Hints = {};
	Hints.ai_family = AF_INET;
	Hints.ai_socktype = SOCK_STREAM;
	Hints.ai_protocol = IPPROTO_TCP;
	if (getaddrinfo(Host, nullptr, &Hints, &Info))
	{
		return 0;
	}

	if (&Info == nullptr)
	{
		return 0;
	}

	auto* SockAddr = (sockaddr_in*)Info->ai_addr;
	SockAddr->sin_port = htons(Port);

	int Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (Socket < 0)
	{
		return 0;
	}

	int Result = connect(Socket, Info->ai_addr, int(Info->ai_addrlen));
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
	int Result = bind(Socket, reinterpret_cast<sockaddr*>(&SockAddr), sizeof(SockAddr));
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

	return UPTRINT(Socket + 1);
}

////////////////////////////////////////////////////////////////////////////////
UPTRINT TcpSocketAccept(UPTRINT Socket)
{
	int Inner = Socket - 1;

	Inner = accept(Socket - 1, nullptr, nullptr);
	if (Inner < 0)
	{
		return 0;
	}

	return UPTRINT(Inner + 1);
}

////////////////////////////////////////////////////////////////////////////////
void TcpSocketClose(UPTRINT Socket)
{
	int Inner = Socket - 1;
	close(Inner);
}

////////////////////////////////////////////////////////////////////////////////
bool TcpSocketSelect(UPTRINT Socket)
{
	int Inner = Socket - 1;
	fd_set FdSet;
	FD_ZERO(&FdSet);
	FD_SET(Inner, &FdSet);
	timeval TimeVal = {};
	int result = select(Inner + 1, &FdSet, nullptr, nullptr, &TimeVal);
	return ((result != 0) || ((result == -1) && (errno == ETIMEDOUT)));
}

////////////////////////////////////////////////////////////////////////////////
bool TcpSocketSend(UPTRINT Socket, const void* Data, uint32 Size)
{
	int Inner = Socket - 1;
	return send(Inner, (const char*)Data, Size, 0) == Size;
}

////////////////////////////////////////////////////////////////////////////////
int32 TcpSocketRecv(UPTRINT Socket, void* Data, uint32 Size)
{
	int Inner = Socket - 1;
	return recv(Inner, (char*)Data, Size, 0);
}

} // namespace Trace

#endif // UE_TRACE_ENABLED
