// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Engine.h"
#include "Containers/ArrayView.h"
#include "CoreGlobals.h"
#include "HAL/UnrealMemory.h"
#include "Trace/Analysis.h"
#include "Trace/Analyzer.h"
#if 0
#include "Trace/Private/DataDecoder.h"
#endif // 0
#include "Trace/Private/Event.h"

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
class FTransportReader
{
public:
	void					SetSource(FStreamReader::FData& InSource);
	template <typename RetType>
	RetType const*			GetPointer();
	template <typename RetType>
	RetType const*			GetPointer(uint32 BlockSize);
	virtual void			Advance(uint32 BlockSize);

protected:
	virtual const uint8*	GetPointerImpl(uint32 BlockSize);
	FStreamReader::FData*	Source;
};

////////////////////////////////////////////////////////////////////////////////
void FTransportReader::SetSource(FStreamReader::FData& InSource)
{
	Source = &InSource;
}

////////////////////////////////////////////////////////////////////////////////
template <typename RetType>
RetType const* FTransportReader::GetPointer()
{
	return GetPointer<RetType>(sizeof(RetType));
}

////////////////////////////////////////////////////////////////////////////////
template <typename RetType>
RetType const* FTransportReader::GetPointer(uint32 BlockSize)
{
	return (RetType const*)GetPointerImpl(BlockSize);
}

////////////////////////////////////////////////////////////////////////////////
void FTransportReader::Advance(uint32 BlockSize)
{
	Source->Advance(BlockSize);
}

////////////////////////////////////////////////////////////////////////////////
const uint8* FTransportReader::GetPointerImpl(uint32 BlockSize)
{
	return Source->GetPointer(BlockSize);
}



////////////////////////////////////////////////////////////////////////////////
#if 0
class FLz4TransportReader
	: public FTransportReader
{
	enum : uint32 { MaxChunks = 4 };

public:
							FLz4TransportReader(uint32 InChunkPow2);
	virtual					~FLz4TransportReader();

private:
	virtual void			Advance(uint32 BlockSize) override;
	virtual const uint8*	GetPointerImpl(uint32 BlockSize) override;
	bool					NextChunk();
    FDataDecoder			Decoder;
	uint8*					Buffer;
	const uint8*			Cursor;
	const uint8*			End;
	uint32					Index = 0;
	const uint32			ChunkPow2;
};

////////////////////////////////////////////////////////////////////////////////
FLz4TransportReader::FLz4TransportReader(uint32 InChunkPow2)
: ChunkPow2(InChunkPow2)
{
	uint32 ChunkSize = 1 << ChunkPow2;
	Buffer = new uint8[ChunkSize * (MaxChunks + 1)];
	Cursor = End = Buffer + (ChunkSize * 2);
}

////////////////////////////////////////////////////////////////////////////////
FLz4TransportReader::~FLz4TransportReader()
{
	delete[] Buffer;
}

////////////////////////////////////////////////////////////////////////////////
void FLz4TransportReader::Advance(uint32 BlockSize)
{
	Cursor += BlockSize;
}

////////////////////////////////////////////////////////////////////////////////
const uint8* FLz4TransportReader::GetPointerImpl(uint32 BlockSize)
{
	while (true)
	{
		uint32 Remaining = uint32(UPTRINT(End - Cursor));
		if (Remaining >= BlockSize)
		{
			break;
		}

		if (!NextChunk())
		{
			return nullptr;
		}
	}

	return Cursor;
}

////////////////////////////////////////////////////////////////////////////////
bool FLz4TransportReader::NextChunk()
{
	const struct {
		uint32	Size;
		uint8	Data[];
	}* Block;

	Block = decltype(Block)(Source->GetPointer(sizeof(*Block)));
	if (Block == nullptr)
	{
		return false;
	}

	int32 BlockSize = Block->Size;
	if (BlockSize < 0)
	{
		BlockSize = ~BlockSize;
	}

	Block = decltype(Block)(Source->GetPointer(BlockSize));
	if (Block == nullptr)
	{
		return false;
	}

	Index = (Index + 1) & (MaxChunks - 1);
	if (!Index)
	{
		uint32 Remaining = uint32(UPTRINT(End - Cursor));
		uint32 OverflowChunks = Remaining >> ChunkPow2;
		if (OverflowChunks >= MaxChunks - 1)
		{
			return false;
		}

		char* Dest = (char*)(Buffer + (1ull << ChunkPow2));
		Dest -= Remaining - OverflowChunks;
		memcpy(Dest, Cursor, Remaining);
		Cursor = (uint8*)Dest;
		End = Cursor + Remaining;
	}

	uint32 SrcSize = BlockSize - sizeof(*Block);
	uint32 DestSize = 1 << ChunkPow2;
	int DecodedSize = Decoder.Decode(Block->Data, End, SrcSize, DestSize);
	if (DecodedSize < 0)
	{
		return false;
	}

	End += DecodedSize;

	if (BlockSize != Block->Size)
	{
		Decoder.Reset();
	}

	Source->Advance(BlockSize);
	return true;
}
#endif // 0



