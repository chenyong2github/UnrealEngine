// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "Platform.h"
#include "Trace/Detail/Atomic.h"
#include "Trace/Detail/Channel.h"
#include "Trace/Detail/Protocol.h"
#include "Trace/Detail/Transport.h"
#include "Trace/Trace.inl"
#include "WriteBufferRedirect.h"

#include <limits.h>
#include <stdlib.h>

#if PLATFORM_WINDOWS
#	define TRACE_PRIVATE_STOMP 0 // 1=overflow, 2=underflow
#	if TRACE_PRIVATE_STOMP
#	include "Windows/AllowWindowsPlatformTypes.h"
#		include "Windows/WindowsHWrapper.h"
#	include "Windows/HideWindowsPlatformTypes.h"
#	endif
#else
#	define TRACE_PRIVATE_STOMP 0
#endif

namespace UE {
namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
int32			Encode(const void*, int32, void*, int32);
void			Writer_SendData(uint32, uint8* __restrict, uint32);
void			Writer_InitializeSharedBuffers();
void			Writer_ShutdownSharedBuffers();
void			Writer_UpdateSharedBuffers();
void			Writer_CacheOnConnect();
void			Writer_InitializePool();
void			Writer_ShutdownPool();
void			Writer_DrainBuffers();
void			Writer_EndThreadBuffer();
uint32			Writer_GetControlPort();
void			Writer_UpdateControl();
void			Writer_InitializeControl();
void			Writer_ShutdownControl();



////////////////////////////////////////////////////////////////////////////////
UE_TRACE_EVENT_BEGIN($Trace, NewTrace, NoSync)
	UE_TRACE_EVENT_FIELD(uint64, StartCycle)
	UE_TRACE_EVENT_FIELD(uint64, CycleFrequency)
	UE_TRACE_EVENT_FIELD(uint32, Serial)
	UE_TRACE_EVENT_FIELD(uint16, UserUidBias)
	UE_TRACE_EVENT_FIELD(uint16, Endian)
	UE_TRACE_EVENT_FIELD(uint8, PointerSize)
	UE_TRACE_EVENT_FIELD(uint8, FeatureSet)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN($Trace, SerialSync, NoSync)
UE_TRACE_EVENT_END()



////////////////////////////////////////////////////////////////////////////////
static bool						GInitialized;		// = false;
FStatistics						GTraceStatistics;	// = {};
uint64							GStartCycle;		// = 0;
TRACELOG_API uint32 volatile	GLogSerial;			// = 0;



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
	ThreadId = AtomicAddRelaxed(&Counter, 1u) + ETransportTid::Bias;
	return ThreadId;
}

////////////////////////////////////////////////////////////////////////////////
thread_local FWriteTlsContext	GTlsContext;

////////////////////////////////////////////////////////////////////////////////
uint32 Writer_GetThreadId()
{
	return GTlsContext.GetThreadId();
}



////////////////////////////////////////////////////////////////////////////////
void*			(*AllocHook)(SIZE_T, uint32);			// = nullptr
void			(*FreeHook)(void*, SIZE_T);				// = nullptr

////////////////////////////////////////////////////////////////////////////////
void Writer_MemorySetHooks(decltype(AllocHook) Alloc, decltype(FreeHook) Free)
{
	AllocHook = Alloc;
	FreeHook = Free;
}

