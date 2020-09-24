// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine.h"
#include "Algo/BinarySearch.h"
#include "Algo/Sort.h"
#include "Containers/ArrayView.h"
#include "CoreGlobals.h"
#include "HAL/UnrealMemory.h"
#include "StreamReader.h"
#include "Templates/UnrealTemplate.h"
#include "Trace/Analysis.h"
#include "Trace/Analyzer.h"
#include "Trace/Detail/Protocol.h"
#include "Transport/PacketTransport.h"
#include "Transport/Transport.h"
#include "Transport/TidPacketTransport.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
void SerializeToCborImpl(TArray<uint8>&, const IAnalyzer::FEventData&, uint32);

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
FThreads::FThreads()
{
	Infos.Reserve(64);
}

////////////////////////////////////////////////////////////////////////////////
FThreads::FInfo* FThreads::GetInfo()
{
	if (LastGetInfoId >= uint32(Infos.Num()))
	{
		return nullptr;
	}

	return Infos.GetData() + LastGetInfoId;
}

////////////////////////////////////////////////////////////////////////////////
FThreads::FInfo& FThreads::GetInfo(uint32 ThreadId)
{
	LastGetInfoId = ThreadId;

	if (ThreadId >= uint32(Infos.Num()))
	{
		Infos.SetNum(ThreadId + 1);
	}

	FInfo& Info = Infos[ThreadId];
	if (Info.ThreadId == ~0u)
	{
		Info.ThreadId = ThreadId;
	}
	return Info;
}

////////////////////////////////////////////////////////////////////////////////
void FThreads::SetGroupName(const ANSICHAR* InGroupName, uint32 Length)
{
	if (InGroupName == nullptr || *InGroupName == '\0')
	{
		GroupName.SetNum(0);
		return;
	}

	GroupName.SetNumUninitialized(Length + 1);
	GroupName[Length] = '\0';
	FMemory::Memcpy(GroupName.GetData(), InGroupName, Length);
}

////////////////////////////////////////////////////////////////////////////////
const TArray<uint8>* FThreads::GetGroupName() const
{
	return (GroupName.Num() > 0) ? &GroupName : nullptr;
}




////////////////////////////////////////////////////////////////////////////////
uint32 IAnalyzer::FThreadInfo::GetId() const
{
	const auto* Info = (const FThreads::FInfo*)this;
	return Info->ThreadId;
}

////////////////////////////////////////////////////////////////////////////////
uint32 IAnalyzer::FThreadInfo::GetSystemId() const
{
	const auto* Info = (const FThreads::FInfo*)this;
	return Info->SystemId;
}

////////////////////////////////////////////////////////////////////////////////
int32 IAnalyzer::FThreadInfo::GetSortHint() const
{
	const auto* Info = (const FThreads::FInfo*)this;
	return Info->SortHint;
}

////////////////////////////////////////////////////////////////////////////////
const ANSICHAR* IAnalyzer::FThreadInfo::GetName() const
{
	const auto* Info = (const FThreads::FInfo*)this;
	if (Info->Name.Num() <= 0)
	{
		return nullptr;
	}

	return (const ANSICHAR*)(Info->Name.GetData());
}

////////////////////////////////////////////////////////////////////////////////
const ANSICHAR* IAnalyzer::FThreadInfo::GetGroupName() const
{
	const auto* Info = (const FThreads::FInfo*)this;
	if (Info->GroupName.Num() <= 0)
	{
		return "";
	}

	return (const ANSICHAR*)(Info->GroupName.GetData());
}



////////////////////////////////////////////////////////////////////////////////
uint64 IAnalyzer::FEventTime::GetTimestamp() const
{
	const auto* Timing = (const FTiming*)this;
	return Timing->EventTimestamp;
}

////////////////////////////////////////////////////////////////////////////////
double IAnalyzer::FEventTime::AsSeconds() const
{
	const auto* Timing = (const FTiming*)this;
	return double(Timing->EventTimestamp) * Timing->InvTimestampHz;
}

////////////////////////////////////////////////////////////////////////////////
uint64 IAnalyzer::FEventTime::AsCycle64() const
{
	const auto* Timing = (const FTiming*)this;
	return Timing->BaseTimestamp + Timing->EventTimestamp;
}

////////////////////////////////////////////////////////////////////////////////
double IAnalyzer::FEventTime::AsSeconds(uint64 Cycles64) const
{
	const auto* Timing = (const FTiming*)this;
	return double(int64(Cycles64) - int64(Timing->BaseTimestamp)) * Timing->InvTimestampHz;
}

////////////////////////////////////////////////////////////////////////////////
double IAnalyzer::FEventTime::AsSecondsAbsolute(int64 DurationCycles64) const
{
	const auto* Timing = (const FTiming*)this;
	return double(DurationCycles64) * Timing->InvTimestampHz;
}



