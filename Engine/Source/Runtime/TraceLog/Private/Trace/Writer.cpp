// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "Trace/Detail/Atomic.h"
#include "Trace/Detail/Protocol.h"
#include "Trace/Platform.h"
#include "Trace/Trace.h"

#include "Misc/CString.h"
#include "Templates/UnrealTemplate.h"

#if PLATFORM_CPU_X86_FAMILY
	#include <emmintrin.h>
	#define PLATFORM_YIELD()	_mm_pause()
#elif PLATFORM_CPU_ARM_FAMILY
	#define PLATFORM_YIELD()	__builtin_arm_yield();
#else
	#error Unsupported architecture!
#endif

namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
int32 Encode(const void*, int32, void*, int32);



////////////////////////////////////////////////////////////////////////////////
#define TRACE_PRIVATE_PERF 0
#if TRACE_PRIVATE_PERF
UE_TRACE_EVENT_BEGIN($Trace, WorkerThread, Always)
	UE_TRACE_EVENT_FIELD(uint32, Cycles)
	UE_TRACE_EVENT_FIELD(uint32, BytesSent)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN($Trace, Memory, Always)
	UE_TRACE_EVENT_FIELD(uint32, AllocSize)
UE_TRACE_EVENT_END()
#endif // TRACE_PRIVATE_PERF



////////////////////////////////////////////////////////////////////////////////
inline void Writer_Yield()
{
	PLATFORM_YIELD();
}



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

	UE_TRACE_EVENT_BEGIN($Trace, Timing, Always|Important)
		UE_TRACE_EVENT_FIELD(uint64, StartCycle)
		UE_TRACE_EVENT_FIELD(uint64, CycleFrequency)
	UE_TRACE_EVENT_END()

	UE_TRACE_LOG($Trace, Timing)
		<< Timing.StartCycle(GStartCycle)
		<< Timing.CycleFrequency(TimeGetFrequency());
}



////////////////////////////////////////////////////////////////////////////////
thread_local					FWriteTlsContext TlsContext;
uint8							FWriteTlsContext::DefaultBuffer[];	// = {}
UPTRINT volatile				FWriteTlsContext::ThreadIdCounter;	// = 0;
TRACELOG_API uint32 volatile	GLogSerial;							// = 0;

