// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "Trace/Detail/Atomic.h"
#include "Trace/Detail/Protocol.h"
#include "Trace/Detail/Channel.h"
#include "Trace/Platform.h"
#include "Trace/Trace.h"

namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
int32	Encode(const void*, int32, void*, int32);
void	Writer_UpdateControl();
void	Writer_InitializeControl();
void	Writer_ShutdownControl();



////////////////////////////////////////////////////////////////////////////////
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
static uint64 GStartCycle = 0;

////////////////////////////////////////////////////////////////////////////////
inline uint64 Writer_GetTimestamp()
{
	return TimeGetTimestamp() - GStartCycle;
}

////////////////////////////////////////////////////////////////////////////////
void Writer_InitializeTiming()
{
	GStartCycle = TimeGetTimestamp();

	UE_TRACE_EVENT_BEGIN($Trace, Timing, Important)
		UE_TRACE_EVENT_FIELD(uint64, StartCycle)
		UE_TRACE_EVENT_FIELD(uint64, CycleFrequency)
	UE_TRACE_EVENT_END()

	UE_TRACE_LOG($Trace, Timing, TraceLogChannel)
		<< Timing.StartCycle(GStartCycle)
		<< Timing.CycleFrequency(TimeGetFrequency());
}



////////////////////////////////////////////////////////////////////////////////
static bool						GInitialized;		// = false;
static FWriteBuffer				GNullWriteBuffer	= { 0, 0, nullptr, nullptr, (uint8*)&GNullWriteBuffer };
thread_local FWriteBuffer*		GTlsWriteBuffer		= &GNullWriteBuffer;
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
	ThreadId = AtomicIncrementRelaxed(&Counter) + 1;
	return ThreadId;
}

////////////////////////////////////////////////////////////////////////////////
thread_local FWriteTlsContext	GTlsContext;



