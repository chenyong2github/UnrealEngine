// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Engine.h"
#include "Containers/ArrayView.h"
#include "CoreGlobals.h"
#include "HAL/UnrealMemory.h"
#include "Trace/Analysis.h"
#include "Trace/Analyzer.h"
#include "Trace/Detail/EventDef.h"
#include "Transport/Transport.h"
#include "Transport/TlsTransport.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FFnv1aHash
{
public:
					FFnv1aHash() = default;
					FFnv1aHash(uint32 PrevResult)		{ Result = PrevResult; }
	void			Add(const ANSICHAR* String)			{ for (; *String; ++String) { Add(*String); } }
	const uint8*	Add(const uint8* Data, uint32 Size)	{ for (uint32 i = 0; i < Size; ++Data, ++i) { Add(*Data); } return Data; }
	void			Add(uint8 Value)					{ Result ^= Value; Result *= 0x01000193; }
	uint32			Get() const							{ return Result; }

private:
	uint32			Result = 0x811c9dc5;
	// uint32: bias=0x811c9dc5			prime=0x01000193
	// uint64: bias=0xcbf29ce484222325	prime=0x00000100000001b3;
};



////////////////////////////////////////////////////////////////////////////////
struct FAnalysisEngine::FDispatch
{
	struct FField
	{
		uint32		Hash;
		uint16		Offset;
		uint16		Size;
		uint16		NameOffset;			// From FField ptr
		int16		SizeAndType;		// value == byte_size, sign == float < 0 < int
	};

	uint16			Uid;
	uint16			FirstRoute;
	uint16			FieldCount;			// Implicit logger name offset; Fields + FieldCount
	uint16			EventSize;
	uint16			EventNameOffset;	// From FDispatch ptr
	uint16			_Unused0;
	FField			Fields[];
};



////////////////////////////////////////////////////////////////////////////////
uint32 IAnalyzer::FEventTypeInfo::GetId() const
{
	const auto& Inner = *(const FAnalysisEngine::FDispatch*)this;
	return Inner.Uid;
}

////////////////////////////////////////////////////////////////////////////////
uint32 IAnalyzer::FEventTypeInfo::GetSize() const
{
	const auto& Inner = *(const FAnalysisEngine::FDispatch*)this;
	return Inner.EventSize;
}

////////////////////////////////////////////////////////////////////////////////
const ANSICHAR* IAnalyzer::FEventTypeInfo::GetName() const
{
	const auto& Inner = *(const FAnalysisEngine::FDispatch*)this;
	return (const ANSICHAR*)(UPTRINT(&Inner) + Inner.EventNameOffset);
}

////////////////////////////////////////////////////////////////////////////////
const ANSICHAR* IAnalyzer::FEventTypeInfo::GetLoggerName() const
{
	const auto& Inner = *(const FAnalysisEngine::FDispatch*)this;
	return (const ANSICHAR*)(Inner.Fields + Inner.FieldCount);
}

////////////////////////////////////////////////////////////////////////////////
uint32 IAnalyzer::FEventTypeInfo::GetFieldCount() const
{
	const auto& Inner = *(const FAnalysisEngine::FDispatch*)this;
	return Inner.FieldCount;
}

////////////////////////////////////////////////////////////////////////////////
const IAnalyzer::FEventFieldInfo* IAnalyzer::FEventTypeInfo::GetFieldInfo(uint32 Index) const
{
	if (Index >= GetFieldCount())
{
		return nullptr;
}

	const auto& Inner = *(const FAnalysisEngine::FDispatch*)this;
	return (const IAnalyzer::FEventFieldInfo*)(Inner.Fields + Index);
		}



////////////////////////////////////////////////////////////////////////////////
const ANSICHAR* IAnalyzer::FEventFieldInfo::GetName() const
	{
	const auto* Inner = (const FAnalysisEngine::FDispatch::FField*)this;
	return (const ANSICHAR*)(UPTRINT(Inner) + Inner->NameOffset);
	}

////////////////////////////////////////////////////////////////////////////////
uint32 IAnalyzer::FEventFieldInfo::GetOffset() const
	{
	const auto* Inner = (const FAnalysisEngine::FDispatch::FField*)this;
	return Inner->Offset;
	}

////////////////////////////////////////////////////////////////////////////////
uint32 IAnalyzer::FEventFieldInfo::GetSize() const
	{
	const auto* Inner = (const FAnalysisEngine::FDispatch::FField*)this;
	return (Inner->SizeAndType < 0) ? -(Inner->SizeAndType) : Inner->SizeAndType;
	}