////////////////////////////////////////////////////////////////////////////////
struct FAnalysisEngine::FEventDataImpl
	: public FEventData
{
	virtual					~FEventDataImpl() = default;
	virtual const FValue&	GetValue(const ANSICHAR* FieldName) const override;
	virtual const FArray&	GetArray(const ANSICHAR* FieldName) const override;
	virtual const uint8*	GetData() const override;
	virtual const uint8*	GetAttachment() const override;
	virtual uint16			GetAttachmentSize() const override;
	virtual uint16			GetTotalSize() const override;
	const FDispatch*		Dispatch;
	const uint8*			Ptr;
	uint16					Size;
};

////////////////////////////////////////////////////////////////////////////////
const IAnalyzer::FValue& FAnalysisEngine::FEventDataImpl::GetValue(const ANSICHAR* FieldName) const
{
	UPTRINT Ret = ~0ull;

	FFnv1aHash Hash;
	Hash.Add(FieldName);
	uint32 NameHash = Hash.Get();

	for (int i = 0, n = Dispatch->FieldCount; i < n; ++i)
	{
		const auto& Field = Dispatch->Fields[i];
		if (Field.Hash == NameHash)
		{
			Ret = UPTRINT(Ptr + Field.Offset);
			Ret |= UPTRINT(Field.TypeInfo) << 48;
			break;
		}
	}

	return *(FValue*)Ret;
}

////////////////////////////////////////////////////////////////////////////////
const IAnalyzer::FArray& FAnalysisEngine::FEventDataImpl::GetArray(const ANSICHAR* FieldName) const
{
	return *(FArray*)0x493;
}

////////////////////////////////////////////////////////////////////////////////
const uint8* FAnalysisEngine::FEventDataImpl::GetData() const
{
	return Ptr;
}

////////////////////////////////////////////////////////////////////////////////
const uint8* FAnalysisEngine::FEventDataImpl::GetAttachment() const
{
	return Ptr + Dispatch->EventSize;
}

////////////////////////////////////////////////////////////////////////////////
uint16 FAnalysisEngine::FEventDataImpl::GetAttachmentSize() const
{
	return Size - Dispatch->EventSize;
}

////////////////////////////////////////////////////////////////////////////////
uint16 FAnalysisEngine::FEventDataImpl::GetTotalSize() const
{
	return Size;
}



////////////////////////////////////////////////////////////////////////////////
enum ERouteId : uint16
{
	RouteId_NewEvent,
	RouteId_NewTrace,
	RouteId_Timing,
};

////////////////////////////////////////////////////////////////////////////////
FAnalysisEngine::FAnalysisEngine(TArray<IAnalyzer*>&& InAnalyzers)
: Analyzers(MoveTemp(InAnalyzers))
{
	EventDataImpl = new FEventDataImpl();

	uint16 SelfIndex = Analyzers.Num();
	Analyzers.Add(this);

	// Manually add event routing for known events, and those we don't quite know
	// yet but are expecting.
	FDispatch& NewEventDispatch = AddDispatch(uint16(FNewEventEvent::Uid), 0);
	NewEventDispatch.FirstRoute = 0;
	AddRoute(SelfIndex, RouteId_NewEvent, 0);
	AddRoute(SelfIndex, RouteId_NewTrace, "$Trace", "NewTrace");
	AddRoute(SelfIndex, RouteId_Timing, "$Trace", "Timing");
}

