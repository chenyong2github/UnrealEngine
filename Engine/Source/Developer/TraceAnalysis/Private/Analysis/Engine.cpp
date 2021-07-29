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
#include "Trace/Detail/Transport.h"
#include "Transport/PacketTransport.h"
#include "Transport/Transport.h"
#include "Transport/TidPacketTransport.h"

namespace UE {
namespace Trace {

// {{{1 misc -------------------------------------------------------------------

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



// {{{1 aux-data ---------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
struct FAuxData
{
	const uint8*	Data;
	uint32			DataSize;
	uint16			FieldIndex;
	int16			FieldSizeAndType;
};

////////////////////////////////////////////////////////////////////////////////
struct FAuxDataCollector
: public TArray<FAuxData>
{
};



// {{{1 threads ----------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
class FThreads
{
public:
	struct FInfo
	{
		TArray<uint64>	ScopeRoutes;
		TArray<uint8>	Name;
		TArray<uint8>	GroupName;
		int64			PrevTimestamp = 0;
		uint32			ThreadId = ~0u;
		uint32			SystemId = 0;
		int32			SortHint = 0xff;
	};

						FThreads();
						~FThreads();
	FInfo*				GetInfo();
	FInfo*				GetInfo(uint32 ThreadId);
	void				SetGroupName(const ANSICHAR* InGroupName, uint32 Length);
	const TArray<uint8>*GetGroupName() const;

private:
	uint32				LastGetInfoId = ~0u;
	TArray<FInfo*>		Infos;
	TArray<uint8>		GroupName;
};

////////////////////////////////////////////////////////////////////////////////
FThreads::FThreads()
{
	Infos.Reserve(64);
}

////////////////////////////////////////////////////////////////////////////////
FThreads::~FThreads()
{
	for (FInfo* Info : Infos)
	{
		delete Info;
	}
}

////////////////////////////////////////////////////////////////////////////////
FThreads::FInfo* FThreads::GetInfo()
{
	return GetInfo(LastGetInfoId);
}

////////////////////////////////////////////////////////////////////////////////
FThreads::FInfo* FThreads::GetInfo(uint32 ThreadId)
{
	LastGetInfoId = ThreadId;

	if (ThreadId >= uint32(Infos.Num()))
	{
		Infos.SetNumZeroed(ThreadId + 1);
	}

	FInfo* Info = Infos[ThreadId];
	if (Info == nullptr)
	{
		Info = new FInfo();
		Info->ThreadId = ThreadId;
		Infos[ThreadId] = Info;
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




// {{{1 thread-info ------------------------------------------------------------

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



// {{{1 timing -----------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
struct FTiming
{
	uint64	BaseTimestamp;
	uint64	TimestampHz;
	double	InvTimestampHz;
	uint64	EventTimestamp = 0;
};



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



// {{{1 dispatch ---------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
struct FDispatch
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
	uint8			FieldCount			= 0;
	uint8			Flags				= 0;
	uint16			EventSize			= 0;
	uint16			LoggerNameOffset	= 0;	// From FDispatch ptr
	uint16			EventNameOffset		= 0;	// From FDispatch ptr
	FField			Fields[];
};

////////////////////////////////////////////////////////////////////////////////
int32 FDispatch::GetFieldIndex(const ANSICHAR* Name) const
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



// {{{1 dispatch-builder -------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
class FDispatchBuilder
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
FDispatchBuilder::FDispatchBuilder()
{
	Buffer.SetNum(sizeof(FDispatch));

	auto* Dispatch = (FDispatch*)(Buffer.GetData());
	new (Dispatch) FDispatch();
}

////////////////////////////////////////////////////////////////////////////////
FDispatch* FDispatchBuilder::Finalize()
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
void FDispatchBuilder::SetUid(uint16 Uid)
{
	auto* Dispatch = (FDispatch*)(Buffer.GetData());
	Dispatch->Uid = Uid;
}

////////////////////////////////////////////////////////////////////////////////
void FDispatchBuilder::SetLoggerName(const ANSICHAR* Name, int32 NameSize)
{
	auto* Dispatch = (FDispatch*)(Buffer.GetData());
	Dispatch->LoggerNameOffset += AppendName(Name, NameSize);
}

////////////////////////////////////////////////////////////////////////////////
void FDispatchBuilder::SetEventName(const ANSICHAR* Name, int32 NameSize)
{
	auto* Dispatch = (FDispatch*)(Buffer.GetData());
	Dispatch->EventNameOffset = AppendName(Name, NameSize);
}

////////////////////////////////////////////////////////////////////////////////
void FDispatchBuilder::SetImportant()
{
	auto* Dispatch = (FDispatch*)(Buffer.GetData());
	Dispatch->Flags |= FDispatch::Flag_Important;
}

////////////////////////////////////////////////////////////////////////////////
void FDispatchBuilder::SetMaybeHasAux()
{
	auto* Dispatch = (FDispatch*)(Buffer.GetData());
	Dispatch->Flags |= FDispatch::Flag_MaybeHasAux;
}

////////////////////////////////////////////////////////////////////////////////
void FDispatchBuilder::SetNoSync()
{
	auto* Dispatch = (FDispatch*)(Buffer.GetData());
	Dispatch->Flags |= FDispatch::Flag_NoSync;
}

////////////////////////////////////////////////////////////////////////////////
FDispatch::FField& FDispatchBuilder::AddField(const ANSICHAR* Name, int32 NameSize, uint16 Size)
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
uint32 FDispatchBuilder::AppendName(const ANSICHAR* Name, int32 NameSize)
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



// {{{1 event-type-info --------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
uint32 IAnalyzer::FEventTypeInfo::GetId() const
{
	const auto* Inner = (const FDispatch*)this;
	return Inner->Uid;
}

////////////////////////////////////////////////////////////////////////////////
const ANSICHAR* IAnalyzer::FEventTypeInfo::GetName() const
{
	const auto* Inner = (const FDispatch*)this;
	return (const ANSICHAR*)Inner + Inner->EventNameOffset;
}

////////////////////////////////////////////////////////////////////////////////
const ANSICHAR* IAnalyzer::FEventTypeInfo::GetLoggerName() const
{
	const auto* Inner = (const FDispatch*)this;
	return (const ANSICHAR*)Inner + Inner->LoggerNameOffset;
}

////////////////////////////////////////////////////////////////////////////////
uint32 IAnalyzer::FEventTypeInfo::GetSize() const
{
	const auto* Inner = (const FDispatch*)this;
	return Inner->EventSize;
}

////////////////////////////////////////////////////////////////////////////////
uint32 IAnalyzer::FEventTypeInfo::GetFieldCount() const
{
	const auto* Inner = (const FDispatch*)this;
	return Inner->FieldCount;
}

////////////////////////////////////////////////////////////////////////////////
const IAnalyzer::FEventFieldInfo* IAnalyzer::FEventTypeInfo::GetFieldInfo(uint32 Index) const
{
	if (Index >= GetFieldCount())
	{
		return nullptr;
	}

	const auto* Inner = (const FDispatch*)this;
	return (const IAnalyzer::FEventFieldInfo*)(Inner->Fields + Index);
}

////////////////////////////////////////////////////////////////////////////////
IAnalyzer::FEventFieldHandle IAnalyzer::FEventTypeInfo::GetFieldHandleImpl(
	const ANSICHAR* FieldName,
	int16& SizeAndType) const
{
	const auto* Inner = (const FDispatch*)this;
	int32 Index = Inner->GetFieldIndex(FieldName);
	if (Index < 0)
	{
		return { -1 };
	}

	const FDispatch::FField& Field = Inner->Fields[Index];
	SizeAndType = Field.SizeAndType;
	return { Field.Offset };
}



// {{{1 field-info -------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
const ANSICHAR* IAnalyzer::FEventFieldInfo::GetName() const
{
	const auto* Inner = (const FDispatch::FField*)this;
	return (const ANSICHAR*)(UPTRINT(Inner) + Inner->NameOffset);
}

////////////////////////////////////////////////////////////////////////////////
IAnalyzer::FEventFieldInfo::EType IAnalyzer::FEventFieldInfo::GetType() const
{
	const auto* Inner = (const FDispatch::FField*)this;

	if (Inner->Class == UE::Trace::Protocol0::Field_String)
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
	const auto* Inner = (const FDispatch::FField*)this;
	return Inner->bArray;
}



// {{{1 array-reader -----------------------------------------------------------

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



// {{{1 event-data -------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
struct FEventDataInfo
{
	const FAuxData*		GetAuxData(uint32 FieldIndex) const;
	const uint8*		Ptr;
	const FDispatch&	Dispatch;
	FAuxDataCollector*	AuxCollector;
	uint16				Size;
};

////////////////////////////////////////////////////////////////////////////////
const FAuxData* FEventDataInfo::GetAuxData(uint32 FieldIndex) const
{
	const auto* Info = (const FEventDataInfo*)this;
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
	const auto* Info = (const FEventDataInfo*)this;
	return (const FEventTypeInfo&)(Info->Dispatch);
}

////////////////////////////////////////////////////////////////////////////////
const IAnalyzer::FArrayReader* IAnalyzer::FEventData::GetArrayImpl(const ANSICHAR* FieldName) const
{
	const auto* Info = (const FEventDataInfo*)this;

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
	const auto* Info = (const FEventDataInfo*)this;
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
	const auto* Info = (const FEventDataInfo*)this;
	const auto& Dispatch = Info->Dispatch;

	int32 Index = Dispatch.GetFieldIndex(FieldName);
	if (Index < 0)
	{
		return false;
	}

	const auto& Field = Dispatch.Fields[Index];
	if (Field.Class != UE::Trace::Protocol0::Field_String || Field.SizeAndType != sizeof(ANSICHAR))
	{
		return false;
	}

	if (const FAuxData* Data = Info->GetAuxData(Index))
	{
		Out = FAnsiStringView((const ANSICHAR*)(Data->Data), Data->DataSize);
		return true;
	}

	Out = FAnsiStringView();
	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool IAnalyzer::FEventData::GetString(const ANSICHAR* FieldName, FStringView& Out) const
{
	const auto* Info = (const FEventDataInfo*)this;
	const auto& Dispatch = Info->Dispatch;

	int32 Index = Dispatch.GetFieldIndex(FieldName);
	if (Index < 0)
	{
		return false;
	}

	const auto& Field = Dispatch.Fields[Index];
	if (Field.Class != UE::Trace::Protocol0::Field_String || Field.SizeAndType != sizeof(TCHAR))
	{
		return false;
	}

	if (const FAuxData* Data = Info->GetAuxData(Index))
	{
		Out = FStringView((const TCHAR*)(Data->Data), Data->DataSize / sizeof(TCHAR));
		return true;
	}

	Out = FStringView();
	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool IAnalyzer::FEventData::GetString(const ANSICHAR* FieldName, FString& Out) const
{
	const auto* Info = (const FEventDataInfo*)this;
	const auto& Dispatch = Info->Dispatch;

	int32 Index = Dispatch.GetFieldIndex(FieldName);
	if (Index < 0)
	{
		return false;
	}

	const auto& Field = Dispatch.Fields[Index];
	if (Field.Class == UE::Trace::Protocol0::Field_String)
	{
		const FAuxData* Data = Info->GetAuxData(Index);
		if (Data == nullptr)
		{
			Out = FString();
			return true;
		}

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
uint32 IAnalyzer::FEventData::GetSize() const
{
	const auto* Info = (const FEventDataInfo*)this;
	uint32 Size = Info->Size;
	for (const FAuxData& Data : *(Info->AuxCollector))
	{
		Size += Data.DataSize;
	}
	return Size;
}

////////////////////////////////////////////////////////////////////////////////
void IAnalyzer::FEventData::SerializeToCbor(TArray<uint8>& Out) const
{
	const auto* Info = (const FEventDataInfo*)this;
	uint32 Size = Info->Size;
	if (Info->AuxCollector != nullptr)
	{
		for (FAuxData& Data : *(Info->AuxCollector))
		{
			Size += Data.DataSize;
		}
	}
	SerializeToCborImpl(Out, *this, Size);
}

////////////////////////////////////////////////////////////////////////////////
const uint8* IAnalyzer::FEventData::GetAttachment() const
{
	const auto* Info = (const FEventDataInfo*)this;
	return Info->Ptr + Info->Dispatch.EventSize;
}

////////////////////////////////////////////////////////////////////////////////
uint32 IAnalyzer::FEventData::GetAttachmentSize() const
{
	const auto* Info = (const FEventDataInfo*)this;
	return Info->Size - Info->Dispatch.EventSize;
}
// }}}



// {{{1 type-registry ----------------------------------------------------------
class FTypeRegistry
{
public:
	typedef FDispatch FTypeInfo;

						~FTypeRegistry();
	const FTypeInfo*	Add(const void* TraceData);
	void				Add(FTypeInfo* TypeInfo);
	const FTypeInfo*	Get(uint32 Uid) const;

private:
	TArray<FTypeInfo*>	TypeInfos;
};

////////////////////////////////////////////////////////////////////////////////
FTypeRegistry::~FTypeRegistry()
{
	for (FTypeInfo* TypeInfo : TypeInfos)
	{
		FMemory::Free(TypeInfo);
	}
}

////////////////////////////////////////////////////////////////////////////////
const FTypeRegistry::FTypeInfo* FTypeRegistry::Get(uint32 Uid) const
{
	if (Uid >= uint32(TypeInfos.Num()))
	{
		return nullptr;
	}

	return TypeInfos[Uid];
}

////////////////////////////////////////////////////////////////////////////////
const FTypeRegistry::FTypeInfo* FTypeRegistry::Add(const void* TraceData)
{
	FDispatchBuilder Builder;

	const auto& NewEvent = *(Protocol4::FNewEventEvent*)(TraceData);

	const auto* NameCursor = (const ANSICHAR*)(NewEvent.Fields + NewEvent.FieldCount);

	Builder.SetLoggerName(NameCursor, NewEvent.LoggerNameSize);
	NameCursor += NewEvent.LoggerNameSize;

	Builder.SetEventName(NameCursor, NewEvent.EventNameSize);
	NameCursor += NewEvent.EventNameSize;
	Builder.SetUid(NewEvent.EventUid);

	// Fill out the fields
	for (int32 i = 0, n = NewEvent.FieldCount; i < n; ++i)
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

	if (NewEvent.Flags & uint32(Protocol4::EEventFlags::Important))
	{
		Builder.SetImportant();
	}

	if (NewEvent.Flags & uint32(Protocol4::EEventFlags::MaybeHasAux))
	{
		Builder.SetMaybeHasAux();
	}

	if (NewEvent.Flags & uint32(Protocol4::EEventFlags::NoSync))
	{
		Builder.SetNoSync();
	}

	FTypeInfo* TypeInfo = Builder.Finalize();
	Add(TypeInfo);

	return TypeInfo;
}

////////////////////////////////////////////////////////////////////////////////
void FTypeRegistry::Add(FTypeInfo* TypeInfo)
{
	// Add the type to the type-infos table. Usually duplicates are an error
	// but due to backwards compatibility we'll override existing types.
	uint16 Uid = TypeInfo->Uid;
	if (Uid < TypeInfos.Num())
 	{
		if (TypeInfos[Uid] != nullptr)
 		{
			FMemory::Free(TypeInfos[Uid]);
			TypeInfos[Uid] = nullptr;
 		}
 	}
	else
 	{
		TypeInfos.SetNum(Uid + 1);
 	}

	TypeInfos[Uid] = TypeInfo;
}

// {{{1 analyzer-hub -----------------------------------------------------------
class FAnalyzerHub
{
public:
	void				End();
	void				SetAnalyzers(TArray<IAnalyzer*>&& InAnalyzers);
	void				OnNewType(const FTypeRegistry::FTypeInfo* TypeInfo);
	void				OnEvent(const FTypeRegistry::FTypeInfo& TypeInfo, IAnalyzer::EStyle Style, const IAnalyzer::FOnEventContext& Context);
	void				OnThreadInfo(const FThreads::FInfo& ThreadInfo);

private:
	void				BuildRoutes();
	void				AddRoute(uint16 AnalyzerIndex, uint16 Id, const ANSICHAR* Logger, const ANSICHAR* Event, bool bScoped);
	int32				GetRouteIndex(const FTypeRegistry::FTypeInfo& TypeInfo);
	void				RetireAnalyzer(IAnalyzer* Analyzer);
	template <typename ImplType>
	void				ForEachRoute(uint32 RouteIndex, bool bScoped, ImplType&& Impl) const;

	struct FRoute
	{
		uint32			Hash;
		uint16			AnalyzerIndex : 15;
		uint16			bScoped : 1;
		uint16			Id;
		union
		{
			uint32		ParentHash;
			struct
			{
				int16	Count;
				int16	Parent;
			};
		};
	};

	typedef TArray<uint16, TInlineAllocator<96>> TypeToRouteArray;

	TArray<IAnalyzer*>	Analyzers;
	TArray<FRoute>		Routes;
	TypeToRouteArray	TypeToRoute; // biases by one so zero represents no route
};

////////////////////////////////////////////////////////////////////////////////
void FAnalyzerHub::End()
{
	for (IAnalyzer* Analyzer : Analyzers)
	{
		if (Analyzer != nullptr)
		{
			Analyzer->OnAnalysisEnd();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAnalyzerHub::SetAnalyzers(TArray<IAnalyzer*>&& InAnalyzers)
{
	Analyzers = MoveTemp(InAnalyzers);
	BuildRoutes();
}

////////////////////////////////////////////////////////////////////////////////
int32 FAnalyzerHub::GetRouteIndex(const FTypeRegistry::FTypeInfo& TypeInfo)
{
	if (TypeInfo.Uid >= uint32(TypeToRoute.Num()))
	{
		return -1;
	}

	return int32(TypeToRoute[TypeInfo.Uid]) - 1;
}

////////////////////////////////////////////////////////////////////////////////
template <typename ImplType>
void FAnalyzerHub::ForEachRoute(uint32 RouteIndex, bool bScoped, ImplType&& Impl) const
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
void FAnalyzerHub::OnNewType(const FTypeRegistry::FTypeInfo* TypeInfo)
{
	// Find routes that have subscribed to this event.
	auto FindRoute = [this] (uint32 Hash)
	{
		int32 Index = Algo::LowerBoundBy(Routes, Hash, [] (const FRoute& Route) { return Route.Hash; });
		return (Index < Routes.Num() && Routes[Index].Hash == Hash) ? Index : -1;
	};

	int32 FirstRoute = FindRoute(TypeInfo->Hash);
	if (FirstRoute < 0)
	{
		FFnv1aHash LoggerHash;
		LoggerHash.Add((const ANSICHAR*)TypeInfo + TypeInfo->LoggerNameOffset);
		if ((FirstRoute = FindRoute(LoggerHash.Get())) < 0)
		{
			FirstRoute = FindRoute(FFnv1aHash().Get());
		}
	}

	uint16 Uid = TypeInfo->Uid;
	if (Uid >= uint16(TypeToRoute.Num()))
 	{
		TypeToRoute.SetNumZeroed(Uid + 32);
 	}

	TypeToRoute[Uid] = uint16(FirstRoute + 1);

	// Inform routes that a new event has been declared.
	if (FirstRoute >= 0)
	{
		ForEachRoute(FirstRoute, false, [&] (IAnalyzer* Analyzer, uint16 RouteId)
		{
			if (!Analyzer->OnNewEvent(RouteId, *(IAnalyzer::FEventTypeInfo*)TypeInfo))
			{
				RetireAnalyzer(Analyzer);
			}
		});
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAnalyzerHub::OnEvent(
	const FTypeRegistry::FTypeInfo& TypeInfo,
	const IAnalyzer::EStyle Style,
	const IAnalyzer::FOnEventContext& Context)
{
	int32 RouteIndex = GetRouteIndex(TypeInfo);
	if (RouteIndex < 0)
	{
		return;
	}

	ForEachRoute(RouteIndex, false, [&] (IAnalyzer* Analyzer, uint16 RouteId)
	{
		if (!Analyzer->OnEvent(RouteId, Style, Context))
		{
			RetireAnalyzer(Analyzer);
		}
	});
}

////////////////////////////////////////////////////////////////////////////////
void FAnalyzerHub::OnThreadInfo(const FThreads::FInfo& ThreadInfo)
{
	const auto& OuterThreadInfo = (IAnalyzer::FThreadInfo&)ThreadInfo;
	for (IAnalyzer* Analyzer : Analyzers)
	{
		if (Analyzer != nullptr)
		{
			Analyzer->OnThreadInfo(OuterThreadInfo);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAnalyzerHub::BuildRoutes()
{
	// Call out to all registered analyzers to have them register event interest
	struct : public IAnalyzer::FInterfaceBuilder
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

		FAnalyzerHub* Self;
		uint16 AnalyzerIndex;
	} Builder;
	Builder.Self = this;

	IAnalyzer::FOnAnalysisContext OnAnalysisContext = { Builder };
	for (uint16 i = 0, n = Analyzers.Num(); i < n; ++i)
	{
		uint32 RouteCount = Routes.Num();

		Builder.AnalyzerIndex = i;
		IAnalyzer* Analyzer = Analyzers[i];
		Analyzer->OnAnalysisBegin(OnAnalysisContext);

		// If the analyzer didn't add any routes we'll retire it immediately
		if (RouteCount == Routes.Num())
		{
			RetireAnalyzer(Analyzer);
		}
	}

	// Sort routes by their hashes.
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
}

////////////////////////////////////////////////////////////////////////////////
void FAnalyzerHub::RetireAnalyzer(IAnalyzer* Analyzer)
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
void FAnalyzerHub::AddRoute(
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

// {{{1 state ------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
struct FAnalysisState
{
	struct FSerial
	{
		uint32	Value = 0;
		uint32	Mask = 0;
	};

	FThreads	Threads;
	FTiming		Timing;
	FSerial		Serial;
	uint32		UserUidBias = Protocol4::EKnownEventUids::User;
};



// {{{1 thread-info-cb ---------------------------------------------------------
class FThreadInfoCallback
{
public:
	virtual			~FThreadInfoCallback() {}
	virtual void	OnThreadInfo(const FThreads::FInfo& Info) = 0;
};



// {{{1 analyzer ---------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
enum
{
	RouteId_NewTrace,
	RouteId_SerialSync,
	RouteId_Timing,
	RouteId_ThreadTiming,
	RouteId_ThreadInfo,
	RouteId_ThreadGroupBegin,
	RouteId_ThreadGroupEnd,
};

////////////////////////////////////////////////////////////////////////////////
class FTraceAnalyzer
	: public IAnalyzer
{
public:
							FTraceAnalyzer(FAnalysisState& InState, FThreadInfoCallback& InCallback);
	virtual bool			OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;
	virtual void			OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	void					OnNewTrace(const FOnEventContext& Context);
	void					OnSerialSync(const FOnEventContext& Context);
	void					OnThreadTiming(const FOnEventContext& Context);
	void					OnThreadInfoInternal(const FOnEventContext& Context);
	void					OnThreadGroupBegin(const FOnEventContext& Context);
	void					OnThreadGroupEnd();
	void					OnTiming(const FOnEventContext& Context);

private:
	FAnalysisState&			State;
	FThreadInfoCallback&	ThreadInfoCallback;
};

////////////////////////////////////////////////////////////////////////////////
FTraceAnalyzer::FTraceAnalyzer(FAnalysisState& InState, FThreadInfoCallback& InCallback)
: State(InState)
, ThreadInfoCallback(InCallback)
{
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;
	Builder.RouteEvent(RouteId_NewTrace,		"$Trace", "NewTrace");
	Builder.RouteEvent(RouteId_SerialSync,		"$Trace", "SerialSync");
	Builder.RouteEvent(RouteId_Timing,			"$Trace", "Timing");
	Builder.RouteEvent(RouteId_ThreadTiming,	"$Trace", "ThreadTiming");
	Builder.RouteEvent(RouteId_ThreadInfo,		"$Trace", "ThreadInfo");
	Builder.RouteEvent(RouteId_ThreadGroupBegin,"$Trace", "ThreadGroupBegin");
	Builder.RouteEvent(RouteId_ThreadGroupEnd,	"$Trace", "ThreadGroupEnd");
}

////////////////////////////////////////////////////////////////////////////////
bool FTraceAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	switch (RouteId)
	{
	case RouteId_NewTrace:			OnNewTrace(Context);			break;
	case RouteId_SerialSync:		OnSerialSync(Context);			break;
	case RouteId_Timing:			OnTiming(Context);				break;
	case RouteId_ThreadTiming:		OnThreadTiming(Context);		break;
	case RouteId_ThreadInfo:		OnThreadInfoInternal(Context);	break;
	case RouteId_ThreadGroupBegin:	OnThreadGroupBegin(Context);	break;
	case RouteId_ThreadGroupEnd:	OnThreadGroupEnd();				break;
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAnalyzer::OnNewTrace(const FOnEventContext& Context)
{
	const FEventData& EventData = Context.EventData;

	// "Serial" will tell us approximately where we've started in the log serial
	// range. We'll bias it by half so we won't accept any serialised events and
	// mark the MSB to indicate that the current serial should be corrected.
	auto& Serial = State.Serial;
	uint32 Hint = EventData.GetValue<uint32>("Serial");
	Hint -= (Serial.Mask + 1) >> 1;
	Hint &= Serial.Mask;
	Serial.Value = Hint;
	Serial.Value |= 0xc0000000;

	// Later traces will have an explicit "SerialSync" trace event to indicate
	// when there is enough data to establish the correct log serial
	if ((EventData.GetValue<uint8>("FeatureSet") & 1) == 0)
	{
		OnSerialSync(Context);
	}

	State.UserUidBias = EventData.GetValue<uint32>("UserUidBias", uint32(UE::Trace::Protocol3::EKnownEventUids::User));

	OnTiming(Context);
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAnalyzer::OnSerialSync(const FOnEventContext& Context)
{
	State.Serial.Value &= ~0x40000000;
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAnalyzer::OnTiming(const FOnEventContext& Context)
{
	uint64 StartCycle = Context.EventData.GetValue<uint64>("StartCycle");
	uint64 CycleFrequency = Context.EventData.GetValue<uint64>("CycleFrequency");

	State.Timing.BaseTimestamp = StartCycle;
	State.Timing.TimestampHz = CycleFrequency;
	State.Timing.InvTimestampHz = 1.0 / double(CycleFrequency);
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAnalyzer::OnThreadTiming(const FOnEventContext& Context)
{
	uint64 BaseTimestamp = Context.EventData.GetValue<uint64>("BaseTimestamp");
	if (FThreads::FInfo* Info = State.Threads.GetInfo())
	{
		Info->PrevTimestamp = BaseTimestamp;

		// We can springboard of this event as a way to know a thread has just
		// started (or at least is about to send its first event). Notify analyzers
		// so they're aware of threads that never get explicitly registered.
		ThreadInfoCallback.OnThreadInfo(*Info);
	}
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAnalyzer::OnThreadInfoInternal(const FOnEventContext& Context)
{
	const FEventData& EventData = Context.EventData;

	FThreads::FInfo* ThreadInfo;
	uint32 ThreadId = EventData.GetValue<uint32>("ThreadId", ~0u);
	if (ThreadId != ~0u)
	{
		// Post important-events; the thread-info event is not on the thread it
		// represents anymore. Fortunately the thread-id is traced now.
		ThreadInfo = State.Threads.GetInfo(ThreadId);
	}
	else
	{
		ThreadInfo = State.Threads.GetInfo();
	}

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
		if (const TArray<uint8>* GroupName = State.Threads.GetGroupName())
		{
			ThreadInfo->GroupName = *GroupName;
		}
	}

	ThreadInfoCallback.OnThreadInfo(*ThreadInfo);
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAnalyzer::OnThreadGroupBegin(const FOnEventContext& Context)
{
	const FEventData& EventData = Context.EventData;

	FAnsiStringView Name;
	EventData.GetString("Name", Name);
	State.Threads.SetGroupName(Name.GetData(), Name.Len());
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAnalyzer::OnThreadGroupEnd()
{
	State.Threads.SetGroupName("", 0);
}

// {{{1 bridge -----------------------------------------------------------------
class FAnalysisBridge
	: public FThreadInfoCallback
{
public:
	typedef FAnalysisState::FSerial FSerial;

						FAnalysisBridge(TArray<IAnalyzer*>&& Analyzers);
	void				Reset();
	uint32				GetUserUidBias() const;
	FSerial&			GetSerial();
	void				SetActiveThread(uint32 ThreadId);
	void				OnNewType(const FTypeRegistry::FTypeInfo* TypeInfo);
	void				OnEvent(const FEventDataInfo& EventDataInfo);
	virtual void		OnThreadInfo(const FThreads::FInfo& InThreadInfo) override;
	void				EnterScope(uint64 Timestamp=0);
	void				LeaveScope(uint64 Timestamp=0);

private:
	FAnalysisState		State;
	FTraceAnalyzer		TraceAnalyzer = { State, *this };
	FAnalyzerHub		AnalyzerHub;
	FThreads::FInfo*	ThreadInfo = nullptr;
};

////////////////////////////////////////////////////////////////////////////////
FAnalysisBridge::FAnalysisBridge(TArray<IAnalyzer*>&& Analyzers)
{
	TArray<IAnalyzer*> TempAnalyzers(MoveTemp(Analyzers));
	TempAnalyzers.Add(&TraceAnalyzer);
	AnalyzerHub.SetAnalyzers(MoveTemp(TempAnalyzers));
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisBridge::Reset()
{
	AnalyzerHub.End();

	State.~FAnalysisState();
	new (&State) FAnalysisState();
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisBridge::SetActiveThread(uint32 ThreadId)
{
	ThreadInfo = State.Threads.GetInfo(ThreadId);
}

////////////////////////////////////////////////////////////////////////////////
uint32 FAnalysisBridge::GetUserUidBias() const
{
	return State.UserUidBias;
}

////////////////////////////////////////////////////////////////////////////////
FAnalysisBridge::FSerial& FAnalysisBridge::GetSerial()
{
	return State.Serial;
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisBridge::OnNewType(const FTypeRegistry::FTypeInfo* TypeInfo)
{
	AnalyzerHub.OnNewType(TypeInfo);
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisBridge::OnEvent(const FEventDataInfo& EventDataInfo)
{
	IAnalyzer::EStyle Style = IAnalyzer::EStyle::Normal;
	if (ThreadInfo->ScopeRoutes.Num() > 0 && int64(ThreadInfo->ScopeRoutes.Last()) < 0)
	{
		Style = IAnalyzer::EStyle::EnterScope;
		State.Timing.EventTimestamp = ~(ThreadInfo->ScopeRoutes.Last());
	}
	else
	{
		State.Timing.EventTimestamp = 0;
	}

	// TODO "Dispatch" should be renamed "EventTypeInfo" or similar.
	const FTypeRegistry::FTypeInfo* TypeInfo = &(EventDataInfo.Dispatch);

	IAnalyzer::FOnEventContext Context = {
		*(const IAnalyzer::FThreadInfo*)ThreadInfo,
		(const IAnalyzer::FEventTime&)(State.Timing),
		(const IAnalyzer::FEventData&)EventDataInfo,
	};
	AnalyzerHub.OnEvent(*TypeInfo, Style, Context);

	if (Style == IAnalyzer::EStyle::EnterScope)
	{
		ThreadInfo->ScopeRoutes.Last() = PTRINT(TypeInfo);
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisBridge::OnThreadInfo(const FThreads::FInfo& InThreadInfo)
{
	// Note that InThreadInfo might not equal the bridge's ThreadInfo because
	// information about threads comes from trace and could have been traced on
	// a different thread to the one it is describing (or no thread at all in
	// the case of important events).
	AnalyzerHub.OnThreadInfo(InThreadInfo);
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisBridge::EnterScope(uint64 Timestamp)
{
    if (Timestamp)
    {
	    Timestamp = ThreadInfo->PrevTimestamp += Timestamp;
    }

    ThreadInfo->ScopeRoutes.Push(~Timestamp);
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisBridge::LeaveScope(uint64 Timestamp)
{
    if (Timestamp)
    {
	    Timestamp = ThreadInfo->PrevTimestamp += Timestamp;
    }

	State.Timing.EventTimestamp = Timestamp;

	if (ThreadInfo->ScopeRoutes.Num() > 0)
	{
		PTRINT TypeInfoPtr = ThreadInfo->ScopeRoutes.Pop(false);
		const auto* TypeInfo = (FTypeRegistry::FTypeInfo*)TypeInfoPtr;

		FEventDataInfo EmptyEventInfo = {
			nullptr,
			*TypeInfo
		};

		IAnalyzer::FOnEventContext Context = {
			(const IAnalyzer::FThreadInfo&)ThreadInfo,
			(const IAnalyzer::FEventTime&)(State.Timing),
			(const IAnalyzer::FEventData&)EmptyEventInfo,
		};

		AnalyzerHub.OnEvent(*TypeInfo, IAnalyzer::EStyle::LeaveScope, Context);
	}

	State.Timing.EventTimestamp = 0;
}



// {{{1 machine ----------------------------------------------------------------
////////////////////////////////////////////////////////////////////////////////
class FAnalysisMachine
{
public:
	enum class EStatus
	{
		Error,
		Abort,
		NotEnoughData,
		Eof,
		Continue,
	};

	struct FMachineContext
	{
		FAnalysisMachine&	Machine;
		FAnalysisBridge&	Bridge;
	};

	class FStage
	{
	public:
		typedef FAnalysisMachine::FMachineContext	FMachineContext;
		typedef FAnalysisMachine::EStatus			EStatus;

		virtual				~FStage() {}
		virtual EStatus		OnData(FStreamReader& Reader, const FMachineContext& Context) = 0;
		virtual void		EnterStage(const FMachineContext& Context) {};
		virtual void		ExitStage(const FMachineContext& Context) {};
	};

							FAnalysisMachine(FAnalysisBridge& InBridge);
							~FAnalysisMachine();
	EStatus					OnData(FStreamReader& Reader);
	void					Transition();
	template <class StageType, typename... ArgsType>
	StageType*				QueueStage(ArgsType... Args);

private:
	void					CleanUp();
	FAnalysisBridge&		Bridge;
	FStage*					ActiveStage = nullptr;
	TArray<FStage*>			StageQueue;
	TArray<FStage*>			DeadStages;
};

////////////////////////////////////////////////////////////////////////////////
FAnalysisMachine::FAnalysisMachine(FAnalysisBridge& InBridge)
: Bridge(InBridge)
{
}

////////////////////////////////////////////////////////////////////////////////
FAnalysisMachine::~FAnalysisMachine()
{
	CleanUp();
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisMachine::CleanUp()
{
	for (FStage* Stage : DeadStages)
	{
		delete Stage;
	}
	DeadStages.Reset();
}

////////////////////////////////////////////////////////////////////////////////
template <class StageType, typename... ArgsType>
StageType* FAnalysisMachine::QueueStage(ArgsType... Args)
{
	StageType* Stage = new StageType(Args...);
	StageQueue.Insert(Stage, 0);
	return Stage;
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisMachine::Transition()
{
	if (ActiveStage != nullptr)
	{
		FMachineContext Context = { *this, Bridge };
		ActiveStage->ExitStage(Context);

		DeadStages.Add(ActiveStage);
	}

	ActiveStage = (StageQueue.Num() > 0) ? StageQueue.Pop() : nullptr;

	if (ActiveStage != nullptr)
	{
		FMachineContext Context = { *this, Bridge };
		ActiveStage->EnterStage(Context);
	}
}

////////////////////////////////////////////////////////////////////////////////
FAnalysisMachine::EStatus FAnalysisMachine::OnData(FStreamReader& Reader)
{
	FMachineContext Context = { *this, Bridge };
	EStatus Ret;
	do
	{
		CleanUp();
		check(ActiveStage != nullptr);
		Ret = ActiveStage->OnData(Reader, Context);
	}
	while (Ret == EStatus::Continue);
	return Ret;
}



// {{{1 protocol-0 -------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
class FProtocol0Stage
	: public FAnalysisMachine::FStage
{
public:
						FProtocol0Stage(FTransport* InTransport);
						~FProtocol0Stage();
	virtual EStatus		OnData(FStreamReader& Reader, const FMachineContext& Context) override;
	virtual void		EnterStage(const FMachineContext& Context) override;

private:
	FTypeRegistry		TypeRegistry;
	FTransport*			Transport;
};

////////////////////////////////////////////////////////////////////////////////
FProtocol0Stage::FProtocol0Stage(FTransport* InTransport)
: Transport(InTransport)
{
}

////////////////////////////////////////////////////////////////////////////////
FProtocol0Stage::~FProtocol0Stage()
{
	delete Transport;
}

////////////////////////////////////////////////////////////////////////////////
void FProtocol0Stage::EnterStage(const FMachineContext& Context)
{
	Context.Bridge.SetActiveThread(0);
}

////////////////////////////////////////////////////////////////////////////////
FProtocol0Stage::EStatus FProtocol0Stage::OnData(
	FStreamReader& Reader,
	const FMachineContext& Context)
{
	Transport->SetReader(Reader);

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

		uint32 Uid = uint32(Header->Uid) & uint32(Protocol0::EKnownEventUids::UidMask);

		if (Uid == uint32(Protocol0::EKnownEventUids::NewEvent))
		{
			// There is no need to check size here as the runtime never builds
			// packets that fragment new-event events.
			const FTypeRegistry::FTypeInfo* TypeInfo = TypeRegistry.Add(Header->EventData);
			Context.Bridge.OnNewType(TypeInfo);
			Transport->Advance(BlockSize);
			continue;
		}

		const FTypeRegistry::FTypeInfo* TypeInfo = TypeRegistry.Get(Uid);
		if (TypeInfo == nullptr)
		{
			return EStatus::Error;
		}

		FEventDataInfo EventDataInfo = {
			Header->EventData,
			*TypeInfo,
			nullptr,
			Header->Size
		};

		Context.Bridge.OnEvent(EventDataInfo);

		Transport->Advance(BlockSize);
	}

	return Reader.IsEmpty() ? EStatus::Eof : EStatus::NotEnoughData;
}



// {{{1 protocol-2 -------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
class FProtocol2Stage
	: public FAnalysisMachine::FStage
{
public:
								FProtocol2Stage(uint32 Version, FTransport* InTransport);
								~FProtocol2Stage();
	virtual EStatus 			OnData(FStreamReader& Reader, const FMachineContext& Context) override;
	virtual void				EnterStage(const FMachineContext& Context) override;

protected:
	virtual int32				OnData(FStreamReader& Reader, FAnalysisBridge& Bridge);
	int32						OnDataAux(FStreamReader& Reader, FAuxDataCollector& Collector);
	FTypeRegistry				TypeRegistry;
	FTransport*					Transport;
	uint32						ProtocolVersion;
};

////////////////////////////////////////////////////////////////////////////////
FProtocol2Stage::FProtocol2Stage(uint32 Version, FTransport* InTransport)
: Transport(InTransport)
, ProtocolVersion(Version)
{
	switch (ProtocolVersion)
	{
	case Protocol0::EProtocol::Id:
	case Protocol1::EProtocol::Id:
	case Protocol2::EProtocol::Id:
		{
			FDispatchBuilder Dispatch;
			Dispatch.SetUid(uint16(Protocol2::EKnownEventUids::NewEvent));
			Dispatch.SetLoggerName("$Trace");
			Dispatch.SetEventName("NewEvent");
			TypeRegistry.Add(Dispatch.Finalize());
		}
		break;

	case Protocol3::EProtocol::Id:
		{
			FDispatchBuilder Dispatch;
			Dispatch.SetUid(uint16(Protocol3::EKnownEventUids::NewEvent));
			Dispatch.SetLoggerName("$Trace");
			Dispatch.SetEventName("NewEvent");
			Dispatch.SetNoSync();
			TypeRegistry.Add(Dispatch.Finalize());
		}
		break;
	}
}

////////////////////////////////////////////////////////////////////////////////
FProtocol2Stage::~FProtocol2Stage()
{
	delete Transport;
}


////////////////////////////////////////////////////////////////////////////////
void FProtocol2Stage::EnterStage(const FMachineContext& Context)
{
	auto& Serial = Context.Bridge.GetSerial();

	if (ProtocolVersion == Protocol1::EProtocol::Id)
	{
		Serial.Mask = 0x0000ffff;
	}
	else
	{
		/* Protocol2::EProtocol::Id
		   Protocol3::EProtocol::Id
		   Protocol4::EProtocol::Id */
		Serial.Mask = 0x00ffffff;
	}
}

////////////////////////////////////////////////////////////////////////////////
FProtocol2Stage::EStatus FProtocol2Stage::OnData(
	FStreamReader& Reader,
	const FMachineContext& Context)
{
	auto* InnerTransport = (FTidPacketTransport*)Transport;
	InnerTransport->SetReader(Reader);
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
		FStreamReader* ThreadReader = InnerTransport->GetThreadStream(i);
		uint32 ThreadId = InnerTransport->GetThreadId(i);
		Rota.Add({~0u, ThreadId, ThreadReader});
	}

	auto& Serial = Context.Bridge.GetSerial();
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

			Context.Bridge.SetActiveThread(RotaItem.ThreadId);

			uint32 AvailableSerial = OnData(*(RotaItem.Reader), Context.Bridge);
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
		if (int32(Serial.Value) < int32(0xc0000000))
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

	return Reader.IsEmpty() ? EStatus::Eof : EStatus::NotEnoughData;
}

////////////////////////////////////////////////////////////////////////////////
int32 FProtocol2Stage::OnData(FStreamReader& Reader, FAnalysisBridge& Bridge)
{
	auto& Serial = Bridge.GetSerial();

	while (true)
	{
		auto Mark = Reader.SaveMark();

		const auto* Header = Reader.GetPointer<Protocol2::FEventHeader>();
		if (Header == nullptr)
		{
			break;
		}

		uint32 Uid = uint32(Header->Uid) & uint32(Protocol2::EKnownEventUids::UidMask);

		const FTypeRegistry::FTypeInfo* TypeInfo = TypeRegistry.Get(Uid);
		if (TypeInfo == nullptr)
		{
			// Event-types may not to be discovered in Uid order.
			break;
		}

		uint32 BlockSize = Header->Size;

		// Make sure we consume events in the correct order
		if ((TypeInfo->Flags & FTypeRegistry::FTypeInfo::Flag_NoSync) == 0)
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
		if (TypeInfo->Flags & FTypeRegistry::FTypeInfo::Flag_MaybeHasAux)
		{
			int AuxStatus = OnDataAux(Reader, AuxCollector);
			if (AuxStatus == 0)
			{
				Reader.RestoreMark(Mark);
				break;
			}
		}

		if ((TypeInfo->Flags & FTypeRegistry::FTypeInfo::Flag_NoSync) == 0)
		{
			Serial.Value += 1;
			Serial.Value &= 0x7fffffff; // don't set msb. that has other uses
		}

		auto* EventData = (const uint8*)Header + BlockSize - Header->Size;
		if (Uid == uint32(Protocol2::EKnownEventUids::NewEvent))
		{
			// There is no need to check size here as the runtime never builds
			// packets that fragment new-event events.
			TypeInfo = TypeRegistry.Add(EventData);
			Bridge.OnNewType(TypeInfo);
		}
		else
		{
			FEventDataInfo EventDataInfo = {
				EventData,
				*TypeInfo,
				&AuxCollector,
				Header->Size,
			};

			Bridge.OnEvent(EventDataInfo);
		}
	}

	return -1;
}

////////////////////////////////////////////////////////////////////////////////
int32 FProtocol2Stage::OnDataAux(FStreamReader& Reader, FAuxDataCollector& Collector)
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



// {{{1 protocol-4 -------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
class FProtocol4Stage
	: public FProtocol2Stage
{
public:
					FProtocol4Stage(uint32 Version, FTransport* InTransport);

private:
	virtual int32	OnData(FStreamReader& Reader, FAnalysisBridge& Bridge) override;
	int32			OnDataImpl(FStreamReader& Reader, FAnalysisBridge& Bridge);
	int32			OnDataKnown(uint32 Uid, FStreamReader& Reader, FAnalysisBridge& Bridge);
};

////////////////////////////////////////////////////////////////////////////////
FProtocol4Stage::FProtocol4Stage(uint32 Version, FTransport* InTransport)
: FProtocol2Stage(Version, InTransport)
{
}

////////////////////////////////////////////////////////////////////////////////
int32 FProtocol4Stage::OnData(FStreamReader& Reader, FAnalysisBridge& Bridge)
{
	while (true)
	{
		if (int32 TriResult = OnDataImpl(Reader, Bridge))
		{
			return (TriResult < 0) ? ~TriResult : -1;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
int32 FProtocol4Stage::OnDataImpl(FStreamReader& Reader, FAnalysisBridge& Bridge)
{
	auto& Serial = Bridge.GetSerial();

	/* Returns 0 if an event was successfully processed, 1 if there's not enough
	 * data available, or ~AvailableLogSerial if the pending event is in the future */
	using namespace UE::Trace::Protocol4;

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

	if (Uid < Bridge.GetUserUidBias())
	{
		Reader.Advance(UidBytes);
		if (!OnDataKnown(Uid, Reader, Bridge))
		{
			Reader.RestoreMark(Mark);
			return 1;
		}
		return 0;
	}

	// Do we know about this event type yet?
	const FTypeRegistry::FTypeInfo* TypeInfo = TypeRegistry.Get(Uid);
	if (TypeInfo == nullptr)
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
	if ((TypeInfo->Flags & FTypeRegistry::FTypeInfo::Flag_NoSync) == 0)
	{
		if (Reader.GetPointer<FEventHeaderSync>() == nullptr)
		{
			return 1;
		}
		
		const auto* HeaderSync = (FEventHeaderSync*)Header;
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
	if (TypeInfo->Flags & FTypeRegistry::FTypeInfo::Flag_MaybeHasAux)
	{
		// Important events' size may include their array data so we need to backtrack
		auto NextMark = Reader.SaveMark();
		if (TypeInfo->Flags & FTypeRegistry::FTypeInfo::Flag_Important)
		{
			Reader.RestoreMark(Mark);
			Reader.Advance(sizeof(FEventHeader) + TypeInfo->EventSize);
		}

		int AuxStatus = OnDataAux(Reader, AuxCollector);
		if (AuxStatus == 0)
		{
			Reader.RestoreMark(Mark);
			return 1;
		}

		// User error could have resulted in less space being used that was
		// allocated for important events. So we can't assume that aux data
		// reading has read all the way up to the next event. So we use marks
		if (TypeInfo->Flags & FTypeRegistry::FTypeInfo::Flag_Important)
		{
			Reader.RestoreMark(NextMark);
		}
	}

	// Maintain sync
	if ((TypeInfo->Flags & FTypeRegistry::FTypeInfo::Flag_NoSync) == 0)
	{
		Serial.Value += 1;
		Serial.Value &= 0x7fffffff; // don't set msb. that has other uses
	}

	// Sent the event to subscribed analyzers
	FEventDataInfo EventDataInfo = {
		(const uint8*)Header + BlockSize - Header->Size,
		*TypeInfo,
		&AuxCollector,
		Header->Size,
	};

	Bridge.OnEvent(EventDataInfo);

	return 0;
}

////////////////////////////////////////////////////////////////////////////////
int32 FProtocol4Stage::OnDataKnown(
	uint32 Uid,
	FStreamReader& Reader,
	FAnalysisBridge& Bridge)
{
	using namespace UE::Trace::Protocol4;

	switch (Uid)
	{
	case EKnownEventUids::NewEvent:
		{
			const auto* Size = Reader.GetPointer<uint16>();
			const FTypeRegistry::FTypeInfo* TypeInfo = TypeRegistry.Add(Size + 1);
			Bridge.OnNewType(TypeInfo);
			Reader.Advance(sizeof(*Size) + *Size);
			return 1;
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

			uint64 Timestamp = *(uint64*)(Stamp - 1) >> 8;
			Bridge.EnterScope(Timestamp);

			Reader.Advance(sizeof(uint64) - 1);
		}
		else
		{
			Bridge.EnterScope();
		}
		return 1;

	case EKnownEventUids::LeaveScope:
	case EKnownEventUids::LeaveScope_T:
		if (Uid > EKnownEventUids::LeaveScope)
		{
			const uint8* Stamp = Reader.GetPointer(sizeof(uint64) - 1);
			if (Stamp == nullptr)
			{
				break;
			}

			uint64 Timestamp = *(uint64*)(Stamp - 1) >> 8;
			Bridge.LeaveScope(Timestamp);

			Reader.Advance(sizeof(uint64) - 1);
		}
		else
		{
			Bridge.LeaveScope();
		}
		return 1;
	};

	return 0;
}



// {{{1 est.-transport ---------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
class FEstablishTransportStage
	: public FAnalysisMachine::FStage
{
public:
	virtual EStatus OnData(FStreamReader& Reader, const FMachineContext& Context) override;
};

////////////////////////////////////////////////////////////////////////////////
FEstablishTransportStage::EStatus FEstablishTransportStage::OnData(
	FStreamReader& Reader,
	const FMachineContext& Context)
{
	using namespace UE::Trace;

	const struct {
		uint8 TransportVersion;
		uint8 ProtocolVersion;
	}* Header = decltype(Header)(Reader.GetPointer(sizeof(*Header)));
	if (Header == nullptr)
	{
		return EStatus::NotEnoughData;
	}

	FTransport* Transport = nullptr;
	switch (Header->TransportVersion)
	{
	case ETransport::Raw:		Transport = new FTransport(); break;
	case ETransport::Packet:	Transport = new FPacketTransport(); break;
	case ETransport::TidPacket:	Transport = new FTidPacketTransport(); break;
	default:					return EStatus::Error;
	}

	uint32 ProtocolVersion = Header->ProtocolVersion;
	switch (ProtocolVersion)
	{
	case Protocol0::EProtocol::Id:
		Context.Machine.QueueStage<FProtocol0Stage>(Transport);
		Context.Machine.Transition();
		break;

	case Protocol1::EProtocol::Id:
	case Protocol2::EProtocol::Id:
	case Protocol3::EProtocol::Id:
		Context.Machine.QueueStage<FProtocol2Stage>(ProtocolVersion, Transport);
		Context.Machine.Transition();
		break;

	case Protocol4::EProtocol::Id:
		Context.Machine.QueueStage<FProtocol4Stage>(ProtocolVersion, Transport);
		Context.Machine.Transition();
		break;

	default:
		return EStatus::Error;
	}

	Reader.Advance(sizeof(*Header));
	return EStatus::Continue;
}



// {{{1 metadata ---------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
class FMetadataStage
	: public FAnalysisMachine::FStage
{
public:
	virtual EStatus OnData(FStreamReader& Reader, const FMachineContext& Context) override;
};

////////////////////////////////////////////////////////////////////////////////
FMetadataStage::EStatus FMetadataStage::OnData(
	FStreamReader& Reader,
	const FMachineContext& Context)
{
	const auto* MetadataSize = Reader.GetPointer<uint16>();
	if (MetadataSize == nullptr)
	{
		return EStatus::NotEnoughData;
	}

	Reader.Advance(sizeof(*MetadataSize) + *MetadataSize);

	Context.Machine.QueueStage<FEstablishTransportStage>();
	Context.Machine.Transition();
	return EStatus::Continue;
}



// {{{1 magic ------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
class FMagicStage
	: public FAnalysisMachine::FStage
{
public:
	virtual EStatus OnData(FStreamReader& Reader, const FMachineContext& Context) override;
};

////////////////////////////////////////////////////////////////////////////////
FMagicStage::EStatus FMagicStage::OnData(
	FStreamReader& Reader,
	const FMachineContext& Context)
{
	const auto* MagicPtr = Reader.GetPointer<uint32>();
	if (MagicPtr == nullptr)
	{
		return EStatus::NotEnoughData;
	}

	uint32 Magic = *MagicPtr;

	if (Magic == 'ECRT' || Magic == '2CRT')
	{
		// Source is big-endian which we don't currently support
		return EStatus::Error;
	}

	if (Magic == 'TRCE')
	{
		Reader.Advance(sizeof(*MagicPtr));
		Context.Machine.QueueStage<FEstablishTransportStage>();
		Context.Machine.Transition();
		return EStatus::Continue;
	}

	if (Magic == 'TRC2')
	{
		Reader.Advance(sizeof(*MagicPtr));
		Context.Machine.QueueStage<FMetadataStage>();
		Context.Machine.Transition();
		return EStatus::Continue;
	}

	// There was no header on early traces so they went straight into declaring
	// protocol and transport versions.
	if (Magic == 0x00'00'00'01) // protocol 0, transport 1
	{
		Context.Machine.QueueStage<FEstablishTransportStage>();
		Context.Machine.Transition();
		return EStatus::Continue;
	}

	return EStatus::Error;
}



// {{{1 engine -----------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
class FAnalysisEngine::FImpl
{
public:
						FImpl(TArray<IAnalyzer*>&& Analyzers);
	void				Begin();
	void				End();
	bool				OnData(FStreamReader& Reader);
	FAnalysisBridge		Bridge;
	FAnalysisMachine	Machine = Bridge;
};

////////////////////////////////////////////////////////////////////////////////
FAnalysisEngine::FImpl::FImpl(TArray<IAnalyzer*>&& Analyzers)
: Bridge(Forward<TArray<IAnalyzer*>>(Analyzers))
{
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::FImpl::Begin()
{
	Machine.QueueStage<FMagicStage>();
	Machine.Transition();
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::FImpl::End()
{
	Machine.Transition();
	Bridge.Reset();
}

////////////////////////////////////////////////////////////////////////////////
bool FAnalysisEngine::FImpl::OnData(FStreamReader& Reader)
{
	return (Machine.OnData(Reader) != FAnalysisMachine::EStatus::Error);
}



////////////////////////////////////////////////////////////////////////////////
FAnalysisEngine::FAnalysisEngine(TArray<IAnalyzer*>&& Analyzers)
: Impl(new FImpl(Forward<TArray<IAnalyzer*>>(Analyzers)))
{
}

////////////////////////////////////////////////////////////////////////////////
FAnalysisEngine::~FAnalysisEngine()
{
	delete Impl;
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::Begin()
{
	Impl->Begin();
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisEngine::End()
{
	Impl->End();
}

////////////////////////////////////////////////////////////////////////////////
bool FAnalysisEngine::OnData(FStreamReader& Reader)
{
	return Impl->OnData(Reader);
}

  // }}}
} // namespace Trace
} // namespace UE

/* vim: set foldlevel=1 : */
