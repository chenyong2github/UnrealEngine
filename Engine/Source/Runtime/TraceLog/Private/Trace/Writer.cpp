// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "Platform.h"
#include "Trace/Detail/Atomic.h"
#include "Trace/Detail/Protocol.h"
#include "Trace/Detail/Channel.h"
#include "Trace/Trace.inl"
#include "WriteBufferRedirect.h"

#include <limits.h>
#include <stdlib.h>

namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
int32			Encode(const void*, int32, void*, int32);
uint32			Writer_SendData(uint32, uint8* __restrict, uint32);
void			Writer_InitializePool();
void			Writer_ShutdownPool();
void			Writer_DrainBuffers();
void			Writer_EndThreadBuffer();
void			Writer_UpdateControl();
void			Writer_InitializeControl();
void			Writer_ShutdownControl();



////////////////////////////////////////////////////////////////////////////////
UE_TRACE_EVENT_BEGIN($Trace, NewTrace, NoSync|Important)
	UE_TRACE_EVENT_FIELD(uint32, Serial)
	UE_TRACE_EVENT_FIELD(uint16, UserUidBias)
	UE_TRACE_EVENT_FIELD(uint16, Endian)
	UE_TRACE_EVENT_FIELD(uint8, PointerSize)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN($Trace, Timing, NoSync|Important)
	UE_TRACE_EVENT_FIELD(uint64, StartCycle)
	UE_TRACE_EVENT_FIELD(uint64, CycleFrequency)
UE_TRACE_EVENT_END()



////////////////////////////////////////////////////////////////////////////////
static bool						GInitialized;		// = false;
uint64							GStartCycle;		// = 0;
TRACELOG_API uint32 volatile	GLogSerial;			// = 0;



////////////////////////////////////////////////////////////////////////////////
enum EKnownThreadIds
{
	Tid_NewEvents,
	Tid_Header,
	Tid_Process		= 8,
};

////////////////////////////////////////////////////////////////////////////////
struct FWriteTlsContext
{
				~FWriteTlsContext();
	uint32		GetThreadId();

private:
	uint32		ThreadId = 0;
};

////////////////////////////////////////////////////////////////////////////////
FWriteTlsContext::~FWriteTlsContext()
{
	if (GInitialized)
	{
		Writer_EndThreadBuffer();
	}
}

////////////////////////////////////////////////////////////////////////////////
uint32 FWriteTlsContext::GetThreadId()
{
	if (ThreadId)
	{
		return ThreadId;
	}

	static uint32 volatile Counter;
	ThreadId = AtomicAddRelaxed(&Counter, 1u) + 1;
	return ThreadId + Tid_Process;
}

////////////////////////////////////////////////////////////////////////////////
thread_local FWriteTlsContext	GTlsContext;

////////////////////////////////////////////////////////////////////////////////
uint32 Writer_GetThreadId()
{
	return GTlsContext.GetThreadId();
}