////////////////////////////////////////////////////////////////////////////////
IAnalyzer::FEventFieldInfo::EType IAnalyzer::FEventFieldInfo::GetType() const
	{
	const auto* Inner = (const FAnalysisEngine::FDispatch::FField*)this;
	if (Inner->SizeAndType > 0)
		{
		return EType::Integer;
	}

	if (Inner->SizeAndType < 0)
	{
		return EType::Float;
	}

	return EType::None;
	}



////////////////////////////////////////////////////////////////////////////////
struct FAnalysisEngine::FEventDataInfo
{
	const FDispatch&	Dispatch;
	const uint8*			Ptr;
	uint16					Size;
};

////////////////////////////////////////////////////////////////////////////////
const IAnalyzer::FEventTypeInfo& IAnalyzer::FEventData::GetTypeInfo() const
{
	const auto& Info = *(const FAnalysisEngine::FEventDataInfo*)this;
	return (const FEventTypeInfo&)(Info.Dispatch);
}

////////////////////////////////////////////////////////////////////////////////
const void* IAnalyzer::FEventData::GetValueImpl(const ANSICHAR* FieldName, int16& SizeAndType) const
{
	const auto& Info = *(const FAnalysisEngine::FEventDataInfo*)this;

	FFnv1aHash Hash;
	Hash.Add(FieldName);
	uint32 NameHash = Hash.Get();

	for (int i = 0, n = Info.Dispatch.FieldCount; i < n; ++i)
	{
		const auto& Field = Info.Dispatch.Fields[i];
		if (Field.Hash == NameHash)
		{
			SizeAndType = Field.SizeAndType;
			return (Info.Ptr + Field.Offset);
		}
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
const uint8* IAnalyzer::FEventData::GetAttachment() const
{
	const auto& Info = *(const FAnalysisEngine::FEventDataInfo*)this;
	return Info.Ptr + Info.Dispatch.EventSize;
}

////////////////////////////////////////////////////////////////////////////////
uint32 IAnalyzer::FEventData::GetAttachmentSize() const
{
	const auto& Info = *(const FAnalysisEngine::FEventDataInfo*)this;
	return Info.Size - Info.Dispatch.EventSize;
}

////////////////////////////////////////////////////////////////////////////////
const uint8* IAnalyzer::FEventData::GetRawPointer() const
{
	const auto& Info = *(const FAnalysisEngine::FEventDataInfo*)this;
	return Info.Ptr;
}



////////////////////////////////////////////////////////////////////////////////
enum ERouteId : uint16
{
	RouteId_NewEvent,
	RouteId_NewTrace,
	RouteId_Timing,
};

////////////////////////////////////////////////////////////////////////////////
// This is used to influence the order of routes (routes are sorted by hash).
enum EKnownRouteHashes : uint32
{
	RouteHash_NewEvent = 0, // must be 0 to match traces.
	RouteHash_AllEvents,	
};

////////////////////////////////////////////////////////////////////////////////
FAnalysisEngine::FAnalysisEngine(TArray<IAnalyzer*>&& InAnalyzers)
: Analyzers(MoveTemp(InAnalyzers))
{
	uint16 SelfIndex = Analyzers.Num();
	Analyzers.Add(this);

	// Manually add event routing for known events, and those we don't quite know
	// yet but are expecting.
	FDispatch* NewEventDispatch = AddDispatch(uint16(FNewEventEvent::Uid), 0);
	NewEventDispatch->FirstRoute = 0;
	AddRoute(SelfIndex, RouteId_NewEvent, RouteHash_NewEvent);
	AddRoute(SelfIndex, RouteId_NewTrace, "$Trace", "NewTrace");
	AddRoute(SelfIndex, RouteId_Timing, "$Trace", "Timing");
}

////////////////////////////////////////////////////////////////////////////////
FAnalysisEngine::~FAnalysisEngine()
{
	for (IAnalyzer* Analyzer : Analyzers)
	{
		if (Analyzer != nullptr)
		{
		Analyzer->OnAnalysisEnd();
	}
	}

	for (FDispatch* Dispatch : Dispatches)
	{
		FMemory::Free(Dispatch);
	}

	delete Transport;
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::RetireAnalyzer(uint32 AnalyzerIndex)
{
	if (AnalyzerIndex >= uint32(Analyzers.Num()))
	{
		return;
}

	IAnalyzer* Analyzer = Analyzers[AnalyzerIndex]; // this line is brought to you with the word "Analyzer" (mostly).
	if (Analyzer == nullptr)
{
		return;
	}

	Analyzer->OnAnalysisEnd();
	Analyzers[AnalyzerIndex] = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
bool FAnalysisEngine::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	switch (RouteId)
	{
	case RouteId_NewEvent:	OnNewEventInternal(Context);	break;
	case RouteId_NewTrace:	OnNewTrace(Context);			break;
	case RouteId_Timing:	OnTiming(Context);				break;
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::AddRoute(
	uint16 AnalyzerIndex,
	uint16 Id,
	const ANSICHAR* Logger,
	const ANSICHAR* Event)
{
	FFnv1aHash Hash;
	Hash.Add(Logger);
	Hash.Add(Event);
	AddRoute(AnalyzerIndex, Id, Hash.Get());
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::AddRoute(
	uint16 AnalyzerIndex,
	uint16 Id,
	uint32 Hash)
{
	check(AnalyzerIndex < Analyzers.Num());

	FRoute& Route = Routes.Emplace_GetRef();
	Route.Id = Id;
	Route.Hash = Hash;
	Route.Count = 1;
	Route.AnalyzerIndex = AnalyzerIndex;
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnNewTrace(const FOnEventContext& Context)
{
	struct : IAnalyzer::FInterfaceBuilder
	{
		virtual void RouteEvent(uint16 RouteId, const ANSICHAR* Logger, const ANSICHAR* Event) override
		{
			Self->AddRoute(AnalyzerIndex, RouteId, Logger, Event);
		}

		virtual void RouteAllEvents(uint16 RouteId) override
		{
			Self->AddRoute(AnalyzerIndex, RouteId, RouteHash_AllEvents);
		}

		FAnalysisEngine* Self;
		uint16 AnalyzerIndex;
	} Builder;
	Builder.Self = this;

	FOnAnalysisContext OnAnalysisContext = { { SessionContext }, Builder };
	for (uint16 i = 0, n = Analyzers.Num(); i < n; ++i)
	{
		Builder.AnalyzerIndex = i;
		Analyzers[i]->OnAnalysisBegin(OnAnalysisContext);
	}

	Algo::SortBy(Routes, [] (const FRoute& Route) { return Route.Hash; });

	FRoute* Cursor = Routes.GetData();
	Cursor->Count = 1;

	for (uint16 i = 1, n = Routes.Num(); i < n; ++i)
	{
		if (Routes[i].Hash == Cursor->Hash)
		{
			Cursor->Count++;
		}
		else
		{
			Cursor = Routes.GetData() + i;
			Cursor->Count = 1;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnTiming(const FOnEventContext& Context)
{
	SessionContext.StartCycle = Context.EventData.GetValue<uint64>("StartCycle");
	SessionContext.CycleFrequency = Context.EventData.GetValue<uint64>("CycleFrequency");
}

////////////////////////////////////////////////////////////////////////////////
FAnalysisEngine::FDispatch* FAnalysisEngine::AddDispatch(
	uint16 Uid,
	uint16 FieldCount,
	uint16 ExtraData)
{
	// Make sure there's enough space in the dispatch table, failing gently if
	// there appears to be an existing entry.
	if (Uid < Dispatches.Num())
	{
		if (Dispatches[Uid] != nullptr)
		{
			return nullptr;
		}
	}
	else
{
		Dispatches.SetNum(Uid + 1);
	}

	// Allocate a block of memory to hold the dispatch
	uint32 Size = sizeof(FDispatch) + (sizeof(FDispatch::FField) * FieldCount) + ExtraData;
	auto* Dispatch = (FDispatch*)FMemory::Malloc(Size);
	Dispatch->Uid = Uid;
	Dispatch->FieldCount = FieldCount;
	Dispatch->EventSize = 0;
	Dispatch->FirstRoute = ~uint16(0);

	// Add the new dispatch in the dispatch table
	Dispatches[Uid] = Dispatch;
	return Dispatch;
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnNewEventInternal(const FOnEventContext& Context)
{
	const FEventDataInfo& EventData = (const FEventDataInfo&)(Context.EventData);
	const auto& NewEvent = *(FNewEventEvent*)(EventData.Ptr);

	// Create a new dispatch with enough space to store new event's various names
	uint16 NameDataSize = NewEvent.LoggerNameSize + NewEvent.EventNameSize;
	NameDataSize += NewEvent.FieldCount + 2; // null terminators
	for (int i = 0, n = NewEvent.FieldCount; i < n; ++i)
	{
		NameDataSize += NewEvent.Fields[i].NameSize + 1;
	}

	FDispatch* Dispatch = AddDispatch(NewEvent.EventUid, NewEvent.FieldCount, NameDataSize);
	if (Dispatch == nullptr)
	{
		return;
	}

	if (NewEvent.FieldCount)
	{
		auto& LastField = NewEvent.Fields[NewEvent.FieldCount - 1];
		Dispatch->EventSize = LastField.Offset + LastField.Size;
	}

	const uint8* NameCursor = (const uint8*)(NewEvent.Fields + NewEvent.FieldCount);

	// Calculate this dispatch's hash.
	FFnv1aHash DispatchHash;
	NameCursor = DispatchHash.Add(NameCursor, NewEvent.LoggerNameSize);
	NameCursor = DispatchHash.Add(NameCursor, NewEvent.EventNameSize);

	// Fill out the fields
	for (int i = 0, n = NewEvent.FieldCount; i < n; ++i)
	{
		const auto& In = NewEvent.Fields[i];
		auto& Out = Dispatch->Fields[i];

		FFnv1aHash FieldHash;
		NameCursor = FieldHash.Add(NameCursor, In.NameSize);

		Out.Hash = FieldHash.Get();
		Out.Offset = In.Offset;
		Out.Size = In.Size;

		Out.SizeAndType = 1 << (In.TypeInfo & _Field_Pow2SizeMask);
		if ((In.TypeInfo & _Field_CategoryMask) == _Field_Float)
		{
			Out.SizeAndType = -Out.SizeAndType;
		}
	}

	// Write out names with null terminators.
	NameCursor = (const uint8*)(NewEvent.Fields + NewEvent.FieldCount);
	uint8* WriteCursor = (uint8*)(Dispatch->Fields + Dispatch->FieldCount);
	auto WriteName = [&] (uint32 Size)
	{
		memcpy(WriteCursor, NameCursor, Size);
		NameCursor += Size;
		WriteCursor += Size + 1;
		WriteCursor[-1] = '\0';
	};

	UPTRINT EventNameOffset = UPTRINT(Dispatch->Fields + NewEvent.FieldCount);
	EventNameOffset -= UPTRINT(Dispatch);
	EventNameOffset += NewEvent.LoggerNameSize + 1;
	Dispatch->EventNameOffset = uint16(EventNameOffset);

	WriteName(NewEvent.LoggerNameSize);
	WriteName(NewEvent.EventNameSize);
	for (int i = 0, n = NewEvent.FieldCount; i < n; ++i)
	{
		auto& Out = Dispatch->Fields[i];
		Out.NameOffset = uint16(UPTRINT(WriteCursor) - UPTRINT(Dispatch));

		WriteName(NewEvent.Fields[i].NameSize);
	}

	// Sort by hash so we can binary search when looking up.
	TArrayView<FDispatch::FField> Fields(Dispatch->Fields, Dispatch->FieldCount);
	Algo::SortBy(Fields, [] (const auto& Field) { return Field.Hash; });

	// Fix up field name offsets
	for (int i = 0, n = NewEvent.FieldCount; i < n; ++i)
	{
		auto& Out = Dispatch->Fields[i];
		Out.NameOffset = uint16(UPTRINT(Dispatch) + Out.NameOffset - UPTRINT(&Out));
	}

	// Find routes that have subscribed to this event.
	for (uint16 i = 0, n = Routes.Num(); i < n; ++i)
	{
		if (Routes[i].Hash == DispatchHash.Get())
		{
			Dispatch->FirstRoute = i;
			break;
		}
	}

	// Inform routes that a new event has been declared.
	uint32 RouteCount = Routes.Num();
	if (Dispatch->FirstRoute < RouteCount)
	{
		const FRoute* Route = Routes.GetData() + Dispatch->FirstRoute;
		for (uint32 n = Route->Count; n--; ++Route)
		{
			IAnalyzer* Analyzer = Analyzers[Route->AnalyzerIndex];
			if (Analyzer == nullptr)
			{
				continue;
			}

			if (!Analyzer->OnNewEvent(Route->Id, *(FEventTypeInfo*)Dispatch))
		{
				RetireAnalyzer(Route->AnalyzerIndex);
			}
		}
	}

	const FRoute* Route = Routes.GetData() + 1;
	if (RouteCount > 1 && Route->Hash == RouteHash_AllEvents)
	{
		for (uint32 n = Route->Count; n--; ++Route)
		{
			IAnalyzer* Analyzer = Analyzers[Route->AnalyzerIndex];
			if (Analyzer == nullptr)
			{
				continue;
			}

			if (!Analyzer->OnNewEvent(Route->Id, *(FEventTypeInfo*)Dispatch))
			{
				RetireAnalyzer(Route->AnalyzerIndex);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
bool FAnalysisEngine::EstablishTransport(FStreamReader::FData& Data)
{
	const struct {
		uint8 Format;
		uint8 Parameter;
	}* Header = decltype(Header)(Data.GetPointer(sizeof(*Header)));
	if (Header == nullptr)
	{
		return false;
	}

	// Check for the magic uint32. Early traces did not include this as it was
	// used to validate a inbound socket connection and then discarded.
	if (Header->Format == 'E' || Header->Format == 'T')
	{
		const uint32* Magic = (const uint32*)(Data.GetPointer(sizeof(*Magic)));
		if (*Magic == 'ECRT')
		{
			// Source is big-endian which we don't currently support
			return false;
		}

		if (*Magic == 'TRCE')
		{
			Data.Advance(sizeof(*Magic));
			return EstablishTransport(Data);
		}

		return false;
	}

	switch (Header->Format)
	{
	case 1:		Transport = new FTransport(); break;
	case 2:		Transport = new FTlsTransport(); break;
	default:	return false;
	//case 'E':	/* See the magic above */ break;
	//case 'T':	/* See the magic above */ break;
	}

	Data.Advance(sizeof(*Header));
	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool FAnalysisEngine::OnData(FStreamReader::FData& Data)
{
	if (Transport == nullptr)
	{
		// Ensure we've a reasonable amount of data to establish the transport with
		if (Data.GetPointer(32) == nullptr)
		{
			return true;
		}

		if (!EstablishTransport(Data))
		{
			return false;
		}
	}

	struct FEventHeader
	{
		uint16	Uid;
		uint16	Size;
		uint8	EventData[];
	};

	Transport->SetSource(Data);

	while (true)
	{
		const auto* Header = Transport->GetPointer<FEventHeader>();
		if (Header == nullptr)
		{
			break;
		}

		uint32 BlockSize = Header->Size + sizeof(FEventHeader);
		Header = Transport->GetPointer<FEventHeader>(BlockSize);
		if (Header == nullptr)
		{
			break;
		}

		uint16 Uid = uint16(Header->Uid & ((1 << 14) - 1));
		if (Uid >= Dispatches.Num())
		{
			return false;
		}

		Transport->Advance(BlockSize);

		const FDispatch* Dispatch = Dispatches[Uid];
		if (Dispatch == nullptr)
		{
			return false;
		}

		FEventDataInfo EventDataInfo = { *Dispatch, Header->EventData, Header->Size };
		const FEventData& EventData = (FEventData&)EventDataInfo;

		uint32 RouteCount = Routes.Num();
		if (Dispatch->FirstRoute < RouteCount)
		{
		const FRoute* Route = Routes.GetData() + Dispatch->FirstRoute;
		for (uint32 n = Route->Count; n--; ++Route)
		{
			IAnalyzer* Analyzer = Analyzers[Route->AnalyzerIndex];
				if (Analyzer == nullptr)
				{
					continue;
				}

				if (!Analyzer->OnEvent(Route->Id, { SessionContext, EventData }))
				{
					RetireAnalyzer(Route->AnalyzerIndex);
				}
			}
		}

		// Don't broadcast internal events
		if (!Uid) // TODO: This makes assumptions about EKnownEventUids::User. Instead we should add this information to the trace.
		{
			continue;
		}

		const FRoute* Route = Routes.GetData() + 1;
		if (RouteCount > 1 && Route->Hash == RouteHash_AllEvents)
		{
			for (uint32 n = Route->Count; n--; ++Route)
			{
				IAnalyzer* Analyzer = Analyzers[Route->AnalyzerIndex];
				if (Analyzer == nullptr)
				{
					continue;
				}

				if (!Analyzer->OnEvent(Route->Id, { SessionContext, EventData }))
				{
					RetireAnalyzer(Route->AnalyzerIndex);
				}
			}
		}
	}

	// If there's no analyzers left we might as well not continue
	int32 ActiveAnalyzerCount = 0;
	for (IAnalyzer* Analyzer : Analyzers)
	{
		if ((Analyzer != nullptr) && (Analyzer != this))
		{
			ActiveAnalyzerCount++;
		}
	}

	return (ActiveAnalyzerCount > 0);
}

} // namespace Trace
