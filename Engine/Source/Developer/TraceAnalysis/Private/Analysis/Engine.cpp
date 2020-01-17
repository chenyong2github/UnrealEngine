// Copyright Epic Games, Inc. All Rights Reserved.

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
struct FAuxData
{
	const uint8*	Data;
	uint32			DataSize;
	uint16			FieldIndex;
	int16			FieldSizeAndType;
};

////////////////////////////////////////////////////////////////////////////////
struct FAnalysisEngine::FAuxDataCollector
	: public TArray<FAuxData>
{
};



////////////////////////////////////////////////////////////////////////////////
struct FAnalysisEngine::FDispatch
{
	enum
	{
		Flag_Important		= 1 << 0,
		Flag_MaybeHasAux	= 1 << 1,
	};

	struct FField
	{
		uint32		Hash;
		uint16		Offset;
		uint16		Size;
		uint16		NameOffset;			// From FField ptr
		int16		SizeAndType;		// value == byte_size, sign == float < 0 < int
		bool		bIsArray;
	};

	uint32			GetFieldIndex(const ANSICHAR* Name) const;
	uint32			Hash				= 0;
	uint16			Uid					= 0;
	uint16			FirstRoute			= ~uint16(0);
	uint8			FieldCount			= 0;
	uint8			Flags				= 0;
	uint16			EventSize			= 0;
	uint16			LoggerNameOffset	= 0;	// From FDispatch ptr
	uint16			EventNameOffset		= 0;	// From FDispatch ptr
	FField			Fields[];
};

////////////////////////////////////////////////////////////////////////////////
uint32 FAnalysisEngine::FDispatch::GetFieldIndex(const ANSICHAR* Name) const
{
	FFnv1aHash NameHash;
	NameHash.Add(Name);

	for (int i = 0, n = FieldCount; i < n; ++i)
	{
		if (Fields[i].Hash == NameHash.Get())
		{
			return i;
		}
	}

	return FieldCount;
}



