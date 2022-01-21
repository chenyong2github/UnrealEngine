// Copyright Epic Games, Inc. All Rights Reserved.
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Trace/Trace.inl"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"
#include "HAL/TlsAutoCleanup.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Misc/Parse.h"
#include "Containers/Map.h"
#include "Misc/MemStack.h"
#include "Misc/Crc.h"
#include "UObject/NameTypes.h"

#if CPUPROFILERTRACE_ENABLED

UE_TRACE_CHANNEL_DEFINE(CpuChannel)

UE_TRACE_EVENT_BEGIN(CpuProfiler, EventSpec, NoSync|Important)
	UE_TRACE_EVENT_FIELD(uint32, Id)
	UE_TRACE_EVENT_FIELD(UE::Trace::AnsiString, Name)
	#if CPUPROFILERTRACE_FILE_AND_LINE_ENABLED
	UE_TRACE_EVENT_FIELD(UE::Trace::AnsiString, File)
	UE_TRACE_EVENT_FIELD(uint32, Line)
	#endif
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CpuProfiler, EventBatch, NoSync)
	UE_TRACE_EVENT_FIELD(uint8[], Data)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CpuProfiler, EndCapture)
	UE_TRACE_EVENT_FIELD(uint8[], Data)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CpuProfiler, EndThread, NoSync)
UE_TRACE_EVENT_END()

struct FCpuProfilerTraceInternal
{
	enum
	{
		MaxBufferSize = 256,
		MaxEncodedEventSize = 15, // 10 + 5
		FullBufferThreshold = MaxBufferSize - MaxEncodedEventSize,
	};

	template<typename CharType>
	struct FDynamicScopeNameMapKeyFuncs
	{
		typedef const CharType* KeyType;
		typedef const CharType* KeyInitType;
		typedef const TPairInitializer<const CharType*, uint32>& ElementInitType;

		enum { bAllowDuplicateKeys = false };

		static FORCEINLINE bool Matches(const CharType* A, const CharType* B)
		{
			return TCString<CharType>::Stricmp(A, B) == 0;
		}

		static FORCEINLINE uint32 GetKeyHash(const CharType* Key)
		{
			return FCrc::Strihash_DEPRECATED(Key);
		}

		static FORCEINLINE KeyInitType GetSetKey(ElementInitType Element)
		{
			return Element.Key;
		}
	};

	struct FThreadBuffer
		: public FTlsAutoCleanup
	{
		FThreadBuffer()
		{
		}

		virtual ~FThreadBuffer()
		{
			UE_TRACE_LOG(CpuProfiler, EndThread, CpuChannel);
			// Clear the thread buffer pointer. In the rare event of there being scopes in the destructors of other
			// FTLSAutoCleanup instances. In that case a new buffer is created for that event only. There is no way of
			// controlling the order of destruction for FTLSAutoCleanup types.
			ThreadBuffer = nullptr;
		}

		uint64 LastCycle = 0;
		uint16 BufferSize = 0;
		uint8 Buffer[MaxBufferSize];
		FMemStackBase DynamicScopeNamesMemory;
		TMap<const ANSICHAR*, uint32, FDefaultSetAllocator, FDynamicScopeNameMapKeyFuncs<ANSICHAR>> DynamicAnsiScopeNamesMap;
		TMap<const TCHAR*, uint32, FDefaultSetAllocator, FDynamicScopeNameMapKeyFuncs<TCHAR>> DynamicTCharScopeNamesMap;
		TMap<FNameEntryId, uint32> DynamicFNameScopeNamesMap;
	};

	uint32 static GetNextSpecId();
	FORCENOINLINE static FThreadBuffer* CreateThreadBuffer();
	FORCENOINLINE static void FlushThreadBuffer(FThreadBuffer* ThreadBuffer);
	FORCENOINLINE static void EndCapture(FThreadBuffer* ThreadBuffer);

	static thread_local uint32 ThreadDepth;
	static thread_local FThreadBuffer* ThreadBuffer;
};

thread_local uint32 FCpuProfilerTraceInternal::ThreadDepth = 0;
thread_local FCpuProfilerTraceInternal::FThreadBuffer* FCpuProfilerTraceInternal::ThreadBuffer = nullptr;

FCpuProfilerTraceInternal::FThreadBuffer* FCpuProfilerTraceInternal::CreateThreadBuffer()
{
	ThreadBuffer = new FThreadBuffer();
	ThreadBuffer->Register();
	return ThreadBuffer;
}

