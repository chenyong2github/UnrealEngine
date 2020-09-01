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
int32	Encode(const void*, int32, void*, int32);
void	Writer_UpdateControl();
void	Writer_InitializeControl();
void	Writer_ShutdownControl();



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

UE_TRACE_EVENT_BEGIN($Trace, ThreadTiming, NoSync|Important)
	UE_TRACE_EVENT_FIELD(uint64, BaseTimestamp)
UE_TRACE_EVENT_END()

#define TRACE_PRIVATE_PERF 0
#if TRACE_PRIVATE_PERF
UE_TRACE_EVENT_BEGIN($Trace, WorkerThread)
	UE_TRACE_EVENT_FIELD(uint32, Cycles)
	UE_TRACE_EVENT_FIELD(uint32, BytesReaped)
	UE_TRACE_EVENT_FIELD(uint32, BytesSent)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN($Trace, Memory)
	UE_TRACE_EVENT_FIELD(uint32, AllocSize)
UE_TRACE_EVENT_END()
#endif // TRACE_PRIVATE_PERF



////////////////////////////////////////////////////////////////////////////////
static bool						GInitialized;		// = false;
static uint64					GStartCycle;		// = 0;
static FWriteBuffer				GNullWriteBuffer	= { 0, 0, 0, 0, nullptr, nullptr, (uint8*)&GNullWriteBuffer };
thread_local FWriteBuffer*		GTlsWriteBuffer		= &GNullWriteBuffer;
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
	if (GInitialized && GTlsWriteBuffer != &GNullWriteBuffer)
	{
		UPTRINT EtxOffset = UPTRINT((uint8*)GTlsWriteBuffer - GTlsWriteBuffer->Cursor);
		AtomicStoreRelaxed(&(GTlsWriteBuffer->EtxOffset), EtxOffset);
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
static uint32 Writer_SendData(uint32, uint8* __restrict, uint32);

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
		uint32 ThreadId = GTlsContext.GetThreadId();
		Writer_SendData(ThreadId, TraceData.GetData(), TraceData.GetSize());
	}

	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
void Writer_MemoryFree(void* Address, SIZE_T Size, uint32 Alignment)
{
	TWriteBufferRedirect<6 << 10> TraceData;

#if defined(_MSC_VER)
	_aligned_free(Address);
#else
	free(Address);
#endif

	if (TraceData.GetSize())
	{
		uint32 ThreadId = GTlsContext.GetThreadId();
		Writer_SendData(ThreadId, TraceData.GetData(), TraceData.GetSize());
	}
}



////////////////////////////////////////////////////////////////////////////////
struct FPoolPage
{
	FPoolPage*	NextPage;
	uint32		AllocSize;
};

////////////////////////////////////////////////////////////////////////////////
#define T_ALIGN alignas(PLATFORM_CACHE_LINE_SIZE)
static const uint32						GPoolBlockSize		= 4 << 10;
static const uint32						GPoolPageSize		= GPoolBlockSize << 4;
static const uint32						GPoolInitPageSize	= GPoolBlockSize << 6;
T_ALIGN static FWriteBuffer* volatile	GPoolFreeList;		// = nullptr;
T_ALIGN static UPTRINT volatile			GPoolFutex;			// = 0
T_ALIGN static FWriteBuffer* volatile	GNewThreadList;		// = nullptr;
T_ALIGN static FPoolPage* volatile		GPoolPageList;		// = nullptr;
static uint32							GPoolUsage;			// = 0;
static uint32							GPoolUsageThreshold;// = 0;
#undef T_ALIGN

////////////////////////////////////////////////////////////////////////////////
#if !IS_MONOLITHIC
TRACELOG_API FWriteBuffer* Writer_GetBuffer()
{
	// Thread locals and DLLs don't mix so for modular builds we are forced to
	// export this function to access thread-local variables.
	return GTlsWriteBuffer;
}
#endif

