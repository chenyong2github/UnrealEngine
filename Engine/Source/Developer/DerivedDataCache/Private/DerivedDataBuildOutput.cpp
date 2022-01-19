// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildOutput.h"

#include "Algo/BinarySearch.h"
#include "Algo/Find.h"
#include "Containers/StringView.h"
#include "DerivedDataBuildKey.h"
#include "DerivedDataBuildPrivate.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataSharedString.h"
#include "DerivedDataValue.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"

namespace UE::DerivedData::Private
{

static const ANSICHAR GBuildOutputValueName[] = "$Output";
static const FValueId GBuildOutputValueId = FValueId::FromName(GBuildOutputValueName);

FValueId GetBuildOutputValueId()
{
	return GBuildOutputValueId;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FUtf8StringView LexToString(EBuildOutputMessageLevel Level)
{
	switch (Level)
	{
	case EBuildOutputMessageLevel::Error: return "Error"_U8SV;
	case EBuildOutputMessageLevel::Warning: return "Warning"_U8SV;
	case EBuildOutputMessageLevel::Display: return "Display"_U8SV;
	default: return "Unknown"_U8SV;
	}
}

static bool TryLexFromString(EBuildOutputMessageLevel& OutLevel, FUtf8StringView String)
{
	if (String.Equals("Error"_U8SV, ESearchCase::CaseSensitive))
	{
		OutLevel = EBuildOutputMessageLevel::Error;
		return true;
	}
	if (String.Equals("Warning"_U8SV, ESearchCase::CaseSensitive))
	{
		OutLevel = EBuildOutputMessageLevel::Warning;
		return true;
	}
	if (String.Equals("Display"_U8SV, ESearchCase::CaseSensitive))
	{
		OutLevel = EBuildOutputMessageLevel::Display;
		return true;
	}
	return false;
}

static FUtf8StringView LexToString(EBuildOutputLogLevel Level)
{
	switch (Level)
	{
	case EBuildOutputLogLevel::Error: return "Error"_U8SV;
	case EBuildOutputLogLevel::Warning: return "Warning"_U8SV;
	default: return "Unknown"_U8SV;
	}
}

static bool TryLexFromString(EBuildOutputLogLevel& OutLevel, FUtf8StringView String)
{
	if (String.Equals("Error"_U8SV, ESearchCase::CaseSensitive))
	{
		OutLevel = EBuildOutputLogLevel::Error;
		return true;
	}
	if (String.Equals("Warning"_U8SV, ESearchCase::CaseSensitive))
	{
		OutLevel = EBuildOutputLogLevel::Warning;
		return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FBuildOutputBuilderInternal final : public IBuildOutputBuilderInternal
{
public:
	explicit FBuildOutputBuilderInternal(const FSharedString& InName, const FUtf8SharedString& InFunction)
		: Name(InName)
		, Function(InFunction)
	{
		checkf(!Name.IsEmpty(), TEXT("A build output requires a non-empty name."));
		AssertValidBuildFunctionName(Function, Name);
		MessageWriter.BeginArray();
		LogWriter.BeginArray();
	}

	~FBuildOutputBuilderInternal() final = default;

	void SetMeta(FCbObject&& InMeta) final { Meta = MoveTemp(InMeta); Meta.MakeOwned(); }
	void AddValue(const FValueId& Id, const FValue& Value) final;
	void AddMessage(const FBuildOutputMessage& Message) final;
	void AddLog(const FBuildOutputLog& Log) final;
	bool HasError() const final { return bHasError; }
	FBuildOutput Build() final;

	FSharedString Name;
	FUtf8SharedString Function;
	FCbObject Meta;
	TArray<FValueWithId> Values;
	FCbWriter MessageWriter;
	FCbWriter LogWriter;
	bool bHasMessages = false;
	bool bHasLogs = false;
	bool bHasError = false;
};

class FBuildOutputInternal final : public IBuildOutputInternal
{
public:
	FBuildOutputInternal(
		FSharedString&& Name,
		FUtf8SharedString&& Function,
		FCbObject&& Meta,
		FCbObject&& Output,
		TArray<FValueWithId>&& Values);
	FBuildOutputInternal(
		const FSharedString& Name,
		const FUtf8SharedString& Function,
		const FCbObject& Output,
		bool& bOutIsValid);
	FBuildOutputInternal(
		const FSharedString& Name,
		const FUtf8SharedString& Function,
		const FCacheRecord& Output,
		bool& bOutIsValid);

	~FBuildOutputInternal() final = default;

	const FSharedString& GetName() const final { return Name; }
	const FUtf8SharedString& GetFunction() const final { return Function; }

	const FCbObject& GetMeta() const final { return Meta; }

	const FValueWithId& GetValue(const FValueId& Id) const final;
	TConstArrayView<FValueWithId> GetValues() const final { return Values; }
	TConstArrayView<FBuildOutputMessage> GetMessages() const final { return Messages; }
	TConstArrayView<FBuildOutputLog> GetLogs() const final { return Logs; }
	bool HasLogs() const final { return !Logs.IsEmpty(); }
	bool HasError() const final;

	bool TryLoad();

	void Save(FCbWriter& Writer) const final;
	void Save(FCacheRecordBuilder& RecordBuilder) const final;

	inline void AddRef() const final
	{
		ReferenceCount.fetch_add(1, std::memory_order_relaxed);
	}

	inline void Release() const final
	{
		if (ReferenceCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			delete this;
		}
	}

private:
	FSharedString Name;
	FUtf8SharedString Function;
	FCbObject Meta;
	FCbObject Output;
	TArray<FValueWithId> Values;
	TArray<FBuildOutputMessage> Messages;
	TArray<FBuildOutputLog> Logs;
	mutable std::atomic<uint32> ReferenceCount{0};
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FBuildOutputInternal::FBuildOutputInternal(
	FSharedString&& InName,
	FUtf8SharedString&& InFunction,
	FCbObject&& InMeta,
	FCbObject&& InOutput,
	TArray<FValueWithId>&& InValues)
	: Name(MoveTemp(InName))
	, Function(MoveTemp(InFunction))
	, Meta(MoveTemp(InMeta))
	, Output(MoveTemp(InOutput))
	, Values(MoveTemp(InValues))
{
	Meta.MakeOwned();
	Output.MakeOwned();
	TryLoad();
}

FBuildOutputInternal::FBuildOutputInternal(
	const FSharedString& InName,
	const FUtf8SharedString& InFunction,
	const FCbObject& InOutput,
	bool& bOutIsValid)
	: Name(InName)
	, Function(InFunction)
	, Output(InOutput)
{
	checkf(!Name.IsEmpty(), TEXT("A build output requires a non-empty name."));
	AssertValidBuildFunctionName(Function, Name);
	Output.MakeOwned();
	FCbField MetaField = InOutput["Meta"_ASV];
	bOutIsValid = TryLoad() && (!MetaField || MetaField.IsObject());
	Meta = MoveTemp(MetaField).AsObject();
}

FBuildOutputInternal::FBuildOutputInternal(
	const FSharedString& InName,
	const FUtf8SharedString& InFunction,
	const FCacheRecord& InOutput,
	bool& bOutIsValid)
	: Name(InName)
	, Function(InFunction)
	, Meta(InOutput.GetMeta())
	, Output(InOutput.GetValue(GBuildOutputValueId).GetData().Decompress())
	, Values(InOutput.GetValues())
{
	checkf(!Name.IsEmpty(), TEXT("A build output requires a non-empty name."));
	AssertValidBuildFunctionName(Function, Name);
	Values.RemoveAll([](const FValueWithId& Value) { return Value.GetId() == GBuildOutputValueId; });
	bOutIsValid = TryLoad();
}

const FValueWithId& FBuildOutputInternal::GetValue(const FValueId& Id) const
{
	const int32 Index = Algo::BinarySearchBy(Values, Id, &FValueWithId::GetId);
	return Values.IsValidIndex(Index) ? Values[Index] : FValueWithId::Null;
}

bool FBuildOutputInternal::HasError() const
{
	return Algo::FindBy(Messages, EBuildOutputMessageLevel::Error, &FBuildOutputMessage::Level) ||
		Algo::FindBy(Logs, EBuildOutputLogLevel::Error, &FBuildOutputLog::Level);
}

bool FBuildOutputInternal::TryLoad()
{
	const FCbObjectView OutputView = Output;

	if (Values.IsEmpty())
	{
		for (FCbFieldView Value : OutputView["Values"_ASV])
		{
			const FValueId Id = Value["Id"_ASV].AsObjectId();
			const FIoHash& RawHash = Value["RawHash"_ASV].AsAttachment();
			const uint64 RawSize = Value["RawSize"_ASV].AsUInt64(MAX_uint64);
			if (Id.IsNull() || RawHash.IsZero() || RawSize == MAX_uint64)
			{
				return false;
			}
			Values.Emplace(Id, RawHash, RawSize);
		}
	}

	if (FCbFieldView MessagesField = OutputView["Messages"_ASV])
	{
		if (!MessagesField.IsArray())
		{
			return false;
		}
		Messages.Reserve(MessagesField.AsArrayView().Num());
		for (FCbFieldView MessageField : MessagesField)
		{
			const FUtf8StringView LevelName = MessageField["Level"_ASV].AsString();
			const FUtf8StringView Message = MessageField["Message"_ASV].AsString();
			EBuildOutputMessageLevel Level;
			if (LevelName.IsEmpty() || Message.IsEmpty() || !TryLexFromString(Level, LevelName))
			{
				return false;
			}
			Messages.Add({Message, Level});
		}
	}

	if (FCbFieldView LogsField = OutputView["Logs"_ASV])
	{
		if (!LogsField.IsArray())
		{
			return false;
		}
		Logs.Reserve(LogsField.AsArrayView().Num());
		for (FCbFieldView LogField : LogsField)
		{
			const FUtf8StringView LevelName = LogField["Level"_ASV].AsString();
			const FUtf8StringView Category = LogField["Category"_ASV].AsString();
			const FUtf8StringView Message = LogField["Message"_ASV].AsString();
			EBuildOutputLogLevel Level;
			if (LevelName.IsEmpty() || Category.IsEmpty() || Message.IsEmpty() || !TryLexFromString(Level, LevelName))
			{
				return false;
			}
			Logs.Add({Category, Message, Level});
		}
	}

	return true;
}

void FBuildOutputInternal::Save(FCbWriter& Writer) const
{
	Writer.BeginObject();
	if (!Values.IsEmpty())
	{
		Writer.BeginArray("Values"_ASV);
		for (const FValueWithId& Value : Values)
		{
			Writer.BeginObject();
			Writer.AddObjectId("Id"_ASV, Value.GetId());
			Writer.AddBinaryAttachment("RawHash"_ASV, Value.GetRawHash());
			Writer.AddInteger("RawSize"_ASV, Value.GetRawSize());
			Writer.EndObject();
		}
		Writer.EndArray();
	}
	if (FCbField MessagesField = Output["Messages"_ASV])
	{
		Writer.AddField("Messages"_ASV, MessagesField);
	}
	if (FCbField LogsField = Output["Logs"_ASV])
	{
		Writer.AddField("Logs"_ASV, LogsField);
	}
	if (Meta)
	{
		Writer.AddObject("Meta"_ASV, Meta);
	}
	Writer.EndObject();
}

void FBuildOutputInternal::Save(FCacheRecordBuilder& RecordBuilder) const
{
	RecordBuilder.SetMeta(FCbObject(Meta));
	if (!Messages.IsEmpty() || !Logs.IsEmpty())
	{
		TCbWriter<1024> Writer;
		if (FCbField MessagesField = Output["Messages"_ASV])
		{
			Writer.AddField("Messages"_ASV, MessagesField);
		}
		if (FCbField LogsField = Output["Logs"_ASV])
		{
			Writer.AddField("Logs"_ASV, LogsField);
		}
		RecordBuilder.AddValue(GBuildOutputValueId, Writer.Save().GetBuffer());
	}
	for (const FValueWithId& Value : Values)
	{
		RecordBuilder.AddValue(Value);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FBuildOutputBuilderInternal::AddValue(const FValueId& Id, const FValue& Value)
{
	checkf(Id.IsValid(), TEXT("Null value added in output for build of '%s' by %s."),
		*Name, *WriteToString<32>(Function));
	checkf(Id != GBuildOutputValueId,
		TEXT("Value added with reserved ID %s from name '%hs' for build of '%s' by %s."),
		*WriteToString<32>(Id), GBuildOutputValueName, *Name, *WriteToString<32>(Function));
	const int32 Index = Algo::LowerBoundBy(Values, Id, &FValueWithId::GetId);
	checkf(!(Values.IsValidIndex(Index) && Values[Index].GetId() == Id),
		TEXT("Duplicate ID %s used by value for build of '%s' by %s."),
		*WriteToString<32>(Id), *Name, *WriteToString<32>(Function));
	Values.Insert(FValueWithId(Id, Value), Index);
}

void FBuildOutputBuilderInternal::AddMessage(const FBuildOutputMessage& Message)
{
	bHasError |= Message.Level == EBuildOutputMessageLevel::Error;
	bHasMessages = true;
	MessageWriter.BeginObject();
	MessageWriter.AddString("Level"_ASV, LexToString(Message.Level));
	MessageWriter.AddString("Message"_ASV, Message.Message);
	MessageWriter.EndObject();
}

void FBuildOutputBuilderInternal::AddLog(const FBuildOutputLog& Log)
{
	bHasError |= Log.Level == EBuildOutputLogLevel::Error;
	bHasLogs = true;
	LogWriter.BeginObject();
	LogWriter.AddString("Level"_ASV, LexToString(Log.Level));
	LogWriter.AddString("Category"_ASV, Log.Category);
	LogWriter.AddString("Message"_ASV, Log.Message);
	LogWriter.EndObject();
}

FBuildOutput FBuildOutputBuilderInternal::Build()
{
	if (bHasError)
	{
		Values.Empty();
	}
	MessageWriter.EndArray();
	LogWriter.EndArray();
	FCbObject Output;
	if (bHasMessages || bHasLogs)
	{
		TCbWriter<1024> Writer;
		Writer.BeginObject();
		if (bHasMessages)
		{
			Writer.AddArray("Messages"_ASV, MessageWriter.Save().AsArray());
			MessageWriter.Reset();
		}
		if (bHasLogs)
		{
			Writer.AddArray("Logs"_ASV, LogWriter.Save().AsArray());
			LogWriter.Reset();
		}
		Writer.EndObject();
		Output = Writer.Save().AsObject();
	}
	return CreateBuildOutput(new FBuildOutputInternal(MoveTemp(Name), MoveTemp(Function), MoveTemp(Meta), MoveTemp(Output), MoveTemp(Values)));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FBuildOutput CreateBuildOutput(IBuildOutputInternal* Output)
{
	return FBuildOutput(Output);
}

FBuildOutputBuilder CreateBuildOutputBuilder(IBuildOutputBuilderInternal* OutputBuilder)
{
	return FBuildOutputBuilder(OutputBuilder);
}

FBuildOutputBuilder CreateBuildOutput(const FSharedString& Name, const FUtf8SharedString& Function)
{
	return CreateBuildOutputBuilder(new FBuildOutputBuilderInternal(Name, Function));
}

} // UE::DerivedData::Private

namespace UE::DerivedData
{

FOptionalBuildOutput FBuildOutput::Load(
	const FSharedString& Name,
	const FUtf8SharedString& Function,
	const FCbObject& Output)
{
	bool bIsValid = false;
	FOptionalBuildOutput Out = Private::CreateBuildOutput(
		new Private::FBuildOutputInternal(Name, Function, Output, bIsValid));
	if (!bIsValid)
	{
		Out.Reset();
	}
	return Out;
}

FOptionalBuildOutput FBuildOutput::Load(
	const FSharedString& Name,
	const FUtf8SharedString& Function,
	const FCacheRecord& Output)
{
	bool bIsValid = false;
	FOptionalBuildOutput Out = Private::CreateBuildOutput(
		new Private::FBuildOutputInternal(Name, Function, Output, bIsValid));
	if (!bIsValid)
	{
		Out.Reset();
	}
	return Out;
}

} // UE::DerivedData