void FCpuProfilerTraceInternal::FlushThreadBuffer(FThreadBuffer* InThreadBuffer)
{
	UE_TRACE_LOG(CpuProfiler, EventBatch, true)
		<< EventBatch.Data(InThreadBuffer->Buffer, InThreadBuffer->BufferSize);
	InThreadBuffer->BufferSize = 0;
	InThreadBuffer->LastCycle = 0;
}

void FCpuProfilerTraceInternal::EndCapture(FThreadBuffer* InThreadBuffer)
{
	UE_TRACE_LOG(CpuProfiler, EndCapture, true)
		<< EndCapture.Data(InThreadBuffer->Buffer, InThreadBuffer->BufferSize);
	InThreadBuffer->BufferSize = 0;
	InThreadBuffer->LastCycle = 0;
}

#define CPUPROFILERTRACE_OUTPUTBEGINEVENT_PROLOGUE() \
	++FCpuProfilerTraceInternal::ThreadDepth; \
	FCpuProfilerTraceInternal::FThreadBuffer* ThreadBuffer = FCpuProfilerTraceInternal::ThreadBuffer; \
	if (!ThreadBuffer) \
	{ \
		ThreadBuffer = FCpuProfilerTraceInternal::CreateThreadBuffer(); \
	} \

#define CPUPROFILERTRACE_OUTPUTBEGINEVENT_EPILOGUE() \
	uint64 Cycle = FPlatformTime::Cycles64(); \
	uint64 CycleDiff = Cycle - ThreadBuffer->LastCycle; \
	ThreadBuffer->LastCycle = Cycle; \
	uint8* BufferPtr = ThreadBuffer->Buffer + ThreadBuffer->BufferSize; \
	FTraceUtils::Encode7bit((CycleDiff << 1) | 1ull, BufferPtr); \
	FTraceUtils::Encode7bit(SpecId, BufferPtr); \
	ThreadBuffer->BufferSize = (uint16)(BufferPtr - ThreadBuffer->Buffer); \
	if (ThreadBuffer->BufferSize >= FCpuProfilerTraceInternal::FullBufferThreshold) \
	{ \
		FCpuProfilerTraceInternal::FlushThreadBuffer(ThreadBuffer); \
	}

void FCpuProfilerTrace::OutputBeginEvent(uint32 SpecId)
{
	CPUPROFILERTRACE_OUTPUTBEGINEVENT_PROLOGUE();
	CPUPROFILERTRACE_OUTPUTBEGINEVENT_EPILOGUE();
}

void FCpuProfilerTrace::OutputBeginDynamicEvent(const ANSICHAR* Name, const ANSICHAR* File, uint32 Line)
{
	CPUPROFILERTRACE_OUTPUTBEGINEVENT_PROLOGUE();
	uint32 SpecId = ThreadBuffer->DynamicAnsiScopeNamesMap.FindRef(Name);
	if (!SpecId)
	{
		int32 NameSize = strlen(Name) + 1;
		ANSICHAR* NameCopy = reinterpret_cast<ANSICHAR*>(ThreadBuffer->DynamicScopeNamesMemory.Alloc(NameSize, alignof(ANSICHAR)));
		FMemory::Memmove(NameCopy, Name, NameSize);
		SpecId = OutputEventType(NameCopy, File, Line);
		ThreadBuffer->DynamicAnsiScopeNamesMap.Add(NameCopy, SpecId);
	}
	CPUPROFILERTRACE_OUTPUTBEGINEVENT_EPILOGUE();
}

void FCpuProfilerTrace::OutputBeginDynamicEvent(const TCHAR* Name, const ANSICHAR* File, uint32 Line)
{
	CPUPROFILERTRACE_OUTPUTBEGINEVENT_PROLOGUE();
	uint32 SpecId = ThreadBuffer->DynamicTCharScopeNamesMap.FindRef(Name);
	if (!SpecId)
	{
		int32 NameSize = (FCString::Strlen(Name) + 1) * sizeof(TCHAR);
		TCHAR* NameCopy = reinterpret_cast<TCHAR*>(ThreadBuffer->DynamicScopeNamesMemory.Alloc(NameSize, alignof(TCHAR)));
		FMemory::Memmove(NameCopy, Name, NameSize);
		SpecId = OutputEventType(NameCopy, File, Line);
		ThreadBuffer->DynamicTCharScopeNamesMap.Add(NameCopy, SpecId);
	}
	CPUPROFILERTRACE_OUTPUTBEGINEVENT_EPILOGUE();
}