////////////////////////////////////////////////////////////////////////////////
void* Writer_MemoryAllocate(SIZE_T Size, uint32 Alignment)
{
	TWriteBufferRedirect<6 << 10> TraceData;

	void* Ret = nullptr;

#if TRACE_PRIVATE_STOMP
	static uint8* Base;
	if (Base == nullptr)
	{
		Base = (uint8*)VirtualAlloc(0, 1ull << 40, MEM_RESERVE, PAGE_READWRITE);
	}

	static SIZE_T PageSize = 4096;
	Base += PageSize;
	uint8* NextBase = Base + ((PageSize - 1 + Size) & ~(PageSize - 1));
	VirtualAlloc(Base, SIZE_T(NextBase - Base), MEM_COMMIT, PAGE_READWRITE);
#if TRACE_PRIVATE_STOMP == 1
	Ret = NextBase - Size;
#elif TRACE_PRIVATE_STOMP == 2
	Ret = Base;
#endif
	Base = NextBase;
#else // TRACE_PRIVATE_STOMP

	if (AllocHook != nullptr)
	{
		Ret = AllocHook(Size, Alignment);
	}
	else
	{
#if defined(_MSC_VER)
		Ret = _aligned_malloc(Size, Alignment);
#elif (defined(__ANDROID_API__) && __ANDROID_API__ < 28) || defined(__APPLE__)
		posix_memalign(&Ret, Alignment, Size);
#else
		Ret = aligned_alloc(Alignment, Size);
#endif
	}
#endif // TRACE_PRIVATE_STOMP

	if (TraceData.GetSize())
	{
		uint32 ThreadId = Writer_GetThreadId();
		Writer_SendData(ThreadId, TraceData.GetData(), TraceData.GetSize());
	}

#if TRACE_PRIVATE_STATISTICS
	AtomicAddRelaxed(&GTraceStatistics.MemoryUsed, uint32(Size));
#endif

	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
void Writer_MemoryFree(void* Address, uint32 Size)
{
#if TRACE_PRIVATE_STOMP
	if (Address == nullptr)
	{
		return;
	}

	*(uint8*)Address = 0xfe;

	MEMORY_BASIC_INFORMATION MemInfo;
	VirtualQuery(Address, &MemInfo, sizeof(MemInfo));

	DWORD Unused;
	VirtualProtect(MemInfo.BaseAddress, MemInfo.RegionSize, PAGE_READONLY, &Unused);
#else // TRACE_PRIVATE_STOMP
	TWriteBufferRedirect<6 << 10> TraceData;

	if (FreeHook != nullptr)
	{
		FreeHook(Address, Size);
	}
	else
	{
#if defined(_MSC_VER)
		_aligned_free(Address);
#else
		free(Address);
#endif
	}

	if (TraceData.GetSize())
	{
		uint32 ThreadId = Writer_GetThreadId();
		Writer_SendData(ThreadId, TraceData.GetData(), TraceData.GetSize());
	}
#endif // TRACE_PRIVATE_STOMP

#if TRACE_PRIVATE_STATISTICS
	AtomicAddRelaxed(&GTraceStatistics.MemoryUsed, uint32(-int64(Size)));
#endif
}



////////////////////////////////////////////////////////////////////////////////
static UPTRINT					GDataHandle;		// = 0
static bool						GSerialSyncPending;	// = false
UPTRINT							GPendingDataHandle;	// = 0

////////////////////////////////////////////////////////////////////////////////
static void Writer_SendDataImpl(const void* Data, uint32 Size)
{
#if TRACE_PRIVATE_STATISTICS
	GTraceStatistics.BytesSent += Size;
#endif

	if (!IoWrite(GDataHandle, Data, Size))
	{
		IoClose(GDataHandle);
		GDataHandle = 0;
	}
}

////////////////////////////////////////////////////////////////////////////////
void Writer_SendDataRaw(const void* Data, uint32 Size)
{
	if (!GDataHandle)
	{
		return;
	}

	Writer_SendDataImpl(Data, Size);
}

////////////////////////////////////////////////////////////////////////////////
void Writer_SendData(uint32 ThreadId, uint8* __restrict Data, uint32 Size)
{
	static_assert(ETransport::Active == ETransport::TidPacket, "Active should be set to what the compiled code uses. It is used to track places that assume transport packet format");

#if TRACE_PRIVATE_STATISTICS
	GTraceStatistics.BytesTraced += Size;
#endif

	if (!GDataHandle)
	{
		return;
	}

	// Smaller buffers usually aren't redundant enough to benefit from being
	// compressed. They often end up being larger.
	if (Size <= 384)
	{
		Data -= sizeof(FTidPacket);
		Size += sizeof(FTidPacket);
		auto* Packet = (FTidPacket*)Data;
		Packet->ThreadId = uint16(ThreadId & 0x7fff);
		Packet->PacketSize = uint16(Size);

		Writer_SendDataImpl(Data, Size);
		return;
	}

	// Buffer size is expressed as "A + B" where A is a maximum expected
	// input size (i.e. at least GPoolBlockSize) and B is LZ4 overhead as
	// per LZ4_COMPRESSBOUND.
	TTidPacketEncoded<8192 + 64> Packet;

	Packet.ThreadId = 0x8000 | uint16(ThreadId & 0x7fff);
	Packet.DecodedSize = uint16(Size);
	Packet.PacketSize = Encode(Data, Packet.DecodedSize, Packet.Data, sizeof(Packet.Data));
	Packet.PacketSize += sizeof(FTidPacketEncoded);

	Writer_SendDataImpl(&Packet, Packet.PacketSize);
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_DescribeEvents()
{
	TWriteBufferRedirect<4096> TraceData;

	FEventNode::FIter Iter = FEventNode::ReadNew();
	while (const FEventNode* Event = Iter.GetNext())
	{
		Event->Describe();

		// Flush just in case an NewEvent event will be larger than 512 bytes.
		if (TraceData.GetSize() >= (TraceData.GetCapacity() - 512))
		{
			Writer_SendData(ETransportTid::Events, TraceData.GetData(), TraceData.GetSize());
			TraceData.Reset();
		}
	}

	if (TraceData.GetSize())
	{
		Writer_SendData(ETransportTid::Events, TraceData.GetData(), TraceData.GetSize());
	}
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_AnnounceChannels()
{
	FChannel::Iter Iter = FChannel::ReadNew();
	while (const FChannel* Channel = Iter.GetNext())
	{
		Channel->Announce();
	}
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_DescribeAnnounce()
{
	if (!GDataHandle)
	{
		return;
	}

	Writer_AnnounceChannels();
	Writer_DescribeEvents();
}



////////////////////////////////////////////////////////////////////////////////
static void Writer_LogHeader()
{
	UE_TRACE_LOG($Trace, NewTrace, TraceLogChannel)
		<< NewTrace.Serial(AtomicLoadRelaxed(&GLogSerial))
		<< NewTrace.UserUidBias(EKnownEventUids::User)
		<< NewTrace.Endian(0x524d)
		<< NewTrace.PointerSize(sizeof(void*))
		<< NewTrace.StartCycle(GStartCycle)
		<< NewTrace.CycleFrequency(TimeGetFrequency())
		<< NewTrace.FeatureSet(1);
}

////////////////////////////////////////////////////////////////////////////////
static bool Writer_UpdateConnection()
{
	if (!GPendingDataHandle)
	{
		return false;
	}

	// Is this a close request?
	if (GPendingDataHandle == ~0ull)
	{
		if (GDataHandle)
		{
			IoClose(GDataHandle);
		}

		GDataHandle = 0;
		GPendingDataHandle = 0;
	}

	// Reject the pending connection if we've already got a connection
	if (GDataHandle)
	{
		IoClose(GPendingDataHandle);
		GPendingDataHandle = 0;
		GSerialSyncPending = false;
		return false;
	}

	GDataHandle = GPendingDataHandle;
	GPendingDataHandle = 0;

	// Handshake.
	struct FHandshake
	{
		uint32 Magic			= 'TRC2';
		uint16 MetadataSize		= uint16(4); //  = sizeof(MetadataField0 + ControlPort)
		uint16 MetadataField0	= uint16(sizeof(ControlPort) | (ControlPortFieldId << 8));
		uint16 ControlPort		= uint16(Writer_GetControlPort());
		enum
		{
			Size				= 10,
			ControlPortFieldId	= 0,
		};
	};
	FHandshake Handshake;
	bool bOk = IoWrite(GDataHandle, &Handshake, FHandshake::Size);

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

	// Reset statistics.
	GTraceStatistics.BytesSent = 0;
	GTraceStatistics.BytesTraced = 0;

	// The first events we will send are ones that describe the trace's events
	FEventNode::OnConnect();
	Writer_DescribeEvents();

	// Send the header event
	TWriteBufferRedirect<512> HeaderEvents;
	Writer_LogHeader();
	HeaderEvents.Close();
	Writer_SendData(ETransportTid::Internal, HeaderEvents.GetData(), HeaderEvents.GetSize());

	Writer_CacheOnConnect();

	GSerialSyncPending = true;
	return true;
}



////////////////////////////////////////////////////////////////////////////////
static UPTRINT			GWorkerThread;		// = 0;
static volatile bool	GWorkerThreadQuit;	// = false;

////////////////////////////////////////////////////////////////////////////////
static void Writer_WorkerUpdate()
{
	Writer_UpdateControl();
	Writer_UpdateConnection();
	Writer_DescribeAnnounce();
	Writer_UpdateSharedBuffers();
	Writer_DrainBuffers();

	if (GSerialSyncPending)
	{
		GSerialSyncPending = false;

		// When analysis receives the "serial sync" event the starting serial
		// can be established from preceding synced events.
		TWriteBufferRedirect<32> SideBuffer;
		UE_TRACE_LOG($Trace, SerialSync, TraceLogChannel);
		Writer_SendData(ETransportTid::Internal, SideBuffer.GetData(), SideBuffer.GetSize());
	}
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_WorkerThread()
{
	ThreadRegister(TEXT("Trace"), 0, INT_MAX);

	Writer_UpdateControl();

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

		if (Writer_UpdateConnection())
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
static void Writer_WorkerJoin()
{
	if (!GWorkerThread)
	{
		return;
	}

	GWorkerThreadQuit = true;
	ThreadJoin(GWorkerThread);
	ThreadDestroy(GWorkerThread);

	Writer_WorkerUpdate();

	GWorkerThread = 0;
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

	Writer_InitializeSharedBuffers();
	Writer_InitializePool();
	Writer_InitializeControl();

	// The order of the events at the beginning of a trace is a little sensitive,
	// especially for the two following events that are added to the trace stream
	// at very specific points. By dummy-tracing them we invoke the code to IDs
	// and can guarantee the events are described on connection. It's not pretty.
	{
		TWriteBufferRedirect<64> TempBuffer;
		UE_TRACE_LOG($Trace, NewTrace, TraceLogChannel);
		UE_TRACE_LOG($Trace, SerialSync, TraceLogChannel);
	}
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_InternalShutdown()
{
	if (!GInitialized)
	{
		return;
	}

	Writer_WorkerJoin();

	if (GDataHandle)
	{
		IoClose(GDataHandle);
		GDataHandle = 0;
	}

	Writer_ShutdownControl();
	Writer_ShutdownPool();
	Writer_ShutdownSharedBuffers();

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
				/* We'll not shut anything down here so we can hopefully capture
				 * any subsequent events. However, we will shutdown the worker
				 * thread and leave it for something else to call update() (mem
				 * tracing at time of writing). Windows will have already done
				 * this implicitly in ExitProcess() anyway. */
				Writer_WorkerJoin();
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
void Writer_Shutdown()
{
	Writer_InternalShutdown();
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

////////////////////////////////////////////////////////////////////////////////
bool Writer_Stop()
{
	if (GPendingDataHandle || !GDataHandle)
	{
		return false;
	}

	GPendingDataHandle = ~UPTRINT(0);
	return true;
}

} // namespace Private
} // namespace Trace
} // namespace UE

#endif // UE_TRACE_ENABLED