////////////////////////////////////////////////////////////////////////////////
void* Writer_MemoryAllocate(SIZE_T Size, uint32 Alignment)
{
	TWriteBufferRedirect<6 << 10> TraceData;

	void* Ret = nullptr;
#if defined(_MSC_VER)
	Ret = _aligned_malloc(Size, Alignment);
#elif (defined(__ANDROID_API__) && __ANDROID_API__ < 28) || defined(__APPLE__)
	posix_memalign(&Ret, Alignment, Size);
#else
	Ret = aligned_alloc(Alignment, Size);
#endif

	if (TraceData.GetSize())
	{
		uint32 ThreadId = Writer_GetThreadId();
		Writer_SendData(ThreadId, TraceData.GetData(), TraceData.GetSize());
	}

	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
void Writer_MemoryFree(void* Address)
{
	TWriteBufferRedirect<6 << 10> TraceData;

#if defined(_MSC_VER)
	_aligned_free(Address);
#else
	free(Address);
#endif

	if (TraceData.GetSize())
	{
		uint32 ThreadId = Writer_GetThreadId();
		Writer_SendData(ThreadId, TraceData.GetData(), TraceData.GetSize());
	}
}



////////////////////////////////////////////////////////////////////////////////
static UPTRINT					GDataHandle;		// = 0
UPTRINT							GPendingDataHandle;	// = 0

////////////////////////////////////////////////////////////////////////////////
uint32 Writer_SendData(uint32 ThreadId, uint8* __restrict Data, uint32 Size)
{
	if (!GDataHandle)
	{
		return 0;
	}

	struct FPacketBase
	{
		uint16 PacketSize;
		uint16 ThreadId;
	};

	// Smaller buffers usually aren't redundant enough to benefit from being
	// compressed. They often end up being larger.
	if (Size <= 384)
	{
		static_assert(sizeof(FPacketBase) == sizeof(uint32), "");
		Data -= sizeof(FPacketBase);
		Size += sizeof(FPacketBase);
		auto* Packet = (FPacketBase*)Data;
		Packet->ThreadId = uint16(ThreadId & 0x7fff);
		Packet->PacketSize = uint16(Size);

		if (!IoWrite(GDataHandle, Data, Size))
		{
			IoClose(GDataHandle);
			GDataHandle = 0;
		}

		return Size;
	}

	struct FPacketEncoded
		: public FPacketBase
	{
		uint16	DecodedSize;
	};

	struct FPacket
		: public FPacketEncoded
	{
		// Buffer size is expressed as "A + B" where A is a maximum expected
		// input size (i.e. at least GPoolBlockSize) and B is LZ4 overhead as
		// per LZ4_COMPRESSBOUND.
		uint8 Data[8129 + 64];
	};

	FPacket Packet;
	Packet.ThreadId = 0x8000 | uint16(ThreadId & 0x7fff);
	Packet.DecodedSize = uint16(Size);
	Packet.PacketSize = Encode(Data, Packet.DecodedSize, Packet.Data, sizeof(Packet.Data));
	Packet.PacketSize += sizeof(FPacketEncoded);

	if (!IoWrite(GDataHandle, (uint8*)&Packet, Packet.PacketSize))
	{
		IoClose(GDataHandle);
		GDataHandle = 0;
	}

	return Packet.PacketSize;
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_DescribeAnnounce()
{
	if (!GDataHandle)
	{
		return;
	}

	// Describe new events
	{
		TWriteBufferRedirect<4096> TraceData;

		FEventNode::FIter Iter = FEventNode::ReadNew();
		while (const FEventNode* Event = Iter.GetNext())
		{
			Event->Describe();

			// Flush just in case an NewEvent event will be larger than 512 bytes.
			if (TraceData.GetSize() >= (TraceData.GetCapacity() - 512))
			{
				Writer_SendData(Tid_NewEvents, TraceData.GetData(), TraceData.GetSize());
				TraceData.Reset();
			}
		}

		if (TraceData.GetSize())
		{
			Writer_SendData(Tid_NewEvents, TraceData.GetData(), TraceData.GetSize());
		}
	}

	// Announce new channels
	FChannel::Iter Iter = FChannel::ReadNew();
	while (const FChannel* Channel = Iter.GetNext())
	{
		Channel->Announce();
	}
}



////////////////////////////////////////////////////////////////////////////////
static void Writer_LogHeader()
{
	UE_TRACE_LOG($Trace, NewTrace, TraceLogChannel)
		<< NewTrace.Serial(uint32(GLogSerial)) // should really atomic-load-relaxed here...
		<< NewTrace.UserUidBias(EKnownEventUids::User)
		<< NewTrace.Endian(0x524d)
		<< NewTrace.PointerSize(sizeof(void*));
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_LogTimingHeader()
{
	UE_TRACE_LOG($Trace, Timing, TraceLogChannel)
		<< Timing.StartCycle(GStartCycle)
		<< Timing.CycleFrequency(TimeGetFrequency());
}

////////////////////////////////////////////////////////////////////////////////
static bool Writer_UpdateData()
{
	if (!GPendingDataHandle)
	{
		return false;
	}

	// Reject the pending connection if we've already got a connection
	if (GDataHandle)
	{
		IoClose(GPendingDataHandle);
		GPendingDataHandle = 0;
		return false;
	}

	GDataHandle = GPendingDataHandle;
	GPendingDataHandle = 0;

	// Handshake.
	const uint32 Magic = 'TRCE';
	bool bOk = IoWrite(GDataHandle, &Magic, sizeof(Magic));

	// Stream header
	const struct {
		uint8 TransportVersion	= ETransport::TidPacket;
		uint8 ProtocolVersion	= EProtocol::Id;
	} TransportHeader;
	bOk &= IoWrite(GDataHandle, &TransportHeader, sizeof(TransportHeader));

	if (!bOk)
	{
		IoClose(GDataHandle);
		GDataHandle = 0;
		return false;
	}

	// Send the header events
	{
		TWriteBufferRedirect<512> HeaderEvents;
		Writer_LogHeader();
		Writer_LogTimingHeader();
		Writer_SendData(Tid_Header, HeaderEvents.GetData(), HeaderEvents.GetSize());
	}

	return true;
}



////////////////////////////////////////////////////////////////////////////////
static UPTRINT			GWorkerThread;		// = 0;
static volatile bool	GWorkerThreadQuit;	// = false;

////////////////////////////////////////////////////////////////////////////////
static void Writer_WorkerUpdate()
{
	Writer_UpdateControl();
	Writer_UpdateData();
	Writer_DescribeAnnounce();
	Writer_DrainBuffers();
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_WorkerThread()
{
	Trace::ThreadRegister(TEXT("Trace"), 0, INT_MAX);

	// At this point we haven't ever collected any trace events. So we'll stall
	// for just a little bit to give the user a chance to set up sending the trace
	// somewhere. This way they get all events since boot, otherwise they'll be
	// unceremoniously dropped.
	int32 PrologueMs = 2000;
	do
	{
		const uint32 SleepMs = 100;
		ThreadSleep(SleepMs);
		PrologueMs -= SleepMs;

		if (Writer_UpdateData())
		{
			break;
		}
	}
	while (PrologueMs > 0);

	while (!GWorkerThreadQuit)
	{
		Writer_WorkerUpdate();

		const uint32 SleepMs = 17;
		ThreadSleep(SleepMs);
	}

	Writer_DrainBuffers();
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_WorkerCreate()
{
	if (GWorkerThread)
	{
		return;
	}

	GWorkerThread = ThreadCreate("TraceWorker", Writer_WorkerThread);
}



////////////////////////////////////////////////////////////////////////////////
static void Writer_InternalInitializeImpl()
{
	if (GInitialized)
	{
		return;
	}

	GInitialized = true;
	GStartCycle = TimeGetTimestamp();

	Trace::ThreadRegister(TEXT("GameThread"), 0, -1);

	Writer_InitializePool();
	Writer_InitializeControl();
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_InternalShutdown()
{
	if (!GInitialized)
	{
		return;
	}

	if (GWorkerThread)
	{
		GWorkerThreadQuit = true;
		ThreadJoin(GWorkerThread);
		ThreadDestroy(GWorkerThread);
		GWorkerThread = 0;
	}

	Writer_ShutdownControl();
	Writer_ShutdownPool();

	GInitialized = false;
}

////////////////////////////////////////////////////////////////////////////////
void Writer_InternalInitialize()
{
	using namespace Private;

	if (!GInitialized)
	{
		static struct FInitializer
		{
			FInitializer()
			{
				Writer_InternalInitializeImpl();
			}
			~FInitializer()
			{
				Writer_InternalShutdown();
			}
		} Initializer;
	}
}

////////////////////////////////////////////////////////////////////////////////
void Writer_Initialize(const FInitializeDesc& Desc)
{
	if (Desc.bUseWorkerThread)
	{
		Writer_WorkerCreate();
	}
}

////////////////////////////////////////////////////////////////////////////////
void Writer_Update()
{
	if (!GWorkerThread)
	{
		Writer_WorkerUpdate();
	}
}

////////////////////////////////////////////////////////////////////////////////
static bool const bEnsureDynamicInit = [] () -> bool
{
	// Trace will register the thread it is initialised on as the main thread. On
	// the off chance that no events are trace during dynamic initialisation this
	// lambda will get called to cover that scenario. Of course, this cunning plan
	// may not work on some platforms or if TraceLog is loaded on another thread.
	Writer_InternalInitialize();
	return false;
}();



////////////////////////////////////////////////////////////////////////////////
bool Writer_SendTo(const ANSICHAR* Host, uint32 Port)
{
	if (GPendingDataHandle || GDataHandle)
	{
		return false;
	}

	Writer_InternalInitialize();

	Port = Port ? Port : 1980;
	UPTRINT DataHandle = TcpSocketConnect(Host, Port);
	if (!DataHandle)
	{
		return false;
	}

	GPendingDataHandle = DataHandle;
	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool Writer_WriteTo(const ANSICHAR* Path)
{
	if (GPendingDataHandle || GDataHandle)
	{
		return false;
	}

	Writer_InternalInitialize();

	UPTRINT DataHandle = FileOpen(Path);
	if (!DataHandle)
	{
		return false;
	}

	GPendingDataHandle = DataHandle;
	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool Writer_IsTracing()
{
	return (GDataHandle != 0);
}

} // namespace Private
} // namespace Trace

#endif // UE_TRACE_ENABLED