////////////////////////////////////////////////////////////////////////////////
static FWriteBuffer* Writer_NextBufferInternal(uint32 PageSize)
{
	// Fetch a new buffer
	FWriteBuffer* NextBuffer;
	while (true)
	{
		// First we'll try one from the free list
		FWriteBuffer* Owned = AtomicLoadRelaxed(&GPoolFreeList);
		if (Owned != nullptr)
		{
			if (!AtomicCompareExchangeRelaxed(&GPoolFreeList, Owned->NextBuffer, Owned))
			{
				PlatformYield();
				continue;
			}
		}

		// If we didn't fetch the sentinal then we've taken a block we can use
		if (Owned != nullptr)
		{
			NextBuffer = (FWriteBuffer*)Owned;
			break;
		}

		// Throttle back if memory usage is growing past a comfortable threshold
		if (GPoolUsageThreshold && (GPoolUsage > GPoolUsageThreshold))
		{
			/* TODO: If there's no worker thread, now what? Force update maybe... */
			ThreadSleep(0);
			continue;
		}

		// The free list is empty. Map some more memory.
		UPTRINT Futex = AtomicLoadRelaxed(&GPoolFutex);
		if (Futex || !AtomicCompareExchangeAcquire(&GPoolFutex, Futex + 1, Futex))
		{
			// Someone else is mapping memory so we'll briefly yield and try the
			// free list again.
			ThreadSleep(0);
			continue;
		}

		// The free list is empty so we have to populate it with some new blocks.
		uint8* PageBase = (uint8*)Writer_MemoryAllocate(PageSize, PLATFORM_CACHE_LINE_SIZE);
		GPoolUsage += PageSize;

		uint32 BufferSize = GPoolBlockSize;
		BufferSize -= sizeof(FWriteBuffer);
		BufferSize -= sizeof(uint32); // to preceed event data with a small header when sending.

		// The first block in the page we'll use for the next buffer. Note that the
		// buffer objects are at the _end_ of their blocks.
		NextBuffer = (FWriteBuffer*)(PageBase + GPoolBlockSize - sizeof(FWriteBuffer));
		NextBuffer->Size = BufferSize;

		// Link subsequent blocks together
		uint8* FirstBlock = (uint8*)NextBuffer + GPoolBlockSize;
		uint8* Block = FirstBlock;
		for (int i = 2, n = PageSize / GPoolBlockSize; ; ++i)
		{
			auto* Buffer = (FWriteBuffer*)Block;
			Buffer->Size = BufferSize;
			if (i >= n)
			{
				break;
			}

			Buffer->NextBuffer = (FWriteBuffer*)(Block + GPoolBlockSize);
			Block += GPoolBlockSize;
		}

		// Keep track of allocation base so we can free it on shutdown
		NextBuffer->Size -= sizeof(FPoolPage);
		FPoolPage* PageListNode = (FPoolPage*)PageBase;
		PageListNode->NextPage = GPoolPageList;
		GPoolPageList = PageListNode;

		// And insert the block list into the freelist. 'Block' is now the last block
		for (auto* ListNode = (FWriteBuffer*)Block;; PlatformYield())
		{
			ListNode->NextBuffer = AtomicLoadRelaxed(&GPoolFreeList);
			if (AtomicCompareExchangeRelease(&GPoolFreeList, (FWriteBuffer*)FirstBlock, ListNode->NextBuffer))
			{
				break;
			}
		}

		for (;; Private::PlatformYield())
		{
			if (AtomicCompareExchangeRelease<UPTRINT>(&GPoolFutex, 0, 1))
			{
				break;
			}
		}

		break;
	}

	NextBuffer->Cursor = (uint8*)NextBuffer - NextBuffer->Size;
	NextBuffer->Committed = NextBuffer->Cursor;
	NextBuffer->Reaped = NextBuffer->Cursor;
	NextBuffer->EtxOffset = UPTRINT(0) - sizeof(FWriteBuffer);
	NextBuffer->NextBuffer = nullptr;

	FWriteBuffer* CurrentBuffer = GTlsWriteBuffer;
	if (CurrentBuffer == &GNullWriteBuffer)
	{
		NextBuffer->ThreadId = uint16(GTlsContext.GetThreadId());
		NextBuffer->PrevTimestamp = TimeGetTimestamp();

		GTlsWriteBuffer = NextBuffer;

		UE_TRACE_LOG($Trace, ThreadTiming, TraceLogChannel)
			<< ThreadTiming.BaseTimestamp(NextBuffer->PrevTimestamp - GStartCycle);

		// Add this next buffer to the active list.
		for (;; PlatformYield())
		{
			NextBuffer->NextThread = AtomicLoadRelaxed(&GNewThreadList);
			if (AtomicCompareExchangeRelease(&GNewThreadList, NextBuffer, NextBuffer->NextThread))
			{
				break;
			}
		}
	}
	else
	{
		CurrentBuffer->NextBuffer = NextBuffer;
		NextBuffer->ThreadId = CurrentBuffer->ThreadId;
		NextBuffer->PrevTimestamp = CurrentBuffer->PrevTimestamp;

		GTlsWriteBuffer = NextBuffer;

		// Retire current buffer.
		UPTRINT EtxOffset = UPTRINT((uint8*)(CurrentBuffer) - CurrentBuffer->Cursor);
		AtomicStoreRelease(&(CurrentBuffer->EtxOffset), EtxOffset);
	}

	return NextBuffer;
}