////////////////////////////////////////////////////////////////////////////////
FWriteTlsContext::FWriteTlsContext()
{
	auto* Target = (FWriteBuffer*)DefaultBuffer;

	static bool Once;
	if (!Once)
	{
		Target->Cursor = DefaultBuffer;
		Once = true;
	}

	Buffer = Target;

	// Assign a new id to this thread.
	for (;; Writer_Yield())
	{
		UPTRINT CandidateId = UPTRINT(AtomicLoadRelaxed((void* volatile*)&ThreadIdCounter));
		UPTRINT NextId = CandidateId + 1;
		if (AtomicCompareExchangeRelaxed(&ThreadIdCounter, NextId, CandidateId))
		{
			ThreadId = uint32(CandidateId);
			break;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
FWriteTlsContext::~FWriteTlsContext()
{
	if (HasValidBuffer())
	{
		AtomicStoreRelease<uint8* __restrict>(&(Buffer->Committed), nullptr);
	}
}

////////////////////////////////////////////////////////////////////////////////
bool FWriteTlsContext::HasValidBuffer() const
{
	return (UPTRINT(Buffer) != UPTRINT(DefaultBuffer));
}

////////////////////////////////////////////////////////////////////////////////
inline void FWriteTlsContext::SetBuffer(FWriteBuffer* InBuffer)
{
	Buffer = InBuffer;
}

////////////////////////////////////////////////////////////////////////////////
inline uint32 FWriteTlsContext::GetThreadId() const
{
	return ThreadId;
}



////////////////////////////////////////////////////////////////////////////////
#define T_ALIGN alignas(PLATFORM_CACHE_LINE_SIZE)
static const uint32						GPoolSize			= 384 << 20; // 384MB ought to be enough
static const uint32						GPoolBlockSize		= 4 << 10;
static const uint32						GPoolPageGrowth		= GPoolBlockSize << 5;
static const uint32						GPoolInitPageSize	= GPoolBlockSize << 5;
static uint8*							GPoolBase;			// = nullptr;
T_ALIGN static uint8* volatile			GPoolPageCursor;	// = nullptr;
T_ALIGN static void* volatile			GPoolFreeList;		// = nullptr;
T_ALIGN static FWriteBuffer* volatile	GNextBufferList;	// = nullptr;
#undef T_ALIGN

////////////////////////////////////////////////////////////////////////////////
uint32 Writer_GetMaxEventSize()
{
	// The bais is to allow for some overhead. Its value was chosen arbitrarily.
	return GPoolBlockSize - 512;
}

////////////////////////////////////////////////////////////////////////////////
#if !IS_MONOLITHIC
TRACELOG_API FWriteBuffer* Writer_GetBuffer()
{
	// Thread locals and DLLs don't mix so for modular builds we are forced to
	// export this function to access thread-local variables.
	return TlsContext.GetBuffer();
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
		void* Owned = AtomicLoadRelaxed(&GPoolFreeList);
		if (Owned != nullptr)
		{
			if (!AtomicCompareExchangeRelaxed(&GPoolFreeList, *(void**)Owned, Owned))
			{
				Writer_Yield();
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
			Writer_Yield();
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
			Buffer->Next = (FWriteBuffer*)(Block + GPoolBlockSize);
			Block += GPoolBlockSize;
		}

		// And insert the block list into the freelist
		uint8* LastBlock = Block;
		for (void** ListNode = (void**)LastBlock;; Writer_Yield())
		{
			*ListNode = AtomicLoadRelaxed(&GPoolFreeList);
			if (AtomicCompareExchangeRelease(&GPoolFreeList, (void*)FirstBlock, *ListNode))
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

	// Add this next buffer to the active list.
	for (;; Writer_Yield())
	{
		NextBuffer->Next = AtomicLoadRelaxed(&GNextBufferList);
		if (AtomicCompareExchangeRelease(&GNextBufferList, NextBuffer, NextBuffer->Next))
		{
			break;
		}
	}

	TlsContext.SetBuffer(NextBuffer);
	return NextBuffer;
}

////////////////////////////////////////////////////////////////////////////////
TRACELOG_API FWriteBuffer* Writer_NextBuffer(uint16 Size)
{
	if (Size >= GPoolBlockSize - sizeof(FWriteBuffer))
	{
		/* Someone is trying to write an event that is too large */
		return nullptr;
	}

	FWriteBuffer* Current = TlsContext.GetBuffer();

	// Retire current buffer unless its the initial boot one.
	if (TlsContext.HasValidBuffer())
	{
		Current->Cursor -= Size;
		AtomicStoreRelease<uint8* __restrict>(&Current->Committed, nullptr);
	}

	FWriteBuffer* NextBuffer = Writer_NextBufferInternal(GPoolPageGrowth);
	NextBuffer->ThreadId = TlsContext.GetThreadId();

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
static FHoldBuffer		GHoldBuffer;		// will init to zero.
static UPTRINT			GDataHandle;		// = 0
static EDataState		GDataState;			// = EDataState::Passive
UPTRINT					GPendingDataHandle;	// = 0
static FWriteBuffer*	GActiveBufferList;	// = nullptr;

////////////////////////////////////////////////////////////////////////////////
static void Writer_ConsumeEvents()
{
	uint64 StartTsc = TimeGetTimestamp();
	uint32 BytesSent = 0;

	// Claim ownership of the latest chain of sent events.
	FWriteBuffer* __restrict NextBufferList;
	for (;; Writer_Yield())
	{
		NextBufferList = AtomicLoadRelaxed(&GNextBufferList);
		if (AtomicCompareExchangeAcquire(&GNextBufferList, (FWriteBuffer*)nullptr, NextBufferList))
		{
			break;
		}
	}

	struct FRetireList
	{
		FWriteBuffer* __restrict Head = nullptr;
		FWriteBuffer* __restrict Tail = nullptr;

		void Insert(FWriteBuffer* __restrict Buffer)
		{
			Buffer->Next = Head;
			Head = Buffer;
			Tail = (Tail != nullptr) ? Tail : Head;
		}
	};

	auto SendInner = [&] (uint8* __restrict Data, uint32 Size)
	{
		if (GDataState == EDataState::Sending)
		{
			// Transmit data to the io handle
			if (GDataHandle)
			{
				if (!IoWrite(GDataHandle, Data, Size))
				{
					IoClose(GDataHandle);
					GDataHandle = 0;
				}
			}
		}
		else
		{
			GHoldBuffer->Write(Data, Size);

			// Did we overflow? Enter partial mode.
			bool bOverflown = GHoldBuffer->IsFull();
			if (bOverflown && GDataState != EDataState::Partial)
			{
				GDataState = EDataState::Partial;
			}
		}
	};

	auto Send = [&] (FWriteBuffer* Buffer, uint8* __restrict Data, uint32 Size)
	{
		BytesSent += Size;

		struct FPacketBase
		{
			uint16 PacketSize;
			uint16 ThreadId;
		};

		// Smaller buffers usually aren't redundant enough to benefit from being
		// compressed. They often end up being larger.
		if (Size > 384)
		{
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
			Packet.ThreadId = 0x8000 | uint16(Buffer->ThreadId & 0x7fff);
			Packet.DecodedSize = uint16(Size);
			Packet.PacketSize = Encode(Data, Packet.DecodedSize, Packet.Data, sizeof(Packet.Data));
			Packet.PacketSize += sizeof(FPacketEncoded);

			SendInner((uint8*)&Packet, Packet.PacketSize);
		}
		else
		{
			static_assert(sizeof(FPacketBase) == sizeof(uint32), "");
			Data -= sizeof(FPacketBase);
			Size += sizeof(FPacketBase);
			auto* Packet = (FPacketBase*)Data;
			Packet->ThreadId = uint16(Buffer->ThreadId & 0x7fff);
			Packet->PacketSize = uint16(Size);

			SendInner(Data, Size);
		}
	};

	FRetireList RetireList;

	// Next buffer list is newest first. Retire full ones and build a new list
	// of buffers that are active (which gets reverse so oldest is first)
	FWriteBuffer* __restrict NextActiveList = nullptr;
	FWriteBuffer* __restrict NextRetireList = nullptr;
	for (FWriteBuffer *__restrict Buffer = NextBufferList, *__restrict NextBuffer;
		Buffer != nullptr;
		Buffer = NextBuffer)
	{
		NextBuffer = Buffer->Next;

		if (AtomicLoadAcquire(&Buffer->Committed) != nullptr)
		{
			Buffer->Next = NextActiveList;
			NextActiveList = Buffer;
		}
		else
		{
			Buffer->Next = NextRetireList;
			NextRetireList = Buffer;
		}
	}

	// Send as much of the active list as we can. Buffers that are full are removed
	// from the list. Note that the list's oldest-first order is maintained
	FWriteBuffer* __restrict ActiveListHead = nullptr;
	FWriteBuffer* __restrict ActiveListTail = nullptr;
	for (FWriteBuffer *__restrict Buffer = GActiveBufferList, *__restrict NextBuffer;
		Buffer != nullptr;
		Buffer = NextBuffer)
	{
		NextBuffer = Buffer->Next;

		uint8* __restrict Committed = AtomicLoadAcquire(&Buffer->Committed);
		if (Committed == nullptr)
		{
			Committed = Buffer->Cursor;
			RetireList.Insert(Buffer);
		}
		else
		{
			if (ActiveListTail != nullptr)
			{
				ActiveListTail->Next = Buffer;
			}
			else
			{
				ActiveListHead = Buffer;
			}

			ActiveListTail = Buffer;
			Buffer->Next = nullptr;
		}

		if (uint32 SizeToReap = uint32(Committed - Buffer->Reaped))
		{
			Send(Buffer, Buffer->Reaped, SizeToReap);
			Buffer->Reaped = Committed;
		}
	}

	// Retire buffers from the next list
	for (FWriteBuffer *__restrict Buffer = NextRetireList, *__restrict NextBuffer;
		Buffer != nullptr;
		Buffer = NextBuffer)
	{
		NextBuffer = Buffer->Next;

		RetireList.Insert(Buffer);

		if (uint32 SizeToReap = uint32(Buffer->Cursor - Buffer->Reaped))
		{
			Send(Buffer, Buffer->Reaped, SizeToReap);
		}
	}

	// Append the new active buffers that have been discovered to the active list
	if (ActiveListTail != nullptr)
	{
		GActiveBufferList = ActiveListHead;
		ActiveListTail->Next = NextActiveList;
	}
	else
	{
		GActiveBufferList = NextActiveList;
	}

#if TRACE_PRIVATE_PERF
	UE_TRACE_LOG($Trace, WorkerThread)
		<< WorkerThread.Cycles(uint32(TimeGetTimestamp() - StartTsc))
		<< WorkerThread.BytesSent(BytesSent);

	UE_TRACE_LOG($Trace, Memory)
		<< Memory.AllocSize(uint32(GPoolPageCursor - GPoolBase));
#endif // TRACE_PRIVATE_PERF

	// Put the retirees we found back into the system again.
	if (RetireList.Head != nullptr)
	{
		for (void** ListNode = (void**)RetireList.Tail;; Writer_Yield())
		{
			*ListNode = AtomicLoadRelaxed(&GPoolFreeList);
			if (AtomicCompareExchangeRelease(&GPoolFreeList, (void*)RetireList.Head, *ListNode))
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
enum class EControlState : uint8
{
	Closed = 0,
	Listening,
	Accepted,
	Failed,
};

struct FControlCommands
{
	enum { Max = 3 };
	struct
	{
		uint32	Hash;
		void*	Param;
		void	(*Thunk)(void*, uint32, ANSICHAR const* const*);
	}			Commands[Max];
	uint8		Count;
};

////////////////////////////////////////////////////////////////////////////////
bool	Writer_SendTo(const ANSICHAR*);
bool	Writer_WriteTo(const ANSICHAR*);
uint32	Writer_EventToggle(const ANSICHAR*, bool);

////////////////////////////////////////////////////////////////////////////////
static FControlCommands	GControlCommands;
static UPTRINT			GControlListen		= 0;
static UPTRINT			GControlSocket		= 0;
static EControlState	GControlState;		// = EControlState::Closed;

////////////////////////////////////////////////////////////////////////////////
static uint32 Writer_ControlHash(const ANSICHAR* Word)
{
	uint32 Hash = 5381;
	for (; *Word; (Hash = (Hash * 33) ^ *Word), ++Word);
	return Hash;
}

////////////////////////////////////////////////////////////////////////////////
static bool Writer_ControlAddCommand(
	const ANSICHAR* Name,
	void* Param,
	void (*Thunk)(void*, uint32, ANSICHAR const* const*))
{
	if (GControlCommands.Count >= FControlCommands::Max)
	{
		return false;
	}

	uint32 Index = GControlCommands.Count++;
	GControlCommands.Commands[Index] = { Writer_ControlHash(Name), Param, Thunk };
	return true;
}

////////////////////////////////////////////////////////////////////////////////
static bool Writer_ControlDispatch(uint32 ArgC, ANSICHAR const* const* ArgV)
{
	if (ArgC == 0)
	{
		return false;
	}

	uint32 Hash = Writer_ControlHash(ArgV[0]);
	--ArgC;
	++ArgV;

	for (int i = 0, n = GControlCommands.Count; i < n; ++i)
	{
		const auto& Command = GControlCommands.Commands[i];
		if (Command.Hash == Hash)
		{
			Command.Thunk(Command.Param, ArgC, ArgV);
			return true;
		}
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////
static bool Writer_ControlListen()
{
	GControlListen = TcpSocketListen(1985);
	if (!GControlListen)
	{
		GControlState = EControlState::Failed;
		return false;
	}

	GControlState = EControlState::Listening;
	return true;
}

////////////////////////////////////////////////////////////////////////////////
static bool Writer_ControlAccept()
{
	UPTRINT Socket;
	int Return = TcpSocketAccept(GControlListen, Socket);
	if (Return <= 0)
	{
		if (Return == -1)
		{
			IoClose(GControlListen);
			GControlListen = 0;
			GControlState = EControlState::Failed;
		}
		return false;
	}

	GControlState = EControlState::Accepted;
	GControlSocket = Socket;
	return true;
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_ControlRecv()
{
	// We'll assume that commands are smaller than the canonical MTU so this
	// doesn't need to be implemented in a reentrant manner (maybe).

	ANSICHAR Buffer[512];
	ANSICHAR* __restrict Head = Buffer;
	while (TcpSocketHasData(GControlSocket))
	{
		int32 ReadSize = int32(UPTRINT(Buffer + sizeof(Buffer) - Head));
		int32 Recvd = IoRead(GControlSocket, Head, ReadSize);
		if (Recvd <= 0)
		{
			IoClose(GControlSocket);
			GControlSocket = 0;
			GControlState = EControlState::Listening;
			break;
		}

		Head += Recvd;

		enum EParseState
		{
			CrLfSkip,
			WhitespaceSkip,
			Word,
		} ParseState = EParseState::CrLfSkip;

		uint32 ArgC = 0;
		const ANSICHAR* ArgV[16];

		const ANSICHAR* __restrict Spent = Buffer;
		for (ANSICHAR* __restrict Cursor = Buffer; Cursor < Head; ++Cursor)
		{
			switch (ParseState)
			{
			case EParseState::CrLfSkip:
				if (*Cursor == '\n' || *Cursor == '\r')
				{
					continue;
				}
				ParseState = EParseState::WhitespaceSkip;
				/* [[fallthrough]] */

			case EParseState::WhitespaceSkip:
				if (*Cursor == ' ' || *Cursor == '\0')
				{
					continue;
				}

				if (ArgC < UE_ARRAY_COUNT(ArgV))
				{
					ArgV[ArgC] = Cursor;
					++ArgC;
				}

				ParseState = EParseState::Word;
				/* [[fallthrough]] */

			case EParseState::Word:
				if (*Cursor == ' ' || *Cursor == '\0')
				{
					*Cursor = '\0';
					ParseState = EParseState::WhitespaceSkip;
					continue;
				}

				if (*Cursor == '\r' || *Cursor == '\n')
				{
					*Cursor = '\0';

					Writer_ControlDispatch(ArgC, ArgV);

					ArgC = 0;
					Spent = Cursor + 1;
					ParseState = EParseState::CrLfSkip;
					continue;
				}

				break;
			}
		}

		int32 UnspentSize = int32(UPTRINT(Head - Spent));
		if (UnspentSize)
		{
			memmove(Buffer, Spent, UnspentSize);
		}
		Head = Buffer + UnspentSize;
	}
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_UpdateControl()
{
	switch (GControlState)
	{
	case EControlState::Closed:
		if (!Writer_ControlListen())
		{
			break;
		}
		/* [[fallthrough]] */

	case EControlState::Listening:
		if (!Writer_ControlAccept())
		{
			break;
		}
		/* [[fallthrough]] */

	case EControlState::Accepted:
		Writer_ControlRecv();
		break;
	}
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_InitializeControl()
{
#if PLATFORM_SWITCH
	GControlState = EControlState::Failed;
	return;
#endif

	Writer_ControlAddCommand("SendTo", nullptr,
		[] (void*, uint32 ArgC, ANSICHAR const* const* ArgV)
		{
			if (ArgC > 0)
			{
				Writer_SendTo(ArgV[0]);
			}
		}
	);

	Writer_ControlAddCommand("WriteTo", nullptr,
		[] (void*, uint32 ArgC, ANSICHAR const* const* ArgV)
		{
			if (ArgC > 0)
			{
				Writer_WriteTo(ArgV[0]);
			}
		}
	);

	Writer_ControlAddCommand("ToggleEvent", nullptr,
		[] (void*, uint32 ArgC, ANSICHAR const* const* ArgV)
		{
			if (ArgC < 1)
			{
				return;
			}
			const ANSICHAR* Wildcard = ArgV[0];
			const ANSICHAR* State = (ArgC > 1) ? ArgV[1] : "";
			Writer_EventToggle(Wildcard, State[0] != '0');
		}
	);
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_ShutdownControl()
{
	if (GControlListen)
	{
		IoClose(GControlListen);
		GControlListen = 0;
	}
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
static bool GInitialized = false;

////////////////////////////////////////////////////////////////////////////////
static void Writer_LogHeader()
{
	UE_TRACE_EVENT_BEGIN($Trace, NewTrace, Always|Important)
		UE_TRACE_EVENT_FIELD(uint16, Endian)
		UE_TRACE_EVENT_FIELD(uint8, Version)
		UE_TRACE_EVENT_FIELD(uint8, PointerSize)
	UE_TRACE_EVENT_END()

	UE_TRACE_LOG($Trace, NewTrace)
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
bool Writer_SendTo(const ANSICHAR* Host)
{
	Writer_Initialize();

	UPTRINT DataHandle = TcpSocketConnect(Host, 1980);
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
static UPTRINT volatile		GEventUidCounter;	// = 0;
static FEventDef* volatile	GHeadEvent;			// = nullptr;

////////////////////////////////////////////////////////////////////////////////
enum class EKnownEventUids : uint16
{
	NewEvent		= FNewEventEvent::Uid,
	User,
	Max				= (1 << 14) - 1, // ...leaves two MSB bits for other uses.
	UidMask			= Max,
	Invalid			= Max,
	Flag_Unused		= 1 << 14,
	Flag_Important	= 1 << 15,
};

////////////////////////////////////////////////////////////////////////////////
template <typename ElementType>
static uint32 Writer_EventGetHash(const ElementType* Input, int32 Length=-1)
{
	uint32 Result = 0x811c9dc5;
	for (; *Input && Length; ++Input, --Length)
	{
		Result ^= *Input;
		Result *= 0x01000193;
	}
	return Result;
}

////////////////////////////////////////////////////////////////////////////////
static uint32 Writer_EventGetHash(uint32 LoggerHash, uint32 NameHash)
{
	uint32 Parts[3] = { LoggerHash, NameHash, 0 };
	return Writer_EventGetHash(Parts);
}

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
	uint32 Uid;
	for (;; Writer_Yield())
	{
		UPTRINT CurrentUid = UPTRINT(AtomicLoadRelaxed((void* volatile*)&GEventUidCounter));
		UPTRINT NextUid = CurrentUid + 1;
		if (AtomicCompareExchangeRelaxed(&GEventUidCounter, NextUid, CurrentUid))
		{
			Uid = uint32(CurrentUid) + uint32(EKnownEventUids::User);
			break;
		}
	}

	if (Uid >= uint32(EKnownEventUids::Max))
	{
		Target->Uid = uint16(EKnownEventUids::Invalid);
		Target->Enabled.bOptedIn = false;
		Target->Enabled.Internal = 0;
		Target->bInitialized = true;
		return;
	}

	if (Flags & FEventDef::Flag_Important)
	{
		Uid |= uint16(EKnownEventUids::Flag_Important);
	}

	uint32 LoggerHash = Writer_EventGetHash(LoggerName.Ptr);
	uint32 NameHash = Writer_EventGetHash(EventName.Ptr);

	// Fill out the target event's properties
 	Target->Uid = uint16(Uid);
	Target->LoggerHash = LoggerHash;
	Target->Hash = Writer_EventGetHash(LoggerHash, NameHash);
	Target->Enabled.Internal = !!(Flags & FEventDef::Flag_Always);
	Target->Enabled.bOptedIn = false;
	Target->bInitialized = true;

	// Calculate the number of fields and size of name data.
	int NamesSize = LoggerName.Length + EventName.Length;
	for (uint32 i = 0; i < FieldCount; ++i)
	{
		NamesSize += FieldDescs[i].NameSize;
	}

	// Allocate the new event event in the log stream.
	uint16 EventUid = uint16(EKnownEventUids::NewEvent)|uint16(EKnownEventUids::Flag_Important);
	uint16 EventSize = sizeof(FNewEventEvent);
	EventSize += sizeof(FNewEventEvent::Fields[0]) * FieldCount;
	EventSize += NamesSize;

	FLogInstance LogInstance = Writer_BeginLog(EventUid, EventSize);
	auto& Event = *(FNewEventEvent*)(LogInstance.Ptr);

	// Write event's main properties.
	Event.EventUid = uint16(Uid) & uint16(EKnownEventUids::UidMask);
	Event.LoggerNameSize = LoggerName.Length;
	Event.EventNameSize = EventName.Length;

	// Write details about event's fields
	Event.FieldCount = FieldCount;
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

	// Add this new event into the list so we can look them up later.
	for (;; Writer_Yield())
	{
		FEventDef* HeadEvent = AtomicLoadRelaxed(&GHeadEvent);
		Target->Handle = HeadEvent;
		if (AtomicCompareExchangeRelease(&GHeadEvent, Target, HeadEvent))
		{
			break;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
uint32 Writer_EventToggle(const ANSICHAR* Wildcard, bool bState)
{
	Writer_Initialize();

	uint32 ToggleCount = 0;

	const ANSICHAR* Dot = FCStringAnsi::Strchr(Wildcard, '.');
	if (Dot == nullptr)
	{
		uint32 LoggerHash = Writer_EventGetHash(Wildcard);

		FEventDef* Event = AtomicLoadAcquire(&GHeadEvent);
		for (; Event != nullptr; Event = (FEventDef*)(Event->Handle))
		{
			if (Event->LoggerHash == LoggerHash)
			{
				Event->Enabled.bOptedIn = bState;
				++ToggleCount;
			}
		}

		return ToggleCount;
	}

	uint32 LoggerHash = Writer_EventGetHash(Wildcard, int(Dot - Wildcard));
	uint32 NameHash = Writer_EventGetHash(Dot + 1);
	uint32 EventHash = Writer_EventGetHash(LoggerHash, NameHash);

	FEventDef* Event = (FEventDef*)AtomicLoadAcquire(&Private::GHeadEvent);
	for (; Event != nullptr; Event = (FEventDef*)(Event->Handle))
	{
		if (Event->Hash == EventHash)
		{
			Event->Enabled.bOptedIn = bState;
			++ToggleCount;
		}
	}

	return ToggleCount;
}

} // namespace Private
} // namespace Trace

#endif // UE_TRACE_ENABLED
