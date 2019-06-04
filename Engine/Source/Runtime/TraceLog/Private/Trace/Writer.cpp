// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Trace/Private/Trace.h"

#if UE_TRACE_ENABLED

#include "Trace/Trace.h"
#include "Misc/CString.h"

#include <emmintrin.h>

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
uint8*	MemoryReserve(SIZE_T);
void	MemoryFree(void*, SIZE_T);
void	MemoryMap(void*, SIZE_T);
UPTRINT	TcpSocketConnect(const ANSICHAR*, uint16);
UPTRINT	TcpSocketListen(uint16);
UPTRINT	TcpSocketAccept(UPTRINT);
void	TcpSocketClose(UPTRINT);
bool	TcpSocketSelect(UPTRINT);
int32	TcpSocketRecv(UPTRINT, void*, uint32);
bool	TcpSocketSend(UPTRINT, const void*, uint32);
UPTRINT	ThreadCreate(const ANSICHAR*, void (*)());
uint32	ThreadGetCurrentId();
void	ThreadSleep(uint32 Milliseconds);
void	ThreadJoin(UPTRINT);
void	ThreadDestroy(UPTRINT);
uint64	TimeGetFrequency();
uint64	TimeGetTimestamp();



namespace Private
{

////////////////////////////////////////////////////////////////////////////////
inline void Writer_Yield()
{
#if PLATFORM_CPU_X86_FAMILY
	_mm_pause();
#else
#	error Unsupported platform!
#endif
}



////////////////////////////////////////////////////////////////////////////////
UE_TRACE_EVENT_BEGIN($Trace, PerfWorker)
	UE_TRACE_EVENT_FIELD(uint64, Start)
	UE_TRACE_EVENT_FIELD(uint32, Acquire)
	UE_TRACE_EVENT_FIELD(uint32, Send)
	UE_TRACE_EVENT_FIELD(uint32, Done)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN($Trace, PerfNextBuffer)
	UE_TRACE_EVENT_FIELD(uint64, Start)
	UE_TRACE_EVENT_FIELD(uint32, Acquire)
	UE_TRACE_EVENT_FIELD(uint32, Done)
	UE_TRACE_EVENT_FIELD(uint16, ThreadId)
UE_TRACE_EVENT_END()

void Writer_InitializeInstrumentation()
{
	// This is usually taken care of automatically by the UE_TRACE_* macros but
	// as we're logging these events from within trace where there might not be
	// any buffer space free, we'll explicitly initialise them here where we
	// can guarantee we have available buffer space.
	F$TracePerfNextBufferFields::Initialize();
	F$TracePerfWorkerFields::Initialize();
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
UE_TRACE_API TTraceAtomic<FBuffer*>	GActiveBuffer;
static FBuffer*						GTailBuffer;
static FBuffer*						GHeadBuffer;
static void*						GAllocBase;
static uint32						GAllocSize;
static uint32						GTailPreSent;

////////////////////////////////////////////////////////////////////////////////
static void Writer_InitializeBuffers()
{
	const uint32 BufferCount = 4;

	GAllocSize = BufferSize * (BufferCount + 1);
	GAllocBase = MemoryReserve(GAllocSize);

	UPTRINT BufferBase = UPTRINT(GAllocBase);
	BufferBase += BufferSizeMask;
	BufferBase &= ~UPTRINT(BufferSizeMask);
	MemoryMap((void*)BufferBase, BufferSize * BufferCount);

	FBuffer* Buffers[BufferCount];
	for (int i = 0; i < BufferCount; ++i)
	{
		void* Block = (void*)(BufferBase + (BufferSize * i));
		Buffers[i] = new (Block) FBuffer();
	}

	for (int i = 1; i < BufferCount; ++i)
	{
		Buffers[i - 1]->Next = Buffers[i];
	}

	GTailBuffer = Buffers[0];
	GHeadBuffer = Buffers[BufferCount - 1];
	GTailPreSent = 0;

	GActiveBuffer.store(Buffers[0], std::memory_order_release);
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_ShutdownBuffers()
{
	MemoryFree(GAllocBase, GAllocSize);
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_RetireBuffer(void (*DataSink)(const uint8*, uint32))
{
	uint64 StartTsc = Writer_GetTimestamp();

	uint32 TailUsed;
	for (; ; Writer_Yield())
	{
		TailUsed = GTailBuffer->Used.load(std::memory_order_acquire);
		if (TailUsed < BufferRefBit)
		{
			break;
		}
	}
	uint64 AcquireTailTsc = Writer_GetTimestamp();

	if (uint32 SendSize = GTailBuffer->Final - sizeof(FBuffer))
	{
		SendSize -= GTailPreSent;
		DataSink(GTailBuffer->Data + GTailPreSent, SendSize);
	}
	uint64 SendTsc = Writer_GetTimestamp();

	FBuffer* Next = GTailBuffer->Next.load(std::memory_order_relaxed);
	new (GTailBuffer) FBuffer();

	GHeadBuffer->Next.store(GTailBuffer, std::memory_order_release);
	GHeadBuffer = GTailBuffer;
	GTailBuffer = Next;
	GTailPreSent = 0;
	uint64 DoneTsc = Writer_GetTimestamp();

	if (UE_TRACE_EVENT_IS_ENABLED($Trace, PerfWorker))
	{
		uint8 PerfEventBuffer[TRACE_PRIVATE_EVENT_SIZE($Trace, PerfWorker) + FEvent::HeaderSize];
		UE_TRACE_LOG($Trace, PerfWorker, PerfEventBuffer)
			<< PerfWorker.Start(StartTsc)
			<< PerfWorker.Acquire(uint32(AcquireTailTsc - StartTsc))
			<< PerfWorker.Send(uint32(SendTsc - StartTsc))
			<< PerfWorker.Done(uint32(DoneTsc - StartTsc));

		DataSink(PerfEventBuffer, sizeof(PerfEventBuffer));
	}
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_UpdateBuffers(void (*DataSink)(const uint8*, uint32))
{
	for (int i = 0; i < 3; ++i)
	{
		FBuffer* Buffer = GActiveBuffer.load(std::memory_order_acquire);
		if (GTailBuffer != Buffer)
		{
			Writer_RetireBuffer(DataSink);
			continue;
		}

		uint32 Used = Buffer->Used.load(std::memory_order_relaxed);
		if (Used >= BufferSize) /* is the buffer in use somewhere? */
		{
			continue;
		}

		if (uint32 Sendable = Used - sizeof(FBuffer) - GTailPreSent)
		{
			DataSink(Buffer->Data + GTailPreSent, Sendable);
			GTailPreSent += Sendable;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
void Writer_Flush()
{
	using namespace Private;

	FBuffer* Buffer = GActiveBuffer.load(std::memory_order_acquire);
	uint32 PrevUsed = Buffer->Used.fetch_add(BufferSize, std::memory_order_relaxed);

	if (!(PrevUsed & BufferSize))
	{
		FBuffer* NextBuffer;
		for (; ; Writer_Yield())
		{
			NextBuffer = Buffer->Next.load(std::memory_order_relaxed);
			if (NextBuffer != nullptr)
			{
				break;
			}
		}

		Buffer->Final = PrevUsed & BufferSizeMask;
		GActiveBuffer.store(NextBuffer, std::memory_order_release);
	}
}

////////////////////////////////////////////////////////////////////////////////
UE_TRACE_API void* Writer_NextBuffer(Private::FBuffer* Buffer, uint32 PrevUsed, uint32 SizeAndRef)
{
	using namespace Private;

	uint32 PerfEventSize = 0;
	if (UE_TRACE_EVENT_IS_ENABLED($Trace, PerfNextBuffer))
	{
		PerfEventSize = TRACE_PRIVATE_EVENT_SIZE($Trace, PerfNextBuffer) + FEvent::HeaderSize;
	}
	SizeAndRef += PerfEventSize;

	uint64 StartTsc = Writer_GetTimestamp();

	FBuffer* NextBuffer;
	while (true)
	{
		if (!(PrevUsed & BufferSize))
		{
			Buffer->Final = PrevUsed & BufferSizeMask;
		}

		// Get the next candidate buffer to allocate from.
		for (; ; Writer_Yield())
		{
			NextBuffer = Buffer->Next.load(std::memory_order_relaxed);
			if (NextBuffer != nullptr)
			{
				break;
			}
		}

		Buffer->Used.fetch_sub(BufferRefBit, std::memory_order_release);

		// Try and allocate some space in the next buffer.
		PrevUsed = NextBuffer->Used.fetch_add(SizeAndRef, std::memory_order_relaxed);
		uint32 Used = PrevUsed + SizeAndRef;
		if (UNLIKELY(!(Used & BufferSize)))
		{
			break;
		}

		// Next buffer's full. Try again.
		Buffer = NextBuffer;
	}

	uint64 AcquireTsc = Writer_GetTimestamp();

	if (!(PrevUsed & BufferSize))
	{
		GActiveBuffer.compare_exchange_weak(Buffer, NextBuffer, std::memory_order_release);
	}

	PrevUsed &= BufferSizeMask;
	uint8* Out = (uint8*)(UPTRINT(NextBuffer) + PrevUsed);

	if (PerfEventSize)
	{
		uint64 DoneTsc = Writer_GetTimestamp();
		UE_TRACE_LOG($Trace, PerfNextBuffer, Out)
			<< PerfNextBuffer.Start(StartTsc)
			<< PerfNextBuffer.Acquire(uint32(AcquireTsc - StartTsc))
			<< PerfNextBuffer.Done(uint32(DoneTsc - StartTsc))
			<< PerfNextBuffer.ThreadId(uint16(ThreadGetCurrentId()));
	}

	return Out + PerfEventSize;
}



////////////////////////////////////////////////////////////////////////////////
class FHoldBuffer
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

////////////////////////////////////////////////////////////////////////////////
void FHoldBuffer::Init()
{
	Base = MemoryReserve(FHoldBuffer::PageSize * FHoldBuffer::MaxPages);
	Used = 0;
	MappedPageCount = 0;
	bFull = false;
}

////////////////////////////////////////////////////////////////////////////////
void FHoldBuffer::Shutdown()
{
	if (Base == nullptr)
	{
		return;
	}

	MemoryFree(Base, FHoldBuffer::PageSize * FHoldBuffer::MaxPages);
	Base = nullptr;
	MappedPageCount = 0;
	Used = 0;
}

////////////////////////////////////////////////////////////////////////////////
void FHoldBuffer::Write(const void* Data, uint32 Size)
{
	int32 NextUsed = Used + Size;

	uint16 HotPageCount = uint16((NextUsed + (FHoldBuffer::PageSize - 1)) >> FHoldBuffer::PageShift);
	if (HotPageCount > MappedPageCount)
	{
		if (HotPageCount > FHoldBuffer::MaxPages)
		{
			bFull = true;
			return;
		}

		void* MapStart = Base + (UPTRINT(MappedPageCount) << FHoldBuffer::PageShift);
		uint32 MapSize = (HotPageCount - MappedPageCount) << FHoldBuffer::PageShift;
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
static FHoldBuffer		GHoldBuffer;
static UPTRINT			GDataHandle			= 0;
static EDataState		GDataState;			// = EDataState::Passive;
UPTRINT					GPendingDataHandle	= 0;

////////////////////////////////////////////////////////////////////////////////
static void Writer_UpdateData()
{
	if (GPendingDataHandle)
	{
		if (GDataHandle)
		{
			TcpSocketClose(GPendingDataHandle);
			GPendingDataHandle = 0;
		}
		else
		{
			GDataHandle = GPendingDataHandle;
			GPendingDataHandle = 0;

			// Handshake.
			const uint32 Magic = 'TRCE';
			bool bOk = TcpSocketSend(GDataHandle, &Magic, sizeof(Magic));

			// Stream header
			const struct {
				uint8 Format;
				uint8 Parameter;
			} TransportHeader = { 1 };
			bOk &= TcpSocketSend(GDataHandle, &TransportHeader, sizeof(TransportHeader));

			// Passively collected data
			bOk &= TcpSocketSend(GDataHandle, GHoldBuffer.GetData(), GHoldBuffer.GetSize());

			if (bOk)
			{
				GDataState = EDataState::Sending;
				GHoldBuffer.Shutdown();
			}
			else
			{
				TcpSocketClose(GDataHandle);
				GDataHandle = 0;
			}
		}
	}

	// Passive mode?
	if (GDataState == EDataState::Sending)
	{
		// Transmit data to the io handle
		if (GDataHandle)
		{
			Writer_UpdateBuffers([] (const uint8* Data, uint32 Size)
			{
				if (GDataHandle && !TcpSocketSend(GDataHandle, Data, Size))
				{
					TcpSocketClose(GDataHandle);
					GDataHandle = 0;
				}
			});
		}
		else
		{
			Writer_UpdateBuffers([] (const uint8*, uint32) {});
		}
	}
	else
	{
		// Send data to hold/ring
		Writer_UpdateBuffers([] (const uint8* Data, uint32 Size)
		{
			GHoldBuffer.Write(Data, Size);
		});

		// Did we overflow? Enter partial mode.
		bool bOverflown = GHoldBuffer.IsFull();
		if (bOverflown && GDataState != EDataState::Partial)
		{
			GDataState = EDataState::Partial;
		}
	}
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
bool	Writer_Connect(const ANSICHAR*);
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
	if (!TcpSocketSelect(GControlListen))
	{
		return false;
	}

	UPTRINT Socket = TcpSocketAccept(GControlListen);
	if (!Socket)
	{
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
	while (TcpSocketSelect(GControlSocket))
	{
		int32 ReadSize = int32(UPTRINT(Buffer + sizeof(Buffer) - Head));
		int32 Recvd = TcpSocketRecv(GControlSocket, Head, ReadSize);
		if (Recvd <= 0)
		{
			TcpSocketClose(GControlSocket);
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

				if (ArgC < ARRAY_COUNT(ArgV))
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
	Writer_ControlAddCommand("Connect", nullptr,
		[] (void*, uint32 ArgC, ANSICHAR const* const* ArgV)
		{
			if (ArgC > 0)
			{
				Writer_Connect(ArgV[0]);
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
		TcpSocketClose(GControlListen);
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
		const uint32 SleepMs = 100;
		ThreadSleep(SleepMs);

		Writer_UpdateControl();
		Writer_UpdateData();
	}
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
		<< NewTrace.Version(1)
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
	GHoldBuffer.Init();

	GWorkerThread = ThreadCreate("TraceWorker", Writer_WorkerThread);

	Writer_LogHeader();

	Writer_InitializeControl();
	Writer_InitializeTiming();
	Writer_InitializeInstrumentation();
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_Shutdown()
{
	if (!GInitialized)
	{
		return;
	}

	Writer_Flush();

	GWorkerThreadQuit = true;
	ThreadJoin(GWorkerThread);
	ThreadDestroy(GWorkerThread);

	Writer_ShutdownControl();

	GHoldBuffer.Shutdown();
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
bool Writer_Connect(const ANSICHAR* Host)
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
template <typename Type>
struct TLateAtomic
{
	typedef TTraceAtomic<Type> InnerType;
	InnerType* operator -> () { return (InnerType*)Buffer; }
	alignas(InnerType) char Buffer[sizeof(InnerType)];
};

static TLateAtomic<uint32>	GEventUidCounter;	// = 0;
static TLateAtomic<FEvent*>	GHeadEvent;			// = nullptr;

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
	FEvent* Target,
	const FLiteralName& LoggerName,
	const FLiteralName& EventName,
	const FFieldDesc* FieldDescs,
	uint32 FieldCount,
	uint32 Flags)
{
	Writer_Initialize();

	// Assign a unique ID for this event
	uint32 Uid = GEventUidCounter->fetch_add(1, std::memory_order_relaxed);
	Uid += uint32(EKnownEventUids::User);

	if (Uid >= uint32(EKnownEventUids::Max))
	{
		Target->Uid = uint16(EKnownEventUids::Invalid);
		Target->Enabled.bOptedIn = false;
		Target->Enabled.Internal = 0;
		Target->bInitialized = true;
		return;
	}

	if (Flags & FEvent::Flag_Important)
	{
		Uid |= uint16(EKnownEventUids::Flag_Important);
	}

	uint32 LoggerHash = Writer_EventGetHash(LoggerName.Ptr);
	uint32 NameHash = Writer_EventGetHash(EventName.Ptr);

	// Fill out the target event's properties
 	Target->Uid = uint16(Uid);
	Target->LoggerHash = LoggerHash;
	Target->Hash = Writer_EventGetHash(LoggerHash, NameHash);
	Target->Enabled.Internal = !!(Flags & FEvent::Flag_Always);
	Target->Enabled.bOptedIn = false;
	Target->bInitialized = true;

	// Calculate the number of fields and size of name data.
	int NamesSize = LoggerName.Length + EventName.Length;
	for (uint32 i = 0; i < FieldCount; ++i)
	{
		NamesSize += FieldDescs[i].NameSize;
	}

	// Allocate the new event event in the log stream.
	uint16 EventSize = sizeof(FNewEventEvent);
	EventSize += sizeof(FNewEventEvent::Fields[0]) * FieldCount;
	EventSize += NamesSize;
	auto& Event = *(FNewEventEvent*)Writer_BeginLog(uint16(EKnownEventUids::NewEvent), EventSize);

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

	Writer_EndLog(&(uint8&)Event);

	// Add this new event into the list so we can look them up later.
	while (true)
	{
		FEvent* HeadEvent = GHeadEvent->load(std::memory_order_relaxed);
		Target->Handle = HeadEvent;
		if (GHeadEvent->compare_exchange_weak(HeadEvent, Target, std::memory_order_release))
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

		FEvent* Event = Private::GHeadEvent->load(std::memory_order_relaxed);
		for (; Event != nullptr; Event = (FEvent*)(Event->Handle))
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

	FEvent* Event = Private::GHeadEvent->load(std::memory_order_relaxed);
	for (; Event != nullptr; Event = (FEvent*)(Event->Handle))
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