////////////////////////////////////////////////////////////////////////////////
TRACELOG_API FWriteBuffer* Writer_NextBuffer(int32 Size)
{
	if (Size >= GPoolBlockSize - sizeof(FWriteBuffer))
	{
		/* Someone is trying to write an event that is too large */
		return nullptr;
	}

	FWriteBuffer* CurrentBuffer = GTlsWriteBuffer;
	if (CurrentBuffer != &GNullWriteBuffer)
	{
		CurrentBuffer->Cursor -= Size;
	}

	FWriteBuffer* NextBuffer = Writer_NextBufferInternal(GPoolPageSize);

	NextBuffer->Cursor += Size;
	return NextBuffer;
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_InitializeBuffers()
{
	Writer_NextBufferInternal(GPoolInitPageSize);

	static_assert(GPoolPageSize >= 0x10000, "Page growth must be >= 64KB");
	static_assert(GPoolInitPageSize >= 0x10000, "Initial page size must be >= 64KB");
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_ShutdownBuffers()
{
	// Claim ownership of the pool page list. There really should be no one
	// creating so we'll just read it an go instead of a CAS loop.
	for (auto* Page = AtomicLoadRelaxed(&GPoolPageList); Page != nullptr;)
	{
		FPoolPage* NextPage = Page->NextPage;
		Writer_MemoryFree(Page, Page->AllocSize, PLATFORM_CACHE_LINE_SIZE);
		Page = NextPage;
	}
}



////////////////////////////////////////////////////////////////////////////////
static UPTRINT					GDataHandle;		// = 0
UPTRINT							GPendingDataHandle;	// = 0
static FWriteBuffer* __restrict GActiveThreadList;	// = nullptr;

////////////////////////////////////////////////////////////////////////////////
static uint32 Writer_SendData(uint32 ThreadId, uint8* __restrict Data, uint32 Size)
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
		uint8 Data[GPoolBlockSize + 64];
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
static void Writer_ConsumeEvents()
{
	struct FRetireList
	{
		FWriteBuffer* __restrict Head = nullptr;
		FWriteBuffer* __restrict Tail = nullptr;

		void Insert(FWriteBuffer* __restrict Buffer)
		{
			Buffer->NextBuffer = Head;
			Head = Buffer;
			Tail = (Tail != nullptr) ? Tail : Head;
		}
	};

#if TRACE_PRIVATE_PERF
	uint64 StartTsc = TimeGetTimestamp();
	uint32 BytesReaped = 0;
	uint32 BytesSent = 0;
#endif

	// Claim ownership of any new thread buffer lists
	FWriteBuffer* __restrict NewThreadList;
	for (;; PlatformYield())
	{
		NewThreadList = AtomicLoadRelaxed(&GNewThreadList);
		if (AtomicCompareExchangeAcquire(&GNewThreadList, (FWriteBuffer*)nullptr, NewThreadList))
		{
			break;
		}
	}

	// Reverse the new threads list so they're more closely ordered by age
	// when sent out.
	FWriteBuffer* __restrict NewThreadCursor = NewThreadList;
	NewThreadList = nullptr;
	while (NewThreadCursor != nullptr)
	{
		FWriteBuffer* __restrict NextThread = NewThreadCursor->NextThread;

		NewThreadCursor->NextThread = NewThreadList;
		NewThreadList = NewThreadCursor;

		NewThreadCursor = NextThread;
	}

	FRetireList RetireList;

	FWriteBuffer* __restrict ActiveThreadList = GActiveThreadList;
	GActiveThreadList = nullptr;

	// Now we've two lists of known and new threads. Each of these lists in turn is
	// a list of that thread's buffers (where it is writing trace events to).
	for (FWriteBuffer* __restrict Buffer : { ActiveThreadList, NewThreadList })
	{
		// For each thread...
		for (FWriteBuffer* __restrict NextThread; Buffer != nullptr; Buffer = NextThread)
		{
			NextThread = Buffer->NextThread;
			uint32 ThreadId = Buffer->ThreadId;

			// For each of the thread's buffers...
			for (FWriteBuffer* __restrict NextBuffer; Buffer != nullptr; Buffer = NextBuffer)
			{
				uint8* Committed = AtomicLoadRelaxed((uint8**)&Buffer->Committed);

				// Send as much as we can.
				if (uint32 SizeToReap = uint32(Committed - Buffer->Reaped))
				{
#if TRACE_PRIVATE_PERF
					BytesReaped += SizeToReap;
					BytesSent += /*...*/
#endif
					Writer_SendData(ThreadId, Buffer->Reaped, SizeToReap);
					Buffer->Reaped = Committed;
				}

				// Is this buffer still in use?
				int32 EtxOffset = int32(AtomicLoadAcquire(&Buffer->EtxOffset));
				if ((uint8*)Buffer - EtxOffset > Committed)
				{
					break;
				}

				// Retire the buffer
				NextBuffer = Buffer->NextBuffer;
				RetireList.Insert(Buffer);
			}

			if (Buffer != nullptr)
			{
				Buffer->NextThread = GActiveThreadList;
				GActiveThreadList = Buffer;
			}
		}
	}

#if TRACE_PRIVATE_PERF
	UE_TRACE_LOG($Trace, WorkerThread, TraceLogChannel)
		<< WorkerThread.Cycles(uint32(TimeGetTimestamp() - StartTsc))
		<< WorkerThread.BytesReaped(BytesReaped)
		<< WorkerThread.BytesSent(BytesSent);

	UE_TRACE_LOG($Trace, Memory, TraceLogChannel)
		<< Memory.AllocSize(GPoolUsage);
#endif // TRACE_PRIVATE_PERF

	// Put the retirees we found back into the system again.
	if (RetireList.Head != nullptr)
	{
		for (FWriteBuffer* ListNode = RetireList.Tail;; PlatformYield())
		{
			ListNode->NextBuffer = AtomicLoadRelaxed(&GPoolFreeList);
			if (AtomicCompareExchangeRelease(&GPoolFreeList, RetireList.Head, ListNode->NextBuffer))
			{
				break;
			}
		}
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
	Trace::ThreadRegister(TEXT("Trace"), 0, INT_MAX);

	Writer_UpdateControl();
	Writer_UpdateData();
	Writer_DescribeAnnounce();
	Writer_ConsumeEvents();
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_WorkerThread()
{
	// At this point we haven't never collected any trace events. So we'll stall
	// for just a little bit to give the user a chance to set up sending the trace
	// somewhere and we they'll get all events since boot. Otherwise they'll be
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

	Writer_ConsumeEvents();
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

	Trace::ThreadRegister(TEXT("MainThread"), 0, -1);

	Writer_InitializeBuffers();
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
	Writer_ShutdownBuffers();

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

	GPoolUsageThreshold = Desc.MaxMemoryHintMb;
	if (GPoolUsageThreshold > 2 << 10)
	{
		GPoolUsageThreshold = 2 << 10;
	}
	GPoolUsageThreshold <<= 20;
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

} // namespace Private
} // namespace Trace

#endif // UE_TRACE_ENABLED