////////////////////////////////////////////////////////////////////////////////
struct FAnalysisEngine::FDispatch
{
	enum
	{
		Flag_Important		= 1 << 0,
		Flag_MaybeHasAux	= 1 << 1,
		Flag_NoSync			= 1 << 2,
	};

	struct FField
	{
		uint32		Hash;
		uint16		Offset;
		uint16		Size;
		uint16		NameOffset;			// From FField ptr
		int8		SizeAndType;		// value == byte_size, sign == float < 0 < int
		uint8		Class : 7;
		uint8		bArray : 1;
	};

	int32			GetFieldIndex(const ANSICHAR* Name) const;
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
int32 FAnalysisEngine::FDispatch::GetFieldIndex(const ANSICHAR* Name) const
{
	FFnv1aHash NameHash;
	NameHash.Add(Name);

	for (int32 i = 0, n = FieldCount; i < n; ++i)
	{
		if (Fields[i].Hash == NameHash.Get())
		{
			return i;
		}
	}

	return -1;
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
	void				SetNoSync();
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
void FAnalysisEngine::FDispatchBuilder::SetNoSync()
{
	auto* Dispatch = (FDispatch*)(Buffer.GetData());
	Dispatch->Flags |= FDispatch::Flag_NoSync;
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
IAnalyzer::FEventFieldInfo::EType IAnalyzer::FEventFieldInfo::GetType() const
{
	const auto* Inner = (const FAnalysisEngine::FDispatch::FField*)this;

	if (Inner->Class == Protocol0::Field_String)
	{
		return (Inner->SizeAndType == 1) ? EType::AnsiString : EType::WideString;
	}

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
	return Inner->bArray;
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
	const FAuxData*		GetAuxData(uint32 FieldIndex) const;
	const FDispatch&	Dispatch;
	FAuxDataCollector*	AuxCollector;
	const uint8*		Ptr;
	uint16				Size;
};

////////////////////////////////////////////////////////////////////////////////
const FAuxData* FAnalysisEngine::FEventDataInfo::GetAuxData(uint32 FieldIndex) const
{
	const auto* Info = (const FAnalysisEngine::FEventDataInfo*)this;
	if (Info->AuxCollector == nullptr)
	{
		return nullptr;
	}

	for (FAuxData& Data : *(Info->AuxCollector))
	{
		if (Data.FieldIndex == FieldIndex)
		{
			Data.FieldSizeAndType = Info->Dispatch.Fields[FieldIndex].SizeAndType;
			return &Data;
		}
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
const IAnalyzer::FEventTypeInfo& IAnalyzer::FEventData::GetTypeInfo() const
{
	const auto* Info = (const FAnalysisEngine::FEventDataInfo*)this;
	return (const FEventTypeInfo&)(Info->Dispatch);
}

////////////////////////////////////////////////////////////////////////////////
const IAnalyzer::FArrayReader* IAnalyzer::FEventData::GetArrayImpl(const ANSICHAR* FieldName) const
{
	const auto* Info = (const FAnalysisEngine::FEventDataInfo*)this;

	int32 Index = Info->Dispatch.GetFieldIndex(FieldName);
	if (Index >= 0)
	{
		if (const FAuxData* Data = Info->GetAuxData(Index))
		{
			return (IAnalyzer::FArrayReader*)Data;
		}
	}

	static const FAuxData EmptyAuxData = {};
	return (const IAnalyzer::FArrayReader*)&EmptyAuxData;
}

////////////////////////////////////////////////////////////////////////////////
const void* IAnalyzer::FEventData::GetValueImpl(const ANSICHAR* FieldName, int16& SizeAndType) const
{
	const auto* Info = (const FAnalysisEngine::FEventDataInfo*)this;
	const auto& Dispatch = Info->Dispatch;

	int32 Index = Dispatch.GetFieldIndex(FieldName);
	if (Index < 0)
	{
		return nullptr;
	}

	const auto& Field = Dispatch.Fields[Index];
	SizeAndType = Field.SizeAndType;
	return (Info->Ptr + Field.Offset);
}

////////////////////////////////////////////////////////////////////////////////
bool IAnalyzer::FEventData::GetString(const ANSICHAR* FieldName, FAnsiStringView& Out) const
{
	const auto* Info = (const FAnalysisEngine::FEventDataInfo*)this;
	const auto& Dispatch = Info->Dispatch;

	int32 Index = Dispatch.GetFieldIndex(FieldName);
	if (Index < 0)
	{
		return false;
	}

	const auto& Field = Dispatch.Fields[Index];
	if (Field.Class != Protocol0::Field_String || Field.SizeAndType != sizeof(ANSICHAR))
	{
		return false;
	}

	if (const FAuxData* Data = Info->GetAuxData(Index))
	{
		Out = FAnsiStringView((const ANSICHAR*)(Data->Data), Data->DataSize);
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////
bool IAnalyzer::FEventData::GetString(const ANSICHAR* FieldName, FStringView& Out) const
{
	const auto* Info = (const FAnalysisEngine::FEventDataInfo*)this;
	const auto& Dispatch = Info->Dispatch;

	int32 Index = Dispatch.GetFieldIndex(FieldName);
	if (Index < 0)
	{
		return false;
	}

	const auto& Field = Dispatch.Fields[Index];
	if (Field.Class != Protocol0::Field_String || Field.SizeAndType != sizeof(TCHAR))
	{
		return false;
	}

	if (const FAuxData* Data = Info->GetAuxData(Index))
	{
		Out = FStringView((const TCHAR*)(Data->Data), Data->DataSize / sizeof(TCHAR));
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////
bool IAnalyzer::FEventData::GetString(const ANSICHAR* FieldName, FString& Out) const
{
	const auto* Info = (const FAnalysisEngine::FEventDataInfo*)this;
	const auto& Dispatch = Info->Dispatch;

	int32 Index = Dispatch.GetFieldIndex(FieldName);
	if (Index < 0)
	{
		return false;
	}

	const FAuxData* Data = Info->GetAuxData(Index);
	if (Data == nullptr)
	{
		return false;
	}

	const auto& Field = Dispatch.Fields[Index];
	if (Field.Class == Protocol0::Field_String)
	{
		if (Field.SizeAndType == sizeof(ANSICHAR))
		{
			Out = FString(Data->DataSize, (const ANSICHAR*)(Data->Data));
			return true;
		}

		if (Field.SizeAndType == sizeof(TCHAR))
		{
			Out = FStringView((const TCHAR*)(Data->Data), Data->DataSize / sizeof(TCHAR));
			return true;
		}
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////
void IAnalyzer::FEventData::SerializeToCbor(TArray<uint8>& Out) const
{
	const auto* Info = (const FAnalysisEngine::FEventDataInfo*)this;
	uint32 Size = Info->Size;
	for (FAuxData& Data : *(Info->AuxCollector))
	{
		Size += Data.DataSize;
	}
	SerializeToCborImpl(Out, *this, Size);
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
enum ERouteId : uint16
{
	RouteId_NewEvent,
	RouteId_NewTrace,
	RouteId_Timing,
	RouteId_ThreadTiming,
	RouteId_ThreadInfo,
	RouteId_ThreadGroupBegin,
	RouteId_ThreadGroupEnd,
};



////////////////////////////////////////////////////////////////////////////////
FAnalysisEngine::FAnalysisEngine(TArray<IAnalyzer*>&& InAnalyzers)
: Analyzers(MoveTemp(InAnalyzers))
{
	uint16 SelfIndex = Analyzers.Num();
	Analyzers.Add(this);
}

////////////////////////////////////////////////////////////////////////////////
FAnalysisEngine::~FAnalysisEngine()
{
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::Begin()
{
	// Call out to all registered analyzers to have them register event interest
	struct : IAnalyzer::FInterfaceBuilder
	{
		virtual void RouteEvent(uint16 RouteId, const ANSICHAR* Logger, const ANSICHAR* Event, bool bScoped) override
		{
			Self->AddRoute(AnalyzerIndex, RouteId, Logger, Event, bScoped);
		}

		virtual void RouteLoggerEvents(uint16 RouteId, const ANSICHAR* Logger, bool bScoped) override
		{
			Self->AddRoute(AnalyzerIndex, RouteId, Logger, "", bScoped);
		}

		virtual void RouteAllEvents(uint16 RouteId, bool bScoped) override
		{
			Self->AddRoute(AnalyzerIndex, RouteId, "", "", bScoped);
		}

		FAnalysisEngine* Self;
		uint16 AnalyzerIndex;
	} Builder;
	Builder.Self = this;

	FOnAnalysisContext OnAnalysisContext = { Builder };
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

	auto RouteProjection = [] (const FRoute& Route) { return Route.Hash; };

	Algo::SortBy(Routes, RouteProjection);

	auto FindRoute = [this, &RouteProjection] (uint32 Hash)
	{
		int32 Index = Algo::LowerBoundBy(Routes, Hash, RouteProjection);
		return (Index < Routes.Num() && Routes[Index].Hash == Hash) ? Index : -1;
	};

	int32 AllEventsIndex = FindRoute(FFnv1aHash().Get());
	auto FixupRoute = [this, &FindRoute, AllEventsIndex] (FRoute* Route) 
	{
		if (Route->ParentHash)
		{
			int32 ParentIndex = FindRoute(Route->ParentHash);
			Route->Parent = int16((ParentIndex < 0) ? AllEventsIndex : ParentIndex);
		}
		else
		{
			Route->Parent = -1;
		}
		Route->Count = 1;
		return Route;
	};

	FRoute* Cursor = FixupRoute(Routes.GetData());
	for (uint16 i = 1, n = Routes.Num(); i < n; ++i)
	{
		FRoute* Route = Routes.GetData() + i;
		if (Route->Hash == Cursor->Hash)
		{
			Cursor->Count++;
		}
		else
		{
			Cursor = FixupRoute(Route);
		}
	}

	switch (ProtocolVersion)
	{
	case Protocol0::EProtocol::Id:
	case Protocol1::EProtocol::Id:
	case Protocol2::EProtocol::Id:
		{
			FDispatchBuilder Dispatch;
			Dispatch.SetUid(uint16(Protocol0::EKnownEventUids::NewEvent));
			Dispatch.SetLoggerName("$Trace");
			Dispatch.SetEventName("NewEvent");
			AddDispatch(Dispatch.Finalize());
		}
		break;

	case Protocol3::EProtocol::Id:
		{
			FDispatchBuilder Dispatch;
			Dispatch.SetUid(uint16(Protocol3::EKnownEventUids::NewEvent));
			Dispatch.SetLoggerName("$Trace");
			Dispatch.SetEventName("NewEvent");
			Dispatch.SetNoSync();
			AddDispatch(Dispatch.Finalize());
		}
		break;
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::End()
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
	const ANSICHAR* Event,
	bool bScoped)
{
	check(AnalyzerIndex < Analyzers.Num());

	uint32 ParentHash = 0;
	if (Logger[0] && Event[0])
	{
		FFnv1aHash Hash;
		Hash.Add(Logger);
		ParentHash = Hash.Get();
	}

	FFnv1aHash Hash;
	Hash.Add(Logger);
	Hash.Add(Event);

	FRoute& Route = Routes.Emplace_GetRef();
	Route.Id = Id;
	Route.Hash = Hash.Get();
	Route.ParentHash = ParentHash;
	Route.AnalyzerIndex = AnalyzerIndex;
	Route.bScoped = (bScoped == true);
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;
	Builder.RouteEvent(RouteId_NewTrace,		"$Trace", "NewTrace");
	Builder.RouteEvent(RouteId_NewEvent,		"$Trace", "NewEvent");
	Builder.RouteEvent(RouteId_Timing,			"$Trace", "Timing");
	Builder.RouteEvent(RouteId_ThreadTiming,	"$Trace", "ThreadTiming");
	Builder.RouteEvent(RouteId_ThreadInfo,		"$Trace", "ThreadInfo");
	Builder.RouteEvent(RouteId_ThreadGroupBegin,"$Trace", "ThreadGroupBegin");
	Builder.RouteEvent(RouteId_ThreadGroupEnd,	"$Trace", "ThreadGroupEnd");
}

////////////////////////////////////////////////////////////////////////////////
bool FAnalysisEngine::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	if (RouteId == RouteId_NewEvent)
	{
		const FEventDataInfo& EventData = (const FEventDataInfo&)(Context.EventData);
		OnNewEventInternal(EventData.Ptr);
		return true;
	}

	switch (RouteId)
	{
	case RouteId_NewTrace:			OnNewTrace(Context);			break;
	case RouteId_Timing:			OnTiming(Context);				break;
	case RouteId_ThreadTiming:		OnThreadTiming(Context);		break;
	case RouteId_ThreadInfo:		OnThreadInfoInternal(Context);	break;
	case RouteId_ThreadGroupBegin:	OnThreadGroupBegin(Context);	break;
	case RouteId_ThreadGroupEnd:	OnThreadGroupEnd();				break;
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnNewTrace(const FOnEventContext& Context)
{
	const FEventData& EventData = Context.EventData;

	// "Serial" will tell us approximately where we've started in the log serial
	// range. We'll bias is by half so we won't accept any serialised events and
	// mark the MSB to indicate that the current serial should be corrected.
	uint32 Hint = EventData.GetValue<uint32>("Serial");
	Hint -= (Serial.Mask + 1) >> 1;
	Hint &= Serial.Mask;
	Serial.Value = Hint|0x80000000;

	UserUidBias = EventData.GetValue<uint32>("UserUidBias", uint32(Protocol3::EKnownEventUids::User));
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnTiming(const FOnEventContext& Context)
{
	uint64 StartCycle = Context.EventData.GetValue<uint64>("StartCycle");
	uint64 CycleFrequency = Context.EventData.GetValue<uint64>("CycleFrequency");

	Timing.BaseTimestamp = StartCycle;
	Timing.TimestampHz = CycleFrequency;
	Timing.InvTimestampHz = 1.0 / double(CycleFrequency);
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnThreadTiming(const FOnEventContext& Context)
{
	uint64 BaseTimestamp = Context.EventData.GetValue<uint64>("BaseTimestamp");
	if (FThreads::FInfo* Info = Threads.GetInfo())
	{
		Info->PrevTimestamp = BaseTimestamp;

		// We can springboard of this event as a way to know a thread has just
		// started (or at least is about to send its first event). Notify analyzers
		// so they're aware of threads that never get explicitly registered.
		const auto* OuterInfo = (FThreadInfo*)Info;
		for (uint16 i = 0, n = Analyzers.Num(); i < n; ++i)
		{
			if (IAnalyzer* Analyzer = Analyzers[i])
			{
				Analyzer->OnThreadInfo(*OuterInfo);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnThreadInfoInternal(const FOnEventContext& Context)
{
	const FEventData& EventData = Context.EventData;

	FThreads::FInfo* ThreadInfo = Threads.GetInfo();
	if (ThreadInfo == nullptr)
	{
		return;
	}
	ThreadInfo->SystemId = EventData.GetValue<uint32>("SystemId");
	ThreadInfo->SortHint = EventData.GetValue<int32>("SortHint");
	
	FAnsiStringView Name;
	EventData.GetString("Name", Name);
	ThreadInfo->Name.SetNumUninitialized(Name.Len() + 1);
	ThreadInfo->Name[Name.Len()] = '\0';
	FMemory::Memcpy(ThreadInfo->Name.GetData(), Name.GetData(), Name.Len());

	if (ThreadInfo->GroupName.Num() <= 0)
	{
		if (const TArray<uint8>* GroupName = Threads.GetGroupName())
		{
			ThreadInfo->GroupName = *GroupName;
		}
	}

	const auto* OuterInfo = (FThreadInfo*)ThreadInfo;
	for (uint16 i = 0, n = Analyzers.Num(); i < n; ++i)
	{
		if (IAnalyzer* Analyzer = Analyzers[i])
		{
			Analyzer->OnThreadInfo(*OuterInfo);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnThreadGroupBegin(const FOnEventContext& Context)
{
	const FEventData& EventData = Context.EventData;

	FAnsiStringView Name;
	EventData.GetString("Name", Name);
	Threads.SetGroupName(Name.GetData(), Name.Len());
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnThreadGroupEnd()
{
	Threads.SetGroupName("", 0);
}

////////////////////////////////////////////////////////////////////////////////
template <typename ImplType>
void FAnalysisEngine::ForEachRoute(uint32 RouteIndex, bool bScoped, ImplType&& Impl)
{
	uint32 RouteCount = Routes.Num();
	if (RouteIndex >= RouteCount)
	{
		return;
	}

	const FRoute* FirstRoute = Routes.GetData();
	const FRoute* Route = FirstRoute + RouteIndex;
	do
	{
		const FRoute* NextRoute = FirstRoute + Route->Parent;
		for (uint32 n = Route->Count; n--; ++Route)
		{
			if (Route->bScoped != (bScoped == true))
			{
				continue;
			}

			IAnalyzer* Analyzer = Analyzers[Route->AnalyzerIndex];
			if (Analyzer != nullptr)
			{
				Impl(Analyzer, Route->Id);
			}
		}
		Route = NextRoute;
	}
	while (Route >= FirstRoute);
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::OnNewEventInternal(const void* EventData)
{
	FDispatchBuilder Builder;
	switch (ProtocolVersion)
	{
	case Protocol0::EProtocol::Id:
		OnNewEventProtocol0(Builder, EventData);
		break;

	case Protocol1::EProtocol::Id:
	case Protocol2::EProtocol::Id:
	case Protocol3::EProtocol::Id:
	case Protocol4::EProtocol::Id:
		OnNewEventProtocol1(Builder, EventData);
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
	ForEachRoute(Dispatch->FirstRoute, false, [&] (IAnalyzer* Analyzer, uint16 RouteId)
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
	// Add the dispatch to the dispatch table. Usually duplicates are an error
	// but due to backwards compatibility we'll override existing dispatches.
	uint16 Uid = Dispatch->Uid;
	if (Uid < Dispatches.Num())
 	{
		if (Dispatches[Uid] != nullptr)
 		{
			FMemory::Free(Dispatches[Uid]);
			Dispatches[Uid] = nullptr;
 		}
 	}
	else
 	{
		Dispatches.SetNum(Uid + 1);
 	}

	// Find routes that have subscribed to this event.
	auto FindRoute = [this] (uint32 Hash)
	{
		int32 Index = Algo::LowerBoundBy(Routes, Hash, [] (const FRoute& Route) { return Route.Hash; });
		return (Index < Routes.Num() && Routes[Index].Hash == Hash) ? Index : -1;
	};

	int32 FirstRoute = FindRoute(Dispatch->Hash);
	if (FirstRoute < 0)
	{
		FFnv1aHash LoggerHash;
		LoggerHash.Add((const ANSICHAR*)Dispatch + Dispatch->LoggerNameOffset);
		if ((FirstRoute = FindRoute(LoggerHash.Get())) < 0)
		{
			FirstRoute = FindRoute(FFnv1aHash().Get());
		}
	}
	Dispatch->FirstRoute = FirstRoute;

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

		OutField.Class = Field.TypeInfo & Protocol0::Field_SpecialMask;
		OutField.bArray = !!(Field.TypeInfo & Protocol0::Field_Array);

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

	if (NewEvent.Flags & int(Protocol1::EEventFlags::NoSync))
	{
		Builder.SetNoSync();
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
		break;

	case Protocol1::EProtocol::Id:
		Serial.Mask = 0x0000ffff;
		ProtocolHandler = &FAnalysisEngine::OnDataProtocol2;
		break;

	case Protocol2::EProtocol::Id:
	case Protocol3::EProtocol::Id:
	case Protocol4::EProtocol::Id:
		Serial.Mask = 0x00ffffff;
		ProtocolHandler = &FAnalysisEngine::OnDataProtocol2;
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

		Begin();
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
	FThreads::FInfo ThreadInfo;

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

		FOnEventContext Context = {
			(const FThreadInfo&)ThreadInfo,
			(const FEventTime&)Timing,
			(const FEventData&)EventDataInfo,
		};

		ForEachRoute(Dispatch->FirstRoute, false, [&] (IAnalyzer* Analyzer, uint16 RouteId)
		{
			if (!Analyzer->OnEvent(RouteId, EStyle::Normal, Context))
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

	struct FRotaItem
	{
		uint32				Serial;
		uint32				ThreadId;
		FStreamReader*		Reader;
		bool				operator < (const FRotaItem& Rhs) { return Serial < Rhs.Serial; }
	};
	TArray<FRotaItem> Rota;

	for (uint32 i = 0, n = InnerTransport->GetThreadCount(); i < n; ++i)
	{
		FStreamReader* Reader = InnerTransport->GetThreadStream(i);
		uint32 ThreadId = InnerTransport->GetThreadId(i);
		Rota.Add({~0u, ThreadId, Reader});
	}

	uint32 ActiveCount = uint32(Rota.Num());
	while (true)
	{
		for (uint32 i = 0; i < ActiveCount;)
		{
			FRotaItem& RotaItem = Rota[i];

			if (int32(RotaItem.Serial) > int32(Serial.Value & Serial.Mask))
			{
				++i;
				continue;
			}

			FThreads::FInfo& ThreadInfo = Threads.GetInfo(RotaItem.ThreadId);

			uint32 AvailableSerial;
			if (ProtocolVersion == Protocol4::EProtocol::Id)
			{
				AvailableSerial = OnDataProtocol4(*(RotaItem.Reader), ThreadInfo);
			}
			else
			{
				AvailableSerial = OnDataProtocol2(*(RotaItem.Reader), ThreadInfo);
			}

			if (int32(AvailableSerial) >= 0)
			{
				RotaItem.Serial = AvailableSerial;
				if (Rota[0].Serial > AvailableSerial)
				{
					Swap(Rota[0], RotaItem);
				}
				++i;
			}
			else
			{
				FRotaItem TempItem = RotaItem;
				TempItem.Serial = ~0u;

				for (uint32 j = i, m = ActiveCount - 1; j < m; ++j)
				{
					Rota[j] = Rota[j + 1];
				}

				Rota[ActiveCount - 1] = TempItem;
				--ActiveCount;
			}

			if (((Rota[0].Serial - Serial.Value) & Serial.Mask) == 0)
			{
				i = 0;
			}
		}

		if (ActiveCount < 1)
		{
			break;
		}

		TArrayView<FRotaItem> ActiveRota(Rota.GetData(), ActiveCount);
		Algo::Sort(ActiveRota);

		int32 MinLogSerial = Rota[0].Serial;
		if (ActiveCount > 1)
		{
			int32 MaxLogSerial = Rota[ActiveCount - 1].Serial;

			if ((uint32(MinLogSerial - Serial.Value) & Serial.Mask) == 0)
			{
				continue;
			}

			// If min/max are more than half the serial range apart consider them
			// as having wrapped.
			int32 HalfRange = int32(Serial.Mask >> 1);
			if ((MaxLogSerial - MinLogSerial) >= HalfRange)
			{
				for (uint32 i = 0; i < ActiveCount; ++i)
				{
					FRotaItem& RotaItem = Rota[i];
					if (int32(RotaItem.Serial) >= HalfRange)
					{
						MinLogSerial = RotaItem.Serial;
						break;
					}
				}
			}
		}

		// If the current serial has its MSB set we're currently in a mode trying
		// to derive the best starting serial.
		if (int32(Serial.Value) < 0)
		{
			Serial.Value = (MinLogSerial & Serial.Mask);
			continue;
		}

		// If we didn't stumble across the next serialised event we have done all
		// we can for now.
		if ((uint32(MinLogSerial - Serial.Value) & Serial.Mask) != 0)
		{
			break;
		}
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////
int32 FAnalysisEngine::OnDataProtocol2(FStreamReader& Reader, FThreads::FInfo& ThreadInfo)
{
	while (true)
	{
		auto Mark = Reader.SaveMark();

		const auto* Header = Reader.GetPointer<Protocol2::FEventHeader>();
		if (Header == nullptr)
		{
			break;
		}

		uint16 Uid = Header->Uid & uint16(Protocol2::EKnownEventUids::UidMask);
		if (Uid >= Dispatches.Num())
		{
			// We don't know about this event yet
			break;
		}

		const FDispatch* Dispatch = Dispatches[Uid];
		if (Dispatch == nullptr)
		{
			// Event-types may not to be discovered in Uid order.
			break;
		}

		uint32 BlockSize = Header->Size;

		// Make sure we consume events in the correct order
		if ((Dispatch->Flags & FDispatch::Flag_NoSync) == 0)
		{
			switch (ProtocolVersion)
			{
			case Protocol1::EProtocol::Id:
				{
					const auto* HeaderV1 = (Protocol1::FEventHeader*)Header;
					if (HeaderV1->Serial != (Serial.Value & Serial.Mask))
					{
						return HeaderV1->Serial;
					}
					BlockSize += sizeof(*HeaderV1);
				}
				break;

			case Protocol2::EProtocol::Id:
			case Protocol3::EProtocol::Id:
				{
					const auto* HeaderSync = (Protocol2::FEventHeaderSync*)Header;
					uint32 EventSerial = HeaderSync->SerialLow|(uint32(HeaderSync->SerialHigh) << 16);
					if (EventSerial != (Serial.Value & Serial.Mask))
					{
						return EventSerial;
					}
					BlockSize += sizeof(*HeaderSync);
				}
				break;
			}
		}
		else
		{
			BlockSize += sizeof(*Header);
		}

		if (Reader.GetPointer(BlockSize) == nullptr)
		{
			break;
		}

		Reader.Advance(BlockSize);

		FAuxDataCollector AuxCollector;
		if (Dispatch->Flags & FDispatch::Flag_MaybeHasAux)
		{
			int AuxStatus = OnDataProtocol2Aux(Reader, AuxCollector);
			if (AuxStatus == 0)
			{
				Reader.RestoreMark(Mark);
				break;
			}
		}

		if ((Dispatch->Flags & FDispatch::Flag_NoSync) == 0)
		{
			Serial.Value += 1;
			Serial.Value &= 0x7fffffff; // don't set msb. that has other uses
		}

		FEventDataInfo EventDataInfo = {
			*Dispatch,
			&AuxCollector,
			(const uint8*)Header + BlockSize - Header->Size,
			Header->Size,
		};

		FOnEventContext Context = {
			(const FThreadInfo&)ThreadInfo,
			(const FEventTime&)Timing,
			(const FEventData&)EventDataInfo,
		};
		ForEachRoute(Dispatch->FirstRoute, false, [&] (IAnalyzer* Analyzer, uint16 RouteId)
		{
			if (!Analyzer->OnEvent(RouteId, EStyle::Normal, Context))
			{
				RetireAnalyzer(Analyzer);
			}
		});
	}

	return -1;
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
int32 FAnalysisEngine::OnDataProtocol4(FStreamReader& Reader, FThreads::FInfo& ThreadInfo)
{
	while (true)
	{
		if (int32 TriResult = OnDataProtocol4Impl(Reader, ThreadInfo))
		{
			return (TriResult < 0) ? ~TriResult : -1;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
int32 FAnalysisEngine::OnDataProtocol4Known(
	uint32 Uid,
	FStreamReader& Reader,
	FThreads::FInfo& ThreadInfo)
{
	using namespace Protocol4;

	switch (Uid)
	{
	case EKnownEventUids::NewEvent:
		if (const auto* Size = Reader.GetPointer<uint16>())
		{
			Reader.Advance(sizeof(*Size));
			uint32 EventSize = *Size;
			if (const void* EventData = Reader.GetPointer(EventSize))
			{
				Reader.Advance(EventSize);
				OnNewEventInternal(EventData);
				return 1;
			}
		}
		break;

	case EKnownEventUids::EnterScope:
	case EKnownEventUids::EnterScope_T:
		if (Uid > EKnownEventUids::EnterScope)
		{
			const uint8* Stamp = Reader.GetPointer(sizeof(uint64) - 1);
			if (Stamp == nullptr)
			{
				break;
			}
			uint64 Timestamp = ThreadInfo.PrevTimestamp += *(uint64*)(Stamp - 1) >> 8;
			ThreadInfo.ScopeRoutes.Push(~Timestamp);
			Reader.Advance(sizeof(uint64) - 1);
		}
		else
		{
			ThreadInfo.ScopeRoutes.Push(~0);
		}

		return 1;

	case EKnownEventUids::LeaveScope:
	case EKnownEventUids::LeaveScope_T:
		Timing.EventTimestamp = 0;
		if (Uid > EKnownEventUids::LeaveScope)
		{
			const uint8* Stamp = Reader.GetPointer(sizeof(uint64) - 1);
			if (Stamp == nullptr)
			{
				break;
			}
			Timing.EventTimestamp = ThreadInfo.PrevTimestamp += *(uint64*)(Stamp - 1) >> 8;
			Reader.Advance(sizeof(uint64) - 1);
		}

		if (ThreadInfo.ScopeRoutes.Num() > 0)
		{
			uint32 RouteIndex = ThreadInfo.ScopeRoutes.Pop(false);

			FDispatch EmptyDispatch = {};
			FEventDataInfo EmptyEventInfo = { EmptyDispatch };

			FOnEventContext Context = {
				(const FThreadInfo&)ThreadInfo,
				(const FEventTime&)Timing,
				(const FEventData&)EmptyEventInfo,
			};
			ForEachRoute(RouteIndex, true, [&] (IAnalyzer* Analyzer, uint16 RouteId)
			{
				if (!Analyzer->OnEvent(RouteId, EStyle::LeaveScope, Context))
				{
					RetireAnalyzer(Analyzer);
				}
			});
		}

		Timing.EventTimestamp = 0;
		return 1;
	};

	return 0;
}

////////////////////////////////////////////////////////////////////////////////
int32 FAnalysisEngine::OnDataProtocol4Impl(
	FStreamReader& Reader,
	FThreads::FInfo& ThreadInfo)
{
	/* Returns 0 if an event was successfully processed, 1 if there's not enough
	 * data available, or ~AvailableLogSerial if the pending event is in the future */
	using namespace Protocol4;

	auto Mark = Reader.SaveMark();

	const auto* UidCursor = Reader.GetPointer<uint8>();
	if (UidCursor == nullptr)
	{
		return 1;
	}

	uint32 UidBytes = 1 + !!(*UidCursor & EKnownEventUids::Flag_TwoByteUid);
	if (UidBytes > 1 && Reader.GetPointer(UidBytes) == nullptr)
	{
		return 1;
	}

	uint32 Uid = ~0u;
	switch (UidBytes)
	{
		case 1:	Uid = *UidCursor;			break;
		case 2:	Uid = *(uint16*)UidCursor;	break;
	}
	Uid >>= EKnownEventUids::_UidShift;

	if (Uid < UserUidBias)
	{
		Reader.Advance(UidBytes);
		if (!OnDataProtocol4Known(Uid, Reader, ThreadInfo))
		{
			Reader.RestoreMark(Mark);
			return 1;
		}
		return 0;
	}

	// Do we know about this event type yet?
	if (Uid >= uint32(Dispatches.Num()))
	{
		return 1;
	}

	const FDispatch* Dispatch = Dispatches[Uid];
	if (Dispatch == nullptr)
	{
		return 1;
	}

	// Parse the header
	const auto* Header = Reader.GetPointer<FEventHeader>();
	if (Header == nullptr)
	{
		return 1;
	}

	uint32 BlockSize = Header->Size;

	// Make sure we consume events in the correct order
	if ((Dispatch->Flags & FDispatch::Flag_NoSync) == 0)
	{
		const auto* HeaderSync = (Protocol4::FEventHeaderSync*)Header;
		uint32 EventSerial = HeaderSync->SerialLow|(uint32(HeaderSync->SerialHigh) << 16);
		if (EventSerial != (Serial.Value & Serial.Mask))
		{
			return ~EventSerial;
		}
		BlockSize += sizeof(*HeaderSync);
	}
	else
	{
		BlockSize += sizeof(*Header);
	}

	// Is all the event's data available?
	if (Reader.GetPointer(BlockSize) == nullptr)
	{
		return 1;
	}

	Reader.Advance(BlockSize);

	// Collect auxiliary data
	FAuxDataCollector AuxCollector;
	if (Dispatch->Flags & FDispatch::Flag_MaybeHasAux)
	{
		int AuxStatus = OnDataProtocol2Aux(Reader, AuxCollector);
		if (AuxStatus == 0)
		{
			Reader.RestoreMark(Mark);
			return 1;
		}
	}

	// Maintain sync
	if ((Dispatch->Flags & FDispatch::Flag_NoSync) == 0)
	{
		Serial.Value += 1;
		Serial.Value &= 0x7fffffff; // don't set msb. that has other uses
	}

	// Sent the event to subscribed analyzers
	FEventDataInfo EventDataInfo = {
		*Dispatch,
		&AuxCollector,
		(const uint8*)Header + BlockSize - Header->Size,
		Header->Size,
	};

	EStyle Style = EStyle::Normal;
	if (ThreadInfo.ScopeRoutes.Num() > 0 && int64(ThreadInfo.ScopeRoutes.Last()) < 0)
	{
		Style = EStyle::EnterScope;
		Timing.EventTimestamp = ~(ThreadInfo.ScopeRoutes.Last());
		ThreadInfo.ScopeRoutes.Last() = Dispatch->FirstRoute;
	}
	else
	{
		Timing.EventTimestamp = 0;
	}

	FOnEventContext Context = {
		(const FThreadInfo&)ThreadInfo,
		(const FEventTime&)Timing,
		(const FEventData&)EventDataInfo,
	};

	bool bScoped = (Style != EStyle::Normal);
	ForEachRoute(Dispatch->FirstRoute, bScoped, [&] (IAnalyzer* Analyzer, uint16 RouteId)
	{
		if (!Analyzer->OnEvent(RouteId, Style, Context))
		{
			RetireAnalyzer(Analyzer);
		}
	});

	return 0;
}

} // namespace Trace
