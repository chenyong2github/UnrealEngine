// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Engine.h"
#include "Containers/ArrayView.h"
#include "CoreGlobals.h"
#include "HAL/UnrealMemory.h"
#include "StreamReader.h"
#include "Trace/Analysis.h"
#include "Trace/Analyzer.h"
#include "Trace/Detail/Protocol.h"
#include "Transport/PacketTransport.h"
#include "Transport/Transport.h"
#include "Transport/TidPacketTransport.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FFnv1aHash
{
public:
			FFnv1aHash() = default;
			FFnv1aHash(uint32 PrevResult)		{ Result = PrevResult; }
	void	Add(const ANSICHAR* String)			{ for (; *String; ++String) { Add(*String); } }
	void	Add(const uint8* Data, uint32 Size)	{ for (uint32 i = 0; i < Size; ++Data, ++i) { Add(*Data); } }
	void	Add(uint8 Value)					{ Result ^= Value; Result *= 0x01000193; }
	uint32	Get() const							{ return Result; }

private:
	uint32	Result = 0x811c9dc5;
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

	uint32			Hash				= 0;
	uint16			Uid					= 0;
	uint16			FirstRoute			= ~uint16(0);
	uint16			FieldCount			= 0;
	uint16			EventSize			= 0;
	uint16			LoggerNameOffset	= 0;	// From FDispatch ptr
	uint16			EventNameOffset		= 0;	// From FDispatch ptr
	FField			Fields[];
};



////////////////////////////////////////////////////////////////////////////////
class FAnalysisEngine::FDispatchBuilder
{
public:
					FDispatchBuilder();
	void			SetUid(uint16 Uid);
	void			SetLoggerName(const ANSICHAR* Name, int32 NameSize=-1);
	void			SetEventName(const ANSICHAR* Name, int32 NameSize=-1);
	void			AddField(const ANSICHAR* Name, int32 NameSize, uint16 Offset, uint16 Size, FEventFieldInfo::EType TypeId, uint16 TypeSize);
	FDispatch*		Finalize();

private:
	uint32			AppendName(const ANSICHAR* Name, int32 NameSize);
	TArray<uint8>	Buffer;
	TArray<uint8>	NameBuffer;
};

////////////////////////////////////////////////////////////////////////////////
FAnalysisEngine::FDispatchBuilder::FDispatchBuilder()
{
	Buffer.SetNum(sizeof(FDispatch));

	auto* Dispatch = (FDispatch*)(Buffer.GetData());
	new (Dispatch) FDispatch();
}