////////////////////////////////////////////////////////////////////////////////
#define T_ALIGN alignas(PLATFORM_CACHE_LINE_SIZE)
static const uint32						GPoolSize			= 384 << 20; // 384MB ought to be enough
static const uint32						GPoolBlockSize		= 4 << 10;
static const uint32						GPoolPageGrowth		= GPoolBlockSize << 5;
static const uint32						GPoolInitPageSize	= GPoolBlockSize << 5;
static uint8*							GPoolBase;			// = nullptr;
T_ALIGN static uint8* volatile			GPoolPageCursor;	// = nullptr;
T_ALIGN static FWriteBuffer* volatile	GPoolFreeList;		// = nullptr;
T_ALIGN static FWriteBuffer* volatile	GNewThreadList;		// = nullptr;
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
static FWriteBuffer* Writer_NextBufferInternal(uint32 PageGrowth)
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
				Private::PlatformYield();
				continue;
			}
		}

		// If we didn't fetch the sentinal then we've taken a block we can use
		if (Owned != nullptr)
		{
			NextBuffer = (FWriteBuffer*)Owned;
			break;
		}

		// The free list is empty. Map some more memory.
		uint8* PageBase = (uint8*)AtomicLoadRelaxed(&GPoolPageCursor);
		if (!AtomicCompareExchangeAcquire(&GPoolPageCursor, PageBase + PageGrowth, PageBase))
		{
			// Someone else is mapping memory so we'll briefly yield and try the
			// free list again.
			Private::PlatformYield();
			continue;
		}

		// We claimed the pool cursor so it is now our job to map memory and add
		// it to the free list.
		MemoryMap(PageBase, PageGrowth);

		// The first block in the page we'll use for the next buffer. Note that the
		// buffer objects are at the _end_ of their blocks.
		PageBase += GPoolBlockSize - sizeof(FWriteBuffer);
		NextBuffer = (FWriteBuffer*)PageBase;
		uint8* FirstBlock = PageBase + GPoolBlockSize;

		// Link subsequent blocks together
		uint8* Block = FirstBlock;
		for (int i = 2, n = PageGrowth / GPoolBlockSize; i < n; ++i)
		{
			auto* Buffer = (FWriteBuffer*)Block;
			Buffer->NextBuffer = (FWriteBuffer*)(Block + GPoolBlockSize);
			Block += GPoolBlockSize;
		}

		// And insert the block list into the freelist. 'Block' is now the last block
		for (auto* ListNode = (FWriteBuffer*)Block;; Private::PlatformYield())
		{
			ListNode->NextBuffer = AtomicLoadRelaxed(&GPoolFreeList);
			if (AtomicCompareExchangeRelease(&GPoolFreeList, (FWriteBuffer*)FirstBlock, ListNode->NextBuffer))
			{
				break;
			}
		}

		break;
	}

	NextBuffer->Cursor = ((uint8*)NextBuffer - GPoolBlockSize + sizeof(FWriteBuffer));
	NextBuffer->Cursor += sizeof(uint32); // this is so we can preceed event data with a small header when sending.
	NextBuffer->Committed = NextBuffer->Cursor;
	NextBuffer->Reaped = NextBuffer->Cursor;
	NextBuffer->EtxOffset = UPTRINT(0) - sizeof(FWriteBuffer);
	NextBuffer->NextBuffer = nullptr;


	FWriteBuffer* CurrentBuffer = GTlsWriteBuffer;
	if (CurrentBuffer == &GNullWriteBuffer)
	{
		NextBuffer->ThreadId = GTlsContext.GetThreadId();

		// Add this next buffer to the active list.
		for (;; Private::PlatformYield())
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

		// Retire current buffer.
		UPTRINT EtxOffset = UPTRINT((uint8*)(CurrentBuffer) - CurrentBuffer->Cursor);
		AtomicStoreRelease(&(CurrentBuffer->EtxOffset), EtxOffset);
	}

	GTlsWriteBuffer = NextBuffer;
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

	FWriteBuffer* NextBuffer = Writer_NextBufferInternal(GPoolPageGrowth);

	NextBuffer->Cursor += Size;
	return NextBuffer;
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_InitializeBuffers()
{
	GPoolBase = MemoryReserve(GPoolSize);
	AtomicStoreRelaxed(&GPoolPageCursor, GPoolBase);

	Writer_NextBufferInternal(GPoolInitPageSize);

	static_assert(GPoolPageGrowth >= 0x10000, "Page growth must be >= 64KB");
	static_assert(GPoolInitPageSize >= 0x10000, "Initial page size must be >= 64KB");
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_ShutdownBuffers()
{
	MemoryFree(GPoolBase, GPoolSize);
}



////////////////////////////////////////////////////////////////////////////
template <typename Class>
class alignas(Class) TSafeStatic
{
public:
	Class* operator -> ()				{ return (Class*)Buffer; }
	Class const* operator -> () const	{ return (Class const*)Buffer; }

protected:
	alignas(Class) uint8 Buffer[sizeof(Class)];
};



////////////////////////////////////////////////////////////////////////////
class FHoldBufferImpl
{
public:
	void				Init();
	void				Shutdown();
	void				Write(const void* Data, uint32 Size);
	bool				IsFull() const	{ return bFull; }
	const uint8*		GetData() const { return Base; }
	uint32				GetSize() const { return Used; }

private:
	static const uint32	PageShift = 16;
	static const uint32	PageSize = 1 << PageShift;
	static const uint32	MaxPages = (4 * 1024 * 1024) >> PageShift;
	uint8*				Base;
	int32				Used;
	uint16				MappedPageCount;
	bool				bFull;
};

typedef TSafeStatic<FHoldBufferImpl> FHoldBuffer;

////////////////////////////////////////////////////////////////////////////////
void FHoldBufferImpl::Init()
{
	Base = MemoryReserve(FHoldBufferImpl::PageSize * FHoldBufferImpl::MaxPages);
	Used = 0;
	MappedPageCount = 0;
	bFull = false;
}

////////////////////////////////////////////////////////////////////////////////
void FHoldBufferImpl::Shutdown()
{
	if (Base == nullptr)
	{
		return;
	}

	MemoryFree(Base, FHoldBufferImpl::PageSize * FHoldBufferImpl::MaxPages);
	Base = nullptr;
	MappedPageCount = 0;
	Used = 0;
}

////////////////////////////////////////////////////////////////////////////////
void FHoldBufferImpl::Write(const void* Data, uint32 Size)
{
	int32 NextUsed = Used + Size;

	uint16 HotPageCount = uint16((NextUsed + (FHoldBufferImpl::PageSize - 1)) >> FHoldBufferImpl::PageShift);
	if (HotPageCount > MappedPageCount)
	{
		if (HotPageCount > FHoldBufferImpl::MaxPages)
		{
			bFull = true;
			return;
		}

		void* MapStart = Base + (UPTRINT(MappedPageCount) << FHoldBufferImpl::PageShift);
		uint32 MapSize = (HotPageCount - MappedPageCount) << FHoldBufferImpl::PageShift;
		MemoryMap(MapStart, MapSize);

		MappedPageCount = HotPageCount;
	}

	memcpy(Base + Used, Data, Size);

	Used = NextUsed;
}



////////////////////////////////////////////////////////////////////////////////
enum class EDataState : uint8
{
	Passive = 0,		// Data is being collected in-process
	Partial,			// Passive, but buffers are full so some events are lost
	Sending,			// Events are being sent to an IO handle
};
static FHoldBuffer				GHoldBuffer;		// will init to zero.
static UPTRINT					GDataHandle;		// = 0
static EDataState				GDataState;			// = EDataState::Passive
UPTRINT							GPendingDataHandle;	// = 0
static FWriteBuffer* __restrict GActiveThreadList;	// = nullptr;

////////////////////////////////////////////////////////////////////////////////
static uint32 Writer_SendData(uint32 ThreadId, uint8* __restrict Data, uint32 Size)
{
	auto SendInner = [] (uint8* __restrict SendData, uint32 SendSize)
	{
		if (GDataState == EDataState::Sending)
		{
			// Transmit data to the io handle
			if (GDataHandle)
			{
				if (!IoWrite(GDataHandle, SendData, SendSize))
				{
					IoClose(GDataHandle);
					GDataHandle = 0;
				}
			}
		}
		else
		{
			GHoldBuffer->Write(SendData, SendSize);

			// Did we overflow? Enter partial mode.
			bool bOverflown = GHoldBuffer->IsFull();
			if (bOverflown && GDataState != EDataState::Partial)
			{
				GDataState = EDataState::Partial;
			}
		}
	};

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

		SendInner(Data, Size);
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

	SendInner((uint8*)&Packet, Packet.PacketSize);
	return Packet.PacketSize;
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
	for (;; Private::PlatformYield())
	{
		NewThreadList = AtomicLoadRelaxed(&GNewThreadList);
		if (AtomicCompareExchangeAcquire(&GNewThreadList, (FWriteBuffer*)nullptr, NewThreadList))
		{
			break;
		}
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
				uint8* Committed = AtomicLoadRelaxed(&Buffer->Committed);

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
		<< WorkerThread.BytesReaped(BytesReaped);
		<< WorkerThread.BytesSent(BytesSent);

	UE_TRACE_LOG($Trace, Memory, TraceLogChannel)
		<< Memory.AllocSize(uint32(GPoolPageCursor - GPoolBase));
#endif // TRACE_PRIVATE_PERF

	// Put the retirees we found back into the system again.
	if (RetireList.Head != nullptr)
	{
		for (FWriteBuffer* ListNode = RetireList.Tail;; Private::PlatformYield())
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
static void Writer_UpdateData()
{
	if (GPendingDataHandle)
	{
		// Reject the pending connection if we've already got a connection
		if (GDataHandle)
		{
			IoClose(GPendingDataHandle);
			GPendingDataHandle = 0;
			return;
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

		// Passively collected data
		if (GHoldBuffer->GetSize())
		{
			bOk &= IoWrite(GDataHandle, GHoldBuffer->GetData(), GHoldBuffer->GetSize());
		}

		if (bOk)
		{
			GDataState = EDataState::Sending;
			GHoldBuffer->Shutdown();
		}
		else
		{
			IoClose(GDataHandle);
			GDataHandle = 0;
		}
	}

	Writer_ConsumeEvents();
}



////////////////////////////////////////////////////////////////////////////////
static UPTRINT			GWorkerThread		= 0;
static volatile bool	GWorkerThreadQuit	= false;

////////////////////////////////////////////////////////////////////////////////
static void Writer_WorkerThread()
{
	while (!GWorkerThreadQuit)
	{
		const uint32 SleepMs = 24;
		ThreadSleep(SleepMs);

		Writer_UpdateControl();
		Writer_UpdateData();
	}

	Writer_ConsumeEvents();
}



////////////////////////////////////////////////////////////////////////////////
static void Writer_LogHeader()
{
	UE_TRACE_EVENT_BEGIN($Trace, NewTrace, Important)
		UE_TRACE_EVENT_FIELD(uint16, Endian)
		UE_TRACE_EVENT_FIELD(uint8, Version)
		UE_TRACE_EVENT_FIELD(uint8, PointerSize)
	UE_TRACE_EVENT_END()

	UE_TRACE_LOG($Trace, NewTrace, TraceLogChannel)
		<< NewTrace.Version(2)
		<< NewTrace.Endian(0x524d)
		<< NewTrace.PointerSize(sizeof(void*));
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_InternalInitialize()
{
	if (GInitialized)
	{
		return;
	}
	GInitialized = true;

	Writer_InitializeBuffers();
	Writer_LogHeader();

	GHoldBuffer->Init();

	GWorkerThread = ThreadCreate("TraceWorker", Writer_WorkerThread);

	Writer_InitializeControl();
	Writer_InitializeTiming();
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_Shutdown()
{
	if (!GInitialized)
	{
		return;
	}

	GWorkerThreadQuit = true;
	ThreadJoin(GWorkerThread);
	ThreadDestroy(GWorkerThread);

	Writer_ShutdownControl();

	GHoldBuffer->Shutdown();
	Writer_ShutdownBuffers();

	GInitialized = false;
}

////////////////////////////////////////////////////////////////////////////////
void Writer_Initialize()
{
	using namespace Private;

	if (!GInitialized)
	{
		static struct FInitializer
		{
			FInitializer()
			{
				Writer_InternalInitialize();
			}
			~FInitializer()
			{
				Writer_Shutdown();
			}
		} Initializer;
	}
}



////////////////////////////////////////////////////////////////////////////////
bool Writer_SendTo(const ANSICHAR* Host, uint32 Port)
{
	if (GPendingDataHandle || GDataHandle)
	{
		return false;
	}

	Writer_Initialize();

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

	Writer_Initialize();

	UPTRINT DataHandle = FileOpen(Path);
	if (!DataHandle)
	{
		return false;
	}

	GPendingDataHandle = DataHandle;
	return true;
}



////////////////////////////////////////////////////////////////////////////////
static uint32 volatile GEventUidCounter; // = 0;

////////////////////////////////////////////////////////////////////////////////
void Writer_EventCreate(
	FEventDef* Target,
	const FLiteralName& LoggerName,
	const FLiteralName& EventName,
	const FFieldDesc* FieldDescs,
	uint32 FieldCount,
	uint32 Flags)
{
	Writer_Initialize();

	// Assign a unique ID for this event
	uint32 Uid = AtomicIncrementRelaxed(&GEventUidCounter) + uint32(EKnownEventUids::User);
	if (Uid >= uint32(EKnownEventUids::Max))
	{
		Target->Uid = uint16(EKnownEventUids::Invalid);
		Target->bInitialized = true;
		return;
	}

	// Fill out the target event's properties
 	Target->Uid = uint16(Uid);
	Target->bInitialized = true;
	Target->bImportant = Flags & FEventDef::Flag_Important;

	// Calculate the number of fields and size of name data.
	uint16 NamesSize = LoggerName.Length + EventName.Length;
	for (uint32 i = 0; i < FieldCount; ++i)
	{
		NamesSize += FieldDescs[i].NameSize;
	}

	// Allocate the new event event in the log stream.
	uint16 EventUid = uint16(EKnownEventUids::NewEvent);
	uint16 EventSize = sizeof(FNewEventEvent);
	EventSize += sizeof(FNewEventEvent::Fields[0]) * FieldCount;
	EventSize += NamesSize;

	FLogInstance LogInstance = Writer_BeginLog(EventUid, EventSize, false);
	auto& Event = *(FNewEventEvent*)(LogInstance.Ptr);

	// Write event's main properties.
	Event.EventUid = uint16(Uid);
	Event.LoggerNameSize = LoggerName.Length;
	Event.EventNameSize = EventName.Length;
	Event.Flags = 0;

	if (Flags & FEventDef::Flag_Important)
	{
		Event.Flags |= uint8(EEventFlags::Important);
	}

	if (Flags & FEventDef::Flag_MaybeHasAux)
	{
		Event.Flags |= uint8(EEventFlags::MaybeHasAux);
	}

	if (Flags & FEventDef::Flag_NoSync)
	{
		Event.Flags |= uint8(EEventFlags::NoSync);
	}

	// Write details about event's fields
	Event.FieldCount = uint8(FieldCount);
	for (uint32 i = 0; i < FieldCount; ++i)
	{
		const FFieldDesc& FieldDesc = FieldDescs[i];
		auto& Out = Event.Fields[i];
		Out.Offset = FieldDesc.ValueOffset;
		Out.Size = FieldDesc.ValueSize;
		Out.TypeInfo = FieldDesc.TypeInfo;
		Out.NameSize = FieldDesc.NameSize;
	}

	// Write names
	uint8* Cursor = (uint8*)(Event.Fields + FieldCount);
	auto WriteName = [&Cursor] (const ANSICHAR* Data, uint32 Size)
	{
		memcpy(Cursor, Data, Size);
		Cursor += Size;
	};

	WriteName(LoggerName.Ptr, LoggerName.Length);
	WriteName(EventName.Ptr, EventName.Length);
	for (uint32 i = 0; i < FieldCount; ++i)
	{
		const FFieldDesc& Desc = FieldDescs[i];
		WriteName(Desc.Name, Desc.NameSize);
	}

	Writer_EndLog(LogInstance);
}

} // namespace Private
} // namespace Trace

#endif // UE_TRACE_ENABLED