void FCpuProfilerTrace::OutputBeginDynamicEvent(const FName& Name, const ANSICHAR* File, uint32 Line)
{
	CPUPROFILERTRACE_OUTPUTBEGINEVENT_PROLOGUE();
	uint32 SpecId = ThreadBuffer->DynamicFNameScopeNamesMap.FindRef(Name.GetComparisonIndex());
	if (!SpecId)
	{
		const FNameEntry* NameEntry = Name.GetDisplayNameEntry();
		if (NameEntry->IsWide())
		{
			static WIDECHAR WideName[NAME_SIZE];
			NameEntry->GetWideName(WideName);
			SpecId = OutputEventType(WideName, File, Line);
		}
		else
		{
			static ANSICHAR AnsiName[NAME_SIZE];
			NameEntry->GetAnsiName(AnsiName);
			SpecId = OutputEventType(AnsiName, File, Line);
		}
		ThreadBuffer->DynamicFNameScopeNamesMap.Add(Name.GetComparisonIndex(), SpecId);
	}
	CPUPROFILERTRACE_OUTPUTBEGINEVENT_EPILOGUE();
}

#undef CPUPROFILERTRACE_OUTPUTBEGINEVENT_PROLOGUE
#undef CPUPROFILERTRACE_OUTPUTBEGINEVENT_EPILOGUE

void FCpuProfilerTrace::OutputEndEvent()
{
	--FCpuProfilerTraceInternal::ThreadDepth;
	FCpuProfilerTraceInternal::FThreadBuffer* ThreadBuffer = FCpuProfilerTraceInternal::ThreadBuffer;
	if (!ThreadBuffer)
	{
		ThreadBuffer = FCpuProfilerTraceInternal::CreateThreadBuffer();
	}
	uint64 Cycle = FPlatformTime::Cycles64();
	uint64 CycleDiff = Cycle - ThreadBuffer->LastCycle;
	ThreadBuffer->LastCycle = Cycle;
	uint8* BufferPtr = ThreadBuffer->Buffer + ThreadBuffer->BufferSize;
	FTraceUtils::Encode7bit(CycleDiff << 1, BufferPtr);
	ThreadBuffer->BufferSize = (uint16)(BufferPtr - ThreadBuffer->Buffer);
	if ((FCpuProfilerTraceInternal::ThreadDepth == 0) | (ThreadBuffer->BufferSize >= FCpuProfilerTraceInternal::FullBufferThreshold))
	{
		FCpuProfilerTraceInternal::FlushThreadBuffer(ThreadBuffer);
	}
}

uint32 FCpuProfilerTraceInternal::GetNextSpecId()
{
	static TAtomic<uint32> NextSpecId;
	return (NextSpecId++) + 1;
}

uint32 FCpuProfilerTrace::OutputEventType(const TCHAR* Name, const ANSICHAR* File, uint32 Line)
{
	uint32 SpecId = FCpuProfilerTraceInternal::GetNextSpecId();
	uint16 NameLen = uint16(FCString::Strlen(Name));
	uint32 DataSize = NameLen * sizeof(ANSICHAR); // EventSpec.Name is traced as UE::Trace::AnsiString
#if CPUPROFILERTRACE_FILE_AND_LINE_ENABLED
	uint16 FileLen = (File != nullptr) ? uint16(strlen(File)) : 0;
	DataSize += FileLen * sizeof(ANSICHAR);
#endif
	UE_TRACE_LOG(CpuProfiler, EventSpec, CpuChannel, DataSize)
		<< EventSpec.Id(SpecId)
		<< EventSpec.Name(Name, NameLen)
#if CPUPROFILERTRACE_FILE_AND_LINE_ENABLED
		<< EventSpec.File(File, FileLen)
		<< EventSpec.Line(Line)
#endif
	;
	return SpecId;
}

uint32 FCpuProfilerTrace::OutputEventType(const ANSICHAR* Name, const ANSICHAR* File, uint32 Line)
{
	uint32 SpecId = FCpuProfilerTraceInternal::GetNextSpecId();
	uint16 NameLen = uint16(strlen(Name));
	uint32 DataSize = NameLen * sizeof(ANSICHAR);
#if CPUPROFILERTRACE_FILE_AND_LINE_ENABLED
	uint16 FileLen = (File != nullptr) ? uint16(strlen(File)) : 0;
	DataSize += FileLen * sizeof(ANSICHAR);
#endif
	UE_TRACE_LOG(CpuProfiler, EventSpec, CpuChannel, DataSize)
		<< EventSpec.Id(SpecId)
		<< EventSpec.Name(Name, NameLen)
#if CPUPROFILERTRACE_FILE_AND_LINE_ENABLED
		<< EventSpec.File(File, FileLen)
		<< EventSpec.Line(Line)
#endif
	;
	return SpecId;
}

#endif