////////////////////////////////////////////////////////////////////////////////
FAnalysisEngine::FDispatch* FAnalysisEngine::FDispatchBuilder::Finalize()
{
	int32 Size = Buffer.Num() + NameBuffer.Num();
	auto* Dispatch = (FDispatch*)FMemory::Malloc(Size);
	memcpy(Dispatch, Buffer.GetData(), Buffer.Num());
	memcpy(Dispatch->Fields + Dispatch->FieldCount, NameBuffer.GetData(), NameBuffer.Num());

	// Sort by hash so we can binary search when looking up.
	TArrayView<FDispatch::FField> Fields(Dispatch->Fields, Dispatch->FieldCount);
	Algo::SortBy(Fields, [] (const auto& Field) { return Field.Hash; });

	// Fix up name offsets
	for (int i = 0, n = Dispatch->FieldCount; i < n; ++i)
	{
		auto* Field = Dispatch->Fields + i;
		Field->NameOffset += Buffer.Num() - uint32(UPTRINT(Field) - UPTRINT(Dispatch));
	}

	// Calculate this dispatch's hash.
	if (Dispatch->LoggerNameOffset || Dispatch->EventNameOffset)
	{
		Dispatch->LoggerNameOffset += Buffer.Num();
		Dispatch->EventNameOffset += Buffer.Num();

		FFnv1aHash Hash;
		Hash.Add((const ANSICHAR*)Dispatch + Dispatch->LoggerNameOffset);
		Hash.Add((const ANSICHAR*)Dispatch + Dispatch->EventNameOffset);
		Dispatch->Hash = Hash.Get();
	}

	return Dispatch;
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::FDispatchBuilder::SetUid(uint16 Uid)
{
	auto* Dispatch = (FDispatch*)(Buffer.GetData());
	Dispatch->Uid = Uid;
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::FDispatchBuilder::SetLoggerName(const ANSICHAR* Name, int32 NameSize)
{
	auto* Dispatch = (FDispatch*)(Buffer.GetData());
	Dispatch->LoggerNameOffset += AppendName(Name, NameSize);
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::FDispatchBuilder::SetEventName(const ANSICHAR* Name, int32 NameSize)
{
	auto* Dispatch = (FDispatch*)(Buffer.GetData());
	Dispatch->EventNameOffset = AppendName(Name, NameSize);
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::FDispatchBuilder::AddField(const ANSICHAR* Name, int32 NameSize, uint16 Offset, uint16 Size, FEventFieldInfo::EType TypeId, uint16 TypeSize)
{
	int32 Bufoff = Buffer.AddUninitialized(sizeof(FDispatch::FField));
	auto* Field = (FDispatch::FField*)(Buffer.GetData() + Bufoff);
	Field->NameOffset = AppendName(Name, NameSize);
	Field->Offset = Offset;
	Field->Size = Size;
	Field->SizeAndType = TypeSize;
	if (TypeId == FEventFieldInfo::EType::Float)
	{
		Field->SizeAndType *= -1;
	}

	FFnv1aHash Hash;
	Hash.Add((const uint8*)Name, NameSize);
	Field->Hash = Hash.Get();
	
	auto* Dispatch = (FDispatch*)(Buffer.GetData());
	Dispatch->FieldCount++;
	Dispatch->EventSize += Field->Size;
}

////////////////////////////////////////////////////////////////////////////////
uint32 FAnalysisEngine::FDispatchBuilder::AppendName(const ANSICHAR* Name, int32 NameSize)
{
	if (NameSize < 0)
	{
		NameSize = int32(FCStringAnsi::Strlen(Name));
	}

	int32 Ret = NameBuffer.AddUninitialized(NameSize + 1);
	uint8* Out = NameBuffer.GetData() + Ret;
	memcpy(Out, Name, NameSize);
	Out[NameSize] = '\0';
	return Ret;
}



////////////////////////////////////////////////////////////////////////////////
uint32 IAnalyzer::FEventTypeInfo::GetId() const
{
	const auto* Inner = (const FAnalysisEngine::FDispatch*)this;
	return Inner->Uid;
}

////////////////////////////////////////////////////////////////////////////////
uint32 IAnalyzer::FEventTypeInfo::GetSize() const
{
	const auto* Inner = (const FAnalysisEngine::FDispatch*)this;
	return Inner->EventSize;
}

////////////////////////////////////////////////////////////////////////////////
const ANSICHAR* IAnalyzer::FEventTypeInfo::GetName() const
{
	const auto* Inner = (const FAnalysisEngine::FDispatch*)this;
	return (const ANSICHAR*)Inner + Inner->EventNameOffset;
}

////////////////////////////////////////////////////////////////////////////////
const ANSICHAR* IAnalyzer::FEventTypeInfo::GetLoggerName() const
{
	const auto* Inner = (const FAnalysisEngine::FDispatch*)this;
	return (const ANSICHAR*)Inner + Inner->LoggerNameOffset;
}

////////////////////////////////////////////////////////////////////////////////
uint32 IAnalyzer::FEventTypeInfo::GetFieldCount() const
{
	const auto* Inner = (const FAnalysisEngine::FDispatch*)this;
	return Inner->FieldCount;
}

////////////////////////////////////////////////////////////////////////////////
const IAnalyzer::FEventFieldInfo* IAnalyzer::FEventTypeInfo::GetFieldInfo(uint32 Index) const
{
	if (Index >= GetFieldCount())
	{
		return nullptr;
	}

	const auto* Inner = (const FAnalysisEngine::FDispatch*)this;
	return (const IAnalyzer::FEventFieldInfo*)(Inner->Fields + Index);
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
	const uint8*		Ptr;
	uint16				Size;
};

////////////////////////////////////////////////////////////////////////////////
const IAnalyzer::FEventTypeInfo& IAnalyzer::FEventData::GetTypeInfo() const
{
	const auto* Info = (const FAnalysisEngine::FEventDataInfo*)this;
	return (const FEventTypeInfo&)(Info->Dispatch);
}

////////////////////////////////////////////////////////////////////////////////
const void* IAnalyzer::FEventData::GetValueImpl(const ANSICHAR* FieldName, int16& SizeAndType) const
{
	const auto* Info = (const FAnalysisEngine::FEventDataInfo*)this;

	FFnv1aHash Hash;
	Hash.Add(FieldName);
	uint32 NameHash = Hash.Get();

	for (int i = 0, n = Info->Dispatch.FieldCount; i < n; ++i)
	{
		const auto& Field = Info->Dispatch.Fields[i];
		if (Field.Hash == NameHash)
		{
			SizeAndType = Field.SizeAndType;
			return (Info->Ptr + Field.Offset);
		}
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
const uint8* IAnalyzer::FEventData::GetAttachment() const
{
	const auto* Info = (const FAnalysisEngine::FEventDataInfo*)this;
	return Info->Ptr + Info->Dispatch.EventSize;
}

////////////////////////////////////////////////////////////////////////////////
uint32 IAnalyzer::FEventData::GetAttachmentSize() const
{
	const auto* Info = (const FAnalysisEngine::FEventDataInfo*)this;
	return Info->Size - Info->Dispatch.EventSize;
}

////////////////////////////////////////////////////////////////////////////////
const uint8* IAnalyzer::FEventData::GetRawPointer() const
{
	const auto* Info = (const FAnalysisEngine::FEventDataInfo*)this;
	return Info->Ptr;
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

	// Manually add event routing for known events.
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
void FAnalysisEngine::RetireAnalyzer(IAnalyzer* Analyzer)
{
	for (uint32 i = 0, n = Analyzers.Num(); i < n; ++i)
	{
		if (Analyzers[i] != Analyzer)
		{
			continue;
		}

		Analyzer->OnAnalysisEnd();
		Analyzers[i] = nullptr;
		break;
	}
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
void FAnalysisEngine::OnNewTrace(const FOnEventContext& Context)
{
	const FEventData& EventData = Context.EventData;
	SessionContext.Version = EventData.GetValue<uint16>("Version");

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

	// Some internal routes have been established already. In case there's some
	// dispatches that are already connected to these routes we won't sort them
	uint32 FixedRouteCount = Routes.Num();

	FOnAnalysisContext OnAnalysisContext = { { SessionContext }, Builder };
	for (uint16 i = 0, n = Analyzers.Num(); i < n; ++i)
	{
		uint32 RouteCount = Routes.Num();

		Builder.AnalyzerIndex = i;
		IAnalyzer* Analyzer = Analyzers[i];
		Analyzer->OnAnalysisBegin(OnAnalysisContext);

		// If the analyzer didn't add any routes we'll retire it immediately
		if (RouteCount == Routes.Num() && Analyzer != this)
		{
			RetireAnalyzer(Analyzer);
		}
	}

	FixedRouteCount = 0; // Disabled for now until AddRoute([ExplicitHash]) has been removed
	TArrayView<FRoute> RouteSubset(Routes.GetData() + FixedRouteCount, Routes.Num() - FixedRouteCount);
	Algo::SortBy(RouteSubset, [] (const FRoute& Route) { return Route.Hash; });

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
template <typename ImplType>
void FAnalysisEngine::ForEachRoute(const FDispatch* Dispatch, ImplType&& Impl)
{
	uint32 RouteCount = Routes.Num();
	if (Dispatch->FirstRoute < RouteCount)
	{
		const FRoute* Route = Routes.GetData() + Dispatch->FirstRoute;
		for (uint32 n = Route->Count; n--; ++Route)
		{
			IAnalyzer* Analyzer = Analyzers[Route->AnalyzerIndex];
			if (Analyzer != nullptr)
			{
				Impl(Analyzer, Route->Id);
			}
		}
	}

	const FRoute* Route = Routes.GetData() + 1;
	if (RouteCount > 1 && Route->Hash == RouteHash_AllEvents)
	{
		for (uint32 n = Route->Count; n--; ++Route)
		{
			IAnalyzer* Analyzer = Analyzers[Route->AnalyzerIndex];
			if (Analyzer != nullptr)
			{
				Impl(Analyzer, Route->Id);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnNewEventInternal(const FOnEventContext& Context)
{
	const FEventDataInfo& EventData = (const FEventDataInfo&)(Context.EventData);

	FDispatchBuilder Builder;
	switch (ProtocolVersion)
	{
	case Protocol0::EProtocol::Id: OnNewEventProtocol0(Builder, EventData.Ptr); break;
	case Protocol1::EProtocol::Id: OnNewEventProtocol1(Builder, EventData.Ptr); break;
	}

	// Get the dispatch and add it into the dispatch table. Fail gently if there
	// is the dispatch table unexpetedly already has an entry.
	FDispatch* Dispatch = Builder.Finalize();
	if (!AddDispatch(Dispatch))
	{
		return;
	}

	// Inform routes that a new event has been declared.
	ForEachRoute(Dispatch, [&] (IAnalyzer* Analyzer, uint16 RouteId)
	{
		if (!Analyzer->OnNewEvent(RouteId, *(FEventTypeInfo*)Dispatch))
		{
			RetireAnalyzer(Analyzer);
		}
	});
}

////////////////////////////////////////////////////////////////////////////////
bool FAnalysisEngine::AddDispatch(FDispatch* Dispatch)
{
	// Add the dispatch to the dispatch table, failing gently if the table slot
	// is already occupied.
	uint16 Uid = Dispatch->Uid;
	if (Uid < Dispatches.Num())
 	{
		if (Dispatches[Uid] != nullptr)
 		{
			FMemory::Free(Dispatch);
			return false;
 		}
 	}
	else
 	{
		Dispatches.SetNum(Uid + 1);
 	}

	// Find routes that have subscribed to this event.
	for (uint16 i = 0, n = Routes.Num(); i < n; ++i)
	{
		if (Routes[i].Hash == Dispatch->Hash)
		{
			Dispatch->FirstRoute = i;
			break;
		}
	}

	Dispatches[Uid] = Dispatch;
	return true;
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnNewEventProtocol0(FDispatchBuilder& Builder, const void* EventData)
{
	const auto& NewEvent = *(Protocol0::FNewEventEvent*)(EventData);

	const auto* NameCursor = (const ANSICHAR*)(NewEvent.Fields + NewEvent.FieldCount);

	Builder.SetLoggerName(NameCursor, NewEvent.LoggerNameSize);
	NameCursor += NewEvent.LoggerNameSize;

	Builder.SetEventName(NameCursor, NewEvent.EventNameSize);
	NameCursor += NewEvent.EventNameSize;
	Builder.SetUid(NewEvent.EventUid);

	// Fill out the fields
	for (int i = 0, n = NewEvent.FieldCount; i < n; ++i)
	{
		const auto& Field = NewEvent.Fields[i];

		uint16 TypeSize = 1 << (Field.TypeInfo & Protocol0::Field_Pow2SizeMask);

		auto TypeId = FEventFieldInfo::EType::Integer;
		if ((Field.TypeInfo & Protocol0::Field_CategoryMask) == Protocol0::Field_Float)
		{
			TypeId = FEventFieldInfo::EType::Float;
		}

		Builder.AddField(NameCursor, Field.NameSize, Field.Offset, Field.Size, TypeId, TypeSize);

		NameCursor += Field.NameSize;
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnNewEventProtocol1(FDispatchBuilder& Builder, const void* EventData)
{
	return OnNewEventProtocol0(Builder, EventData);
}

////////////////////////////////////////////////////////////////////////////////
bool FAnalysisEngine::EstablishTransport(FStreamReader& Reader)
{
	const struct {
		uint8 TransportVersion;
		uint8 ProtocolVersion;
	}* Header = decltype(Header)(Reader.GetPointer(sizeof(*Header)));
	if (Header == nullptr)
	{
		return false;
	}

	// Check for the magic uint32. Early traces did not include this as it was
	// used to validate a inbound socket connection and then discarded.
	if (Header->TransportVersion == 'E' || Header->TransportVersion == 'T')
	{
		const uint32* Magic = (const uint32*)(Reader.GetPointer(sizeof(*Magic)));
		if (*Magic == 'ECRT')
		{
			// Source is big-endian which we don't currently support
			return false;
		}

		if (*Magic == 'TRCE')
		{
			Reader.Advance(sizeof(*Magic));
			return EstablishTransport(Reader);
		}

		return false;
	}

	switch (Header->TransportVersion)
	{
	case ETransport::Raw:		Transport = new FTransport(); break;
	case ETransport::Packet:	Transport = new FPacketTransport(); break;
	case ETransport::TidPacket:	Transport = new FTidPacketTransport(); break;
	default:					return false;
	//case 'E':	/* See the magic above */ break;
	//case 'T':	/* See the magic above */ break;
	}

	ProtocolVersion = Header->ProtocolVersion;
	switch (ProtocolVersion)
	{
	case Protocol0::EProtocol::Id:
		ProtocolHandler = &FAnalysisEngine::OnDataProtocol0;
		{
			FDispatchBuilder Builder;
			Builder.SetUid(uint16(Protocol0::FNewEventEvent::Uid));
			AddDispatch(Builder.Finalize());
		}
		break;

	case Protocol1::EProtocol::Id:
		ProtocolHandler = &FAnalysisEngine::OnDataProtocol1;
		{
			FDispatchBuilder Builder;
			Builder.SetUid(uint16(Protocol0::FNewEventEvent::Uid));
			AddDispatch(Builder.Finalize());
		}
		break;

	default:
		return false;
	}

	Reader.Advance(sizeof(*Header));
	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool FAnalysisEngine::OnData(FStreamReader& Reader)
{
	if (Transport == nullptr)
	{
		// Ensure we've a reasonable amount of data to establish the transport with
		if (Reader.GetPointer(32) == nullptr)
		{
			return true;
		}

		if (!EstablishTransport(Reader))
		{
			return false;
		}
	}

	Transport->SetReader(Reader);
	bool bRet = (this->*ProtocolHandler)();

	// If there's no analyzers left we might as well not continue
	int32 ActiveAnalyzerCount = 0;
	for (IAnalyzer* Analyzer : Analyzers)
	{
		if ((Analyzer != nullptr) && (Analyzer != this))
		{
			ActiveAnalyzerCount++;
		}
	}

	return (ActiveAnalyzerCount > 0) & bRet;
}

////////////////////////////////////////////////////////////////////////////////
bool FAnalysisEngine::OnDataProtocol0()
{
	while (true)
	{
		const auto* Header = Transport->GetPointer<Protocol0::FEventHeader>();
		if (Header == nullptr)
		{
			break;
		}

		uint32 BlockSize = Header->Size + sizeof(Protocol0::FEventHeader);
		Header = Transport->GetPointer<Protocol0::FEventHeader>(BlockSize);
		if (Header == nullptr)
		{
			break;
		}

		uint16 Uid = uint16(Header->Uid & ((1 << 14) - 1));
		if (Uid >= Dispatches.Num())
		{
			return false;
		}

		const FDispatch* Dispatch = Dispatches[Uid];
		if (Dispatch == nullptr)
		{
			return false;
		}

		FEventDataInfo EventDataInfo = { *Dispatch, Header->EventData, Header->Size };
		const FEventData& EventData = (FEventData&)EventDataInfo;

		ForEachRoute(Dispatch, [&] (IAnalyzer* Analyzer, uint16 RouteId)
		{
			if (!Analyzer->OnEvent(RouteId, { SessionContext, EventData }))
			{
				RetireAnalyzer(Analyzer);
			}
		});

		Transport->Advance(BlockSize);
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool FAnalysisEngine::OnDataProtocol1()
{
	auto* InnerTransport = (FTidPacketTransport*)Transport;
	InnerTransport->Update();

	int32 EventCount;
	do
	{
		EventCount = 0;
		FTidPacketTransport::ThreadIter Iter = InnerTransport->ReadThreads();
		while (FStreamReader* Reader = InnerTransport->GetNextThread(Iter))
		{
			int32 ThreadEventCount = OnDataProtocol1(*Reader);
			if (ThreadEventCount < 0)
			{
				return false;
			}

			EventCount += ThreadEventCount;
		}
	}
	while (EventCount);

	return true;
}

////////////////////////////////////////////////////////////////////////////////
int32 FAnalysisEngine::OnDataProtocol1(FStreamReader& Reader)
{
	int32 EventCount = 0;
	while (!Reader.IsEmpty())
	{
		const auto* Header = Reader.GetPointer<Protocol1::FEventHeader>();
		if (Header == nullptr)
		{
			break;
		}

		// Make sure we consume events in the correct order
		if (Header->Serial != uint16(NextLogSerial))
		{
			break;
		}

		uint32 BlockSize = Header->Size + sizeof(Protocol1::FEventHeader);
		if (Reader.GetPointer(BlockSize) == nullptr)
		{
			break;
		}

		uint16 Uid = uint16(Header->Uid & 0x3fff);
		if (Uid >= Dispatches.Num())
		{
			return -1;
		}

		const FDispatch* Dispatch = Dispatches[Uid];
		if (Dispatch == nullptr)
		{
			return -1;
		}

		++NextLogSerial;

		FEventDataInfo EventDataInfo = { *Dispatch, Header->EventData, Header->Size };
		const FEventData& EventData = (FEventData&)EventDataInfo;

		ForEachRoute(Dispatch, [&] (IAnalyzer* Analyzer, uint16 RouteId)
		{
			if (!Analyzer->OnEvent(RouteId, { SessionContext, EventData }))
			{
				RetireAnalyzer(Analyzer);
			}
		});

		Reader.Advance(BlockSize);
		++EventCount;
	}

	return EventCount;
}

} // namespace Trace
