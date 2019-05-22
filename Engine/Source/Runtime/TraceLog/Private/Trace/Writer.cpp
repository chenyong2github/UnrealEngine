// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Trace/Private/Trace.h"

#if UE_TRACE_ENABLED

#include "Trace/Trace.h"

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

	UE_TRACE_EVENT_BEGIN($Trace, Timing)
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

	GActiveBuffer.store(Buffers[0], std::memory_order_release);
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_ShutdownBuffers()
{
	MemoryFree(GAllocBase, GAllocSize);
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_RetireBuffer(FBuffer* Buffer, void (*DataSink)(const uint8*, uint32))
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
		DataSink(GTailBuffer->Data, SendSize);
	}
	uint64 SendTsc = Writer_GetTimestamp();

	FBuffer* Next = GTailBuffer->Next.load(std::memory_order_relaxed);
	new (GTailBuffer) FBuffer();

	GHeadBuffer->Next.store(GTailBuffer, std::memory_order_release);
	GHeadBuffer = GTailBuffer;
	GTailBuffer = Next;
	uint64 DoneTsc = Writer_GetTimestamp();

	if (true)
	{
		uint8 PerfEventBuffer[UE_TRACE_EVENT_SIZE($Trace, PerfWorker) + FEvent::HeaderSize];
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
		if (GTailBuffer == Buffer)
		{
			return;
		}

		Writer_RetireBuffer(Buffer, DataSink);
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

	uint32 PerfEventSize = UE_TRACE_EVENT_SIZE($Trace, PerfNextBuffer) + FEvent::HeaderSize;
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

	uint64 DoneTsc = Writer_GetTimestamp();
	UE_TRACE_LOG($Trace, PerfNextBuffer, Out)
		<< PerfNextBuffer.Start(StartTsc)
		<< PerfNextBuffer.Acquire(uint32(AcquireTsc - StartTsc))
		<< PerfNextBuffer.Done(uint32(DoneTsc - StartTsc))
		<< PerfNextBuffer.ThreadId(uint16(ThreadGetCurrentId()));

	return Out + PerfEventSize;
}



////////////////////////////////////////////////////////////////////////////////
class FHoldBuffer
{
public:
	void				Init();
	void				Shutdown();
	void				Write(const void* Data, uint32 Size);
	bool				IsFull() const	{ return Full; }
	const uint8*		GetData() const { return Base; }
	uint32				GetSize() const { return Used; }

private:
	static const uint32	PageShift = 16;
	static const uint32	PageSize = 1 << PageShift;
	static const uint32	MaxPages = (4 * 1024 * 1024) >> PageShift;
	uint8*				Base;
	int32				Used;
	uint16				MappedPageCount;
	bool				Full;
};

////////////////////////////////////////////////////////////////////////////////
void FHoldBuffer::Init()
{
	Base = MemoryReserve(FHoldBuffer::PageSize * FHoldBuffer::MaxPages);
	Used = 0;
	MappedPageCount = 0;
	Full = false;
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
		if (HotPageCount >= FHoldBuffer::MaxPages)
		{
			Full = true;
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
			GDataState = EDataState::Sending;

			// Handshake.
			const uint32 Magic = 'TRCE';
			TcpSocketSend(GDataHandle, &Magic, sizeof(Magic));

			// Stream header
			const struct {
				uint8 Format;
				uint8 Parameter;
			} TransportHeader = { 1 };
			TcpSocketSend(GDataHandle, &TransportHeader, sizeof(TransportHeader));

			// Passively collected data
			TcpSocketSend(GDataHandle, GHoldBuffer.GetData(), GHoldBuffer.GetSize());
			GHoldBuffer.Shutdown();
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
		bool Overflown = GHoldBuffer.IsFull();
		if (Overflown && GDataState != EDataState::Partial)
		{
			/* TODO: turn off non-retail events */
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
bool Writer_ConnectImpl(const ANSICHAR*);
bool Writer_ToggleEventImpl(const ANSICHAR*, const ANSICHAR*, bool);

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
#if PLATFORM_PS4
	GControlState = EControlState::Failed;
	return false;
#endif

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

		const uint32 ArgN = 16;
		uint32 ArgC = 0;
		const ANSICHAR* ArgV[ArgN];

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

				if (ArgC < ArgN)
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
	Writer_ControlListen();

	Writer_ControlAddCommand("Connect", nullptr,
		[] (void*, uint32 ArgC, ANSICHAR const* const* ArgV)
		{
			if (ArgC > 0)
			{
				Writer_ConnectImpl(ArgV[0]);
			}
		}
	);

	Writer_ControlAddCommand("ToggleEvent", nullptr,
		[] (void*, uint32 ArgC, ANSICHAR const* const* ArgV)
		{
			if (ArgC < 2)
			{
				return;
			}
			const ANSICHAR* LoggerName = ArgV[0];
			const ANSICHAR* EventName = ArgV[1];
			const ANSICHAR* State = (ArgC > 2) ? ArgV[2] : "";
			Writer_ToggleEventImpl(LoggerName, EventName, State[0] != '0');
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
	UE_TRACE_EVENT_BEGIN($Trace, NewTrace)
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
bool Writer_ConnectImpl(const ANSICHAR* Host)
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
bool Writer_ToggleEventImpl(const ANSICHAR* LoggerName, const ANSICHAR* EventName, bool State)
{
	Writer_Initialize();

	if (FEvent* Event = FEvent::Find(LoggerName, EventName))
	{
		Event->bEnabled = State;
		return true;
	}

	return false;
}

} // namespace Private
} // namespace Trace

#endif // UE_TRACE_ENABLED