////////////////////////////////////////////////////////////////////////////////
FAnalysisEngine::~FAnalysisEngine()
{
	for (IAnalyzer* Analyzer : Analyzers)
	{
		Analyzer->OnAnalysisEnd();
	}

	for (FDispatch* Dispatch : Dispatches)
	{
		FMemory::Free(Dispatch);
	}

	delete EventDataImpl;
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnAnalysisEnd()
{
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	switch (RouteId)
	{
	case RouteId_NewEvent:	return OnNewEvent(Context);
	case RouteId_NewTrace:	return OnNewTrace(Context);
	case RouteId_Timing:	return OnTiming(Context);
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

	uint16 HashIndex = 0;
	for (uint16 n = Hashes.Num(); HashIndex < n; ++HashIndex)
	{
		if (Hashes[HashIndex] == Hash)
		{
			break;
		}
	}

	if (HashIndex == Hashes.Num())
	{
		Hashes.Add(Hash);
	}

	FRoute& Route = Routes.Emplace_GetRef();
	Route.Id = Id;
	Route.HashIndex = HashIndex;
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

	Algo::SortBy(Routes, [] (const FRoute& Route) { return Route.HashIndex; });

	FRoute* Cursor = Routes.GetData();
	Cursor->Count = 1;

	for (uint16 i = 1, n = Routes.Num(); i < n; ++i)
	{
		if (Routes[i].HashIndex == Cursor->HashIndex)
		{
			Cursor->Count++;
		}
		else
		{
			Cursor = Routes.GetData() + i;
			Cursor->Count = 1;
		}
	}

	// Add a terminal route for events that aren't subscribed to
	FRoute& Route = Routes.Emplace_GetRef();
	Route.HashIndex = Hashes.Num();
	Route.Count = 0;
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnTiming(const FOnEventContext& Context)
{
	SessionContext.StartCycle = Context.EventData.GetValue("StartCycle").As<uint64>();
	SessionContext.CycleFrequency = Context.EventData.GetValue("CycleFrequency").As<uint64>();
}

////////////////////////////////////////////////////////////////////////////////
FAnalysisEngine::FDispatch& FAnalysisEngine::AddDispatch(uint16 Uid, uint16 FieldCount)
{
	// Allocate a block of memory to hold the dispatch
	uint32 Size = sizeof(FDispatch) + (sizeof(FDispatch::FField) * FieldCount);
	auto* Dispatch = (FDispatch*)FMemory::Malloc(Size);
	Dispatch->FieldCount = FieldCount;
	Dispatch->EventSize = 0;
	Dispatch->FirstRoute = -1;

	// Add the new dispatch in the dispatch table
	if (Uid >= Dispatches.Num())
	{
		Dispatches.SetNum(Uid + 1);
	}
	check(Dispatches[Uid] == nullptr);
	Dispatches[Uid] = Dispatch;

	return *Dispatch;
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnNewEvent(const FOnEventContext& Context)
{
	const auto& NewEvent = *(FNewEventEvent*)Context.EventData.GetData();

	FDispatch& Dispatch = AddDispatch(NewEvent.EventUid, NewEvent.FieldCount);

	if (NewEvent.FieldCount)
	{
		auto& LastField = NewEvent.Fields[NewEvent.FieldCount - 1];
		Dispatch.EventSize = LastField.Offset + LastField.Size;
	}

	const uint8* NameCursor = (uint8*)(NewEvent.Fields + NewEvent.FieldCount);

	// Calculate this dispatch's hash.
	FFnv1aHash DispatchHash;
	NameCursor = DispatchHash.Add(NameCursor, NewEvent.LoggerNameSize);
	NameCursor = DispatchHash.Add(NameCursor, NewEvent.EventNameSize);

	// Fill out the fields
	for (int i = 0, n = NewEvent.FieldCount; i < n; ++i)
	{
		const auto& In = NewEvent.Fields[i];
		auto& Out = Dispatch.Fields[i];

		FFnv1aHash FieldHash;
		NameCursor = FieldHash.Add(NameCursor, In.NameSize);

		Out.Hash = FieldHash.Get();
		Out.Offset = In.Offset;
		Out.Size = In.Size;
		Out.TypeInfo = In.TypeInfo;
	}

	TArrayView<FDispatch::FField> Fields(Dispatch.Fields, Dispatch.FieldCount);
	Algo::SortBy(Fields, [] (const auto& Field) { return Field.Hash; });

	// Find routes that have subscribed to this event.
	uint16 HashIndex = 0;
	for (uint16 n = Hashes.Num(); HashIndex < n; ++HashIndex)
	{
		if (Hashes[HashIndex] == DispatchHash.Get())
		{
			break;
		}
	}

	uint16 RouteIndex = 0;
	for (uint16 n = Routes.Num(); RouteIndex < n; ++RouteIndex)
	{
		if (Routes[RouteIndex].HashIndex == HashIndex)
		{
			break;
		}
	}

	check(RouteIndex < Routes.Num());
	Dispatch.FirstRoute = RouteIndex;
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

	Data.Advance(sizeof(*Header));

	switch (Header->Format)
	{
	case 1:		Transport = new FTransportReader();						break;
#if 0
	case 4:		Transport = new FLz4TransportReader(Header->Parameter); break;
#endif // 0
	default:	return false;
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool FAnalysisEngine::OnData(FStreamReader::FData& Data)
{
	if (Transport == nullptr)
	{
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

		EventDataImpl->Dispatch = Dispatch;
		EventDataImpl->Ptr = Header->EventData;
		EventDataImpl->Size = Header->Size;

		const FRoute* Route = Routes.GetData() + Dispatch->FirstRoute;
		for (uint32 n = Route->Count; n--; ++Route)
		{
			IAnalyzer* Analyzer = Analyzers[Route->AnalyzerIndex];
			Analyzer->OnEvent(Route->Id, { SessionContext, *EventDataImpl });
		}
	}

	return true;
}

} // namespace Trace