////////////////////////////////////////////////////////////////////////////////
class FAnalysisEngine::FDispatchBuilder
{
public:
						FDispatchBuilder();
	void				SetUid(uint16 Uid);
	void				SetLoggerName(const ANSICHAR* Name, int32 NameSize=-1);
	void				SetEventName(const ANSICHAR* Name, int32 NameSize=-1);
	void				SetImportant();
	void				SetMaybeHasAux();
	FDispatch::FField&	AddField(const ANSICHAR* Name, int32 NameSize, uint16 Size);
	FDispatch*			Finalize();

private:
	uint32				AppendName(const ANSICHAR* Name, int32 NameSize);
	TArray<uint8>		Buffer;
	TArray<uint8>		NameBuffer;
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
void FAnalysisEngine::FDispatchBuilder::SetImportant()
{
	auto* Dispatch = (FDispatch*)(Buffer.GetData());
	Dispatch->Flags |= FDispatch::Flag_Important;
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::FDispatchBuilder::SetMaybeHasAux()
{
	auto* Dispatch = (FDispatch*)(Buffer.GetData());
	Dispatch->Flags |= FDispatch::Flag_MaybeHasAux;
}

////////////////////////////////////////////////////////////////////////////////
FAnalysisEngine::FDispatch::FField& FAnalysisEngine::FDispatchBuilder::AddField(const ANSICHAR* Name, int32 NameSize, uint16 Size)
{
	int32 Bufoff = Buffer.AddUninitialized(sizeof(FDispatch::FField));
	auto* Field = (FDispatch::FField*)(Buffer.GetData() + Bufoff);
	Field->NameOffset = AppendName(Name, NameSize);
	Field->Size = Size;

	FFnv1aHash Hash;
	Hash.Add((const uint8*)Name, NameSize);
	Field->Hash = Hash.Get();
	
	auto* Dispatch = (FDispatch*)(Buffer.GetData());
	Dispatch->FieldCount++;
	Dispatch->EventSize += Field->Size;

	return *Field;
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
bool IAnalyzer::FEventFieldInfo::IsArray() const
{
	const auto* Inner = (const FAnalysisEngine::FDispatch::FField*)this;
	return Inner->bIsArray;
}



////////////////////////////////////////////////////////////////////////////////
uint32 IAnalyzer::FArrayReader::Num() const
{
	const auto* Inner = (const FAuxData*)this;
	int32 SizeAndType = Inner->FieldSizeAndType;
	SizeAndType = (SizeAndType < 0) ? -SizeAndType : SizeAndType;
	return (SizeAndType == 0) ? SizeAndType : (Inner->DataSize / SizeAndType);
}

////////////////////////////////////////////////////////////////////////////////
const void* IAnalyzer::FArrayReader::GetImpl(uint32 Index, int16& SizeAndType) const
{
	const auto* Inner = (const FAuxData*)this;
	SizeAndType = Inner->FieldSizeAndType;
	uint32 Count = Num();
	if (Index >= Count)
	{
		return nullptr;
	}

	SizeAndType = (SizeAndType < 0) ? -SizeAndType : SizeAndType;
	return Inner->Data + (Index * SizeAndType);
}



////////////////////////////////////////////////////////////////////////////////
struct FAnalysisEngine::FEventDataInfo
{
	const FDispatch&	Dispatch;
	FAuxDataCollector*	AuxCollector;
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
const IAnalyzer::FArrayReader* IAnalyzer::FEventData::GetArrayImpl(const ANSICHAR* FieldName) const
{
	static const FAuxData EmptyAuxData = {};

	const auto* Info = (const FAnalysisEngine::FEventDataInfo*)this;
	if (Info->AuxCollector == nullptr)
	{
		return (IAnalyzer::FArrayReader*)&EmptyAuxData;
	}

	uint32 Index = Info->Dispatch.GetFieldIndex(FieldName);
	if (Index >= Info->Dispatch.FieldCount)
	{
		return (IAnalyzer::FArrayReader*)&EmptyAuxData;
	}
	for (FAuxData& Data : *(Info->AuxCollector))
	{
		if (Data.FieldIndex == Index)
		{
			Data.FieldSizeAndType = Info->Dispatch.Fields[Index].SizeAndType;
			return (IAnalyzer::FArrayReader*)&Data;
		}
	}

	return (IAnalyzer::FArrayReader*)&EmptyAuxData;
}

////////////////////////////////////////////////////////////////////////////////
const void* IAnalyzer::FEventData::GetValueImpl(const ANSICHAR* FieldName, int16& SizeAndType) const
{
	const auto* Info = (const FAnalysisEngine::FEventDataInfo*)this;
	const auto& Dispatch = Info->Dispatch;

	uint32 Index = Dispatch.GetFieldIndex(FieldName);
	if (Index >= Dispatch.FieldCount)
	{
		return nullptr;
	}

	const auto& Field = Dispatch.Fields[Index];
	SizeAndType = Field.SizeAndType;
	return (Info->Ptr + Field.Offset);
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
	RouteId_ChannelAnnounce,
	RouteId_ChannelToggle,
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
	AddRoute(SelfIndex, RouteId_ChannelAnnounce, "$Trace", "ChannelAnnounce");
	AddRoute(SelfIndex, RouteId_ChannelToggle, "$Trace", "ChannelToggle");
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
	case RouteId_NewEvent:			OnNewEventInternal(Context);		break;
	case RouteId_NewTrace:			OnNewTrace(Context);				break;
	case RouteId_Timing:			OnTiming(Context);					break;
	case RouteId_ChannelAnnounce:	OnChannelAnnounceInternal(Context);	break;
	case RouteId_ChannelToggle:		OnChannelToggleInternal(Context);	break;
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
	case Protocol0::EProtocol::Id:
		OnNewEventProtocol0(Builder, EventData.Ptr);
		break;

	case Protocol1::EProtocol::Id:
	case Protocol2::EProtocol::Id:
		OnNewEventProtocol1(Builder, EventData.Ptr);
		break;
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
		if (Field.TypeInfo & Protocol0::Field_Float)
		{
			TypeSize = -TypeSize;
		}

		auto& OutField = Builder.AddField(NameCursor, Field.NameSize, Field.Size);
		OutField.Offset = Field.Offset;
		OutField.SizeAndType = TypeSize;
		OutField.bIsArray = (Field.TypeInfo & Protocol0::Field_Array) != 0;

		NameCursor += Field.NameSize;
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnNewEventProtocol1(FDispatchBuilder& Builder, const void* EventData)
{
	OnNewEventProtocol0(Builder, EventData);

	const auto& NewEvent = *(Protocol1::FNewEventEvent*)(EventData);

	if (NewEvent.Flags & int(Protocol1::EEventFlags::Important))
	{
		Builder.SetImportant();
	}

	if (NewEvent.Flags & int(Protocol1::EEventFlags::MaybeHasAux))
	{
		Builder.SetMaybeHasAux();
	}
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
			Builder.SetUid(uint16(Protocol0::EKnownEventUids::NewEvent));
			AddDispatch(Builder.Finalize());
		}
		break;

	case Protocol1::EProtocol::Id:
	case Protocol2::EProtocol::Id:
		ProtocolHandler = &FAnalysisEngine::OnDataProtocol2;
		{
			FDispatchBuilder Builder;
			Builder.SetUid(uint16(Protocol0::EKnownEventUids::NewEvent));
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

		uint16 Uid = uint16(Header->Uid) & uint16(Protocol0::EKnownEventUids::UidMask);
		if (Uid >= Dispatches.Num())
		{
			return false;
		}

		const FDispatch* Dispatch = Dispatches[Uid];
		if (Dispatch == nullptr)
		{
			return false;
		}

		FEventDataInfo EventDataInfo = {
			*Dispatch,
			nullptr,
			Header->EventData,
			Header->Size
		};
		const FEventData& EventData = (FEventData&)EventDataInfo;

		ForEachRoute(Dispatch, [&] (IAnalyzer* Analyzer, uint16 RouteId)
		{
			if (!Analyzer->OnEvent(RouteId, { SessionContext, EventData, ~0u }))
			{
				RetireAnalyzer(Analyzer);
			}
		});

		Transport->Advance(BlockSize);
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool FAnalysisEngine::OnDataProtocol2()
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
			uint32 ThreadId = InnerTransport->GetThreadId(Iter);
			int32 ThreadEventCount = OnDataProtocol2(ThreadId, *Reader);
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
int32 FAnalysisEngine::OnDataProtocol2(uint32 ThreadId, FStreamReader& Reader)
{
	int32 EventCount = 0;
	while (!Reader.IsEmpty())
	{
		auto Mark = Reader.SaveMark();

		const auto* Header = Reader.GetPointer<Protocol2::FEventHeader>();
		if (Header == nullptr)
		{
			break;
		}

		uint32 BlockSize = Header->Size;
		
		switch (ProtocolVersion)
		{
			case Protocol1::EProtocol::Id:
				{
					const auto* HeaderV1 = (Protocol1::FEventHeader*)Header;
					if (HeaderV1->Serial != uint16(NextLogSerial))
					{
						return EventCount;
					}
					BlockSize += sizeof(*HeaderV1);
				}
				break;

			case Protocol2::EProtocol::Id:
				{
					uint32 EventSerial = Header->SerialLow|(uint32(Header->SerialHigh) << 16);
					if (EventSerial != (NextLogSerial & 0x00ffffff))
					{
						return EventCount;
					}
					BlockSize += sizeof(*Header);
				}
				break;
		}

		if (Reader.GetPointer(BlockSize) == nullptr)
		{
			break;
		}

		Reader.Advance(BlockSize);

		uint16 Uid = Header->Uid & uint16(Protocol2::EKnownEventUids::UidMask);
		if (Uid >= Dispatches.Num())
		{
			return -1;
		}

		const FDispatch* Dispatch = Dispatches[Uid];
		if (Dispatch == nullptr)
		{
			return -1;
		}

		FAuxDataCollector AuxCollector;
		if (Dispatch->Flags & FDispatch::Flag_MaybeHasAux)
		{
			int AuxStatus = OnDataProtocol2Aux(Reader, AuxCollector);
			if (AuxStatus == 0)
			{
				Reader.RestoreMark(Mark);
				break;
			}
			else if (AuxStatus < 0)
			{
				return -1;
			}
		}

		++NextLogSerial;

		FEventDataInfo EventDataInfo = {
			*Dispatch,
			&AuxCollector,
			(const uint8*)Header + BlockSize - Header->Size,
			Header->Size,
		};
		const FEventData& EventData = (FEventData&)EventDataInfo;

		ForEachRoute(Dispatch, [&] (IAnalyzer* Analyzer, uint16 RouteId)
		{
			if (!Analyzer->OnEvent(RouteId, { SessionContext, EventData, ThreadId }))
			{
				RetireAnalyzer(Analyzer);
			}
		});

		++EventCount;
	}

	return EventCount;
}

////////////////////////////////////////////////////////////////////////////////
int32 FAnalysisEngine::OnDataProtocol2Aux(FStreamReader& Reader, FAuxDataCollector& Collector)
{
	while (true)
	{
		const uint8* NextByte = Reader.GetPointer<uint8>();
		if (NextByte == nullptr)
		{
			return 0;
		}

		// Is the following sequence a blob of auxilary data or the null
		// terminator byte?
		if (NextByte[0] == 0)
		{
			Reader.Advance(1);
			return 1;
		}

		// Get header and the auxilary blob's size
		const auto* Header = Reader.GetPointer<Protocol1::FAuxHeader>();
		if (Header == nullptr)
		{
			return 0;
		}

		// Check it exists
		uint32 BlockSize = (Header->Size >> 8) + sizeof(*Header);
		if (Reader.GetPointer(BlockSize) == nullptr)
		{
			return 0;
		}

		// Attach to event
		FAuxData AuxData;
		AuxData.Data = Header->Data;
		AuxData.DataSize = uint32(BlockSize - sizeof(*Header));
		AuxData.FieldIndex = uint16(Header->FieldIndex & Protocol1::FAuxHeader::FieldMask);
		Collector.Push(AuxData);

		Reader.Advance(BlockSize);
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnChannelAnnounceInternal(const FOnEventContext& Context)
{
	const ANSICHAR* ChannelName = (ANSICHAR*)Context.EventData.GetAttachment();
	const uint32 ChannelId = Context.EventData.GetValue<uint32>("Id");

	for (IAnalyzer* Analyzer : Analyzers)
	{
		Analyzer->OnChannelAnnounce(ChannelName, ChannelId);
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnChannelToggleInternal(const FOnEventContext& Context)
{
	const uint32 ChannelId = Context.EventData.GetValue<uint32>("Id");
	const bool bEnabled = Context.EventData.GetValue<bool>("IsEnabled");

	for (IAnalyzer* Analyzer : Analyzers)
	{
		Analyzer->OnChannelToggle(ChannelId, bEnabled);
	}
}

} // namespace Trace
