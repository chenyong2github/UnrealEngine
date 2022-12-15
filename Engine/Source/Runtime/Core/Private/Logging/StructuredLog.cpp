// Copyright Epic Games, Inc. All Rights Reserved.

#include "Logging/StructuredLog.h"

#include "Algo/Accumulate.h"
#include "Algo/AllOf.h"
#include "Algo/NoneOf.h"
#include "Containers/StringConv.h"
#include "Containers/StringView.h"
#include "CoreGlobals.h"
#include "HAL/PlatformTime.h"
#include "Logging/LogTrace.h"
#include "Misc/AsciiSet.h"
#include "Misc/DateTime.h"
#include "Misc/FeedbackContext.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeExit.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryValue.h"
#include "Serialization/VarInt.h"

namespace UE::Logging::Private
{

static constexpr FAsciiSet ValidLogFieldName("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FLogTemplateOp
{
	enum EOpCode : int32 { OpEnd, OpSkip, OpText, OpName, OpIndex, OpCount };

	static constexpr int32 ValueShift = 3;
	static_assert(OpCount <= (1 << ValueShift));

	EOpCode Code = OpEnd;
	int32 Value = 0;

	inline int32 GetSkipSize() const { return Code == OpIndex ? 0 : Value; }

	static inline FLogTemplateOp Load(const uint8*& Data);
	static inline uint32 SaveSize(const FLogTemplateOp& Op) { return MeasureVarUInt(Encode(Op)); }
	static inline void Save(const FLogTemplateOp& Op, uint8*& Data);
	static inline uint64 Encode(const FLogTemplateOp& Op) { return uint64(Op.Code) | (uint64(Op.Value) << ValueShift); }
	static inline FLogTemplateOp Decode(uint64 Value) { return {EOpCode(Value & ((1 << ValueShift) - 1)), int32(Value >> ValueShift)}; }
};

inline FLogTemplateOp FLogTemplateOp::Load(const uint8*& Data)
{
	uint32 ByteCount = 0;
	ON_SCOPE_EXIT { Data += ByteCount; };
	return Decode(ReadVarUInt(Data, ByteCount));
}

inline void FLogTemplateOp::Save(const FLogTemplateOp& Op, uint8*& Data)
{
	Data += WriteVarUInt(Encode(Op), Data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename CharType>
struct TLogFieldValueConstants;

template <>
struct TLogFieldValueConstants<UTF8CHAR>
{
	static inline const FAnsiStringView Null = ANSITEXTVIEW("null");
	static inline const FAnsiStringView True = ANSITEXTVIEW("true");
	static inline const FAnsiStringView False = ANSITEXTVIEW("false");
};

template <>
struct TLogFieldValueConstants<WIDECHAR>
{
	static inline const FWideStringView Null = WIDETEXTVIEW("null");
	static inline const FWideStringView True = WIDETEXTVIEW("true");
	static inline const FWideStringView False = WIDETEXTVIEW("false");
};

template <typename CharType>
static void LogFieldValue(TStringBuilderBase<CharType>& Out, const FCbFieldView& Field)
{
	using FConstants = TLogFieldValueConstants<CharType>;
	switch (FCbValue Accessor = Field.GetValue(); Accessor.GetType())
	{
	case ECbFieldType::Null:
		Out.Append(FConstants::Null);
		break;
	case ECbFieldType::Object:
	case ECbFieldType::UniformObject:
	case ECbFieldType::Array:
	case ECbFieldType::UniformArray:
	case ECbFieldType::Binary:
		CompactBinaryToCompactJson(Field, Out);
		break;
	case ECbFieldType::String:
		Out.Append(Accessor.AsString());
		break;
	case ECbFieldType::IntegerPositive:
		Out << Accessor.AsIntegerPositive();
		break;
	case ECbFieldType::IntegerNegative:
		Out << Accessor.AsIntegerNegative();
		break;
	case ECbFieldType::Float32:
	case ECbFieldType::Float64:
		CompactBinaryToCompactJson(Field, Out);
		break;
	case ECbFieldType::BoolFalse:
		Out.Append(FConstants::False);
		break;
	case ECbFieldType::BoolTrue:
		Out.Append(FConstants::True);
		break;
	case ECbFieldType::ObjectAttachment:
	case ECbFieldType::BinaryAttachment:
		Out << Accessor.AsAttachment();
		break;
	case ECbFieldType::Hash:
		Out << Accessor.AsHash();
		break;
	case ECbFieldType::Uuid:
		Out << Accessor.AsUuid();
		break;
	case ECbFieldType::DateTime:
		Out << FDateTime(Accessor.AsDateTimeTicks()).ToIso8601();
		break;
	case ECbFieldType::TimeSpan:
	{
		const FTimespan Span(Accessor.AsTimeSpanTicks());
		if (Span.GetDays() == 0)
		{
			Out << Span.ToString(TEXT("%h:%m:%s.%n"));
		}
		else
		{
			Out << Span.ToString(TEXT("%d.%h:%m:%s.%n"));
		}
		break;
	}
	case ECbFieldType::ObjectId:
		Out << Accessor.AsObjectId();
		break;
	case ECbFieldType::CustomById:
	case ECbFieldType::CustomByName:
		CompactBinaryToCompactJson(Field, Out);
		break;
	default:
		checkNoEntry();
		break;
	}
}

} // UE::Logging::Private

namespace UE
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FLogTemplate
{
	using FLogField = Logging::Private::FLogField;

public:
	static FLogTemplate* Create(const TCHAR* Format, const FLogField* Fields = nullptr, int32 FieldCount = 0);
	static void Destroy(FLogTemplate* Template);

	template <typename CharType>
	void FormatTo(TStringBuilderBase<CharType>& Out, const TCHAR* Format, const FCbObjectView& Fields) const;

private:
	FLogTemplate() = default;
	~FLogTemplate() = default;
	FLogTemplate(const FLogTemplate&) = delete;
	FLogTemplate& operator=(const FLogTemplate&) = delete;
};

FLogTemplate* FLogTemplate::Create(const TCHAR* Format, const FLogField* Fields, const int32 FieldCount)
{
	using namespace Logging::Private;

	const TConstArrayView<FLogField> FieldsView(Fields, FieldCount);
	const bool bFindFields = !!Fields;
	const bool bPositional = !FieldCount || Algo::NoneOf(FieldsView, &FLogField::Name);
	checkf(bPositional || Algo::AllOf(FieldsView, &FLogField::Name),
		TEXT("Log fields must be entirely named or entirely anonymous. [[%s]]"), Format);
	checkf(bPositional || Algo::AllOf(FieldsView,
		[](const FLogField& Field) { return *Field.Name && FAsciiSet::HasOnly(Field.Name, ValidLogFieldName); }),
		TEXT("Log field names must match \"[A-Za-z0-9_]+\" in [[%s]]."), Format);

	TArray<FLogTemplateOp, TInlineAllocator<16>> Ops;

	int32 FormatFieldCount = 0;
	int32 BracketSearchOffset = 0;
	for (const TCHAR* TextStart = Format;;)
	{
		constexpr FAsciiSet Brackets("{}");
		const TCHAR* const TextEnd = FAsciiSet::FindFirstOrEnd(TextStart + BracketSearchOffset, Brackets);
		BracketSearchOffset = 0;

		// Escaped "{{" or "}}"
		if ((TextEnd[0] == TEXT('{') && TextEnd[1] == TEXT('{')) ||
			(TextEnd[0] == TEXT('}') && TextEnd[1] == TEXT('}')))
		{
			// Only "{{" or "}}"
			if (TextStart == TextEnd)
			{
				Ops.Add({FLogTemplateOp::OpSkip, 1});
				TextStart = TextEnd + 1;
				BracketSearchOffset = 1;
			}
			// Text and "{{" or "}}"
			else
			{
				Ops.Add({FLogTemplateOp::OpText, UE_PTRDIFF_TO_INT32(1 + TextEnd - TextStart)});
				Ops.Add({FLogTemplateOp::OpSkip, 1});
				TextStart = TextEnd + 2;
			}
			continue;
		}

		// Text
		if (TextStart != TextEnd)
		{
			Ops.Add({FLogTemplateOp::OpText, UE_PTRDIFF_TO_INT32(TextEnd - TextStart)});
			TextStart = TextEnd;
		}

		// End
		if (!*TextEnd)
		{
			Ops.Add({FLogTemplateOp::OpEnd});
			break;
		}

		// Invalid '}'
		checkf(*TextStart == TEXT('{'), TEXT("Log format has an unexpected '}' character. Use '}}' to escape it. [[%s]]"), Format);

		// Field
		const TCHAR* const FieldStart = TextStart;
		const TCHAR* const FieldNameEnd = FAsciiSet::Skip(FieldStart + 1, ValidLogFieldName);
		checkf(*FieldNameEnd, TEXT("Log format has an unterminated field reference. Use '{{' to escape '{' if needed. [[%s]]"), Format);
		checkf(*FieldNameEnd == TEXT('}'), TEXT("Log format has invalid character '%c' in field name. Use '{{' to escape '{' if needed. Names must match \"[A-Za-z0-9_]+\". [[%s]]"), *FieldNameEnd, Format);
		const TCHAR* const FieldEnd = FieldNameEnd + 1;
		const int32 FieldLen = UE_PTRDIFF_TO_INT32(FieldEnd - FieldStart);
		const int32 FieldNameLen = FieldLen - 2;

		if (bFindFields && !bPositional)
		{
			bool bFoundField = false;
			for (int32 BaseFieldIndex = 0; BaseFieldIndex < FieldCount; ++BaseFieldIndex)
			{
				const int32 FieldIndex = (FormatFieldCount + BaseFieldIndex) % FieldCount;
				const ANSICHAR* FieldName = Fields[FieldIndex].Name;
				if (FPlatformString::Strncmp(FieldName, FieldStart + 1, FieldNameLen) == 0 && !FieldName[FieldNameLen])
				{
					Ops.Add({FLogTemplateOp::OpIndex, FieldIndex});
					bFoundField = true;
					break;
				}
			}
			checkf(bFoundField, TEXT("Log format requires field '%.*s' which was not provided. [[%s]]"),
				FieldNameLen, FieldStart + 1, Format);
		}

		Ops.Add({FLogTemplateOp::OpName, FieldLen});
		++FormatFieldCount;

		TextStart = FieldEnd;
	}

	checkf(!bFindFields || !bPositional || FormatFieldCount == FieldCount,
		TEXT("Log format requires %d fields and %d were provided. [[%s]]"), FormatFieldCount, FieldCount);

	const uint32 TotalSize = Algo::TransformAccumulate(Ops, FLogTemplateOp::SaveSize, 0);
	FLogTemplate* const Template = (FLogTemplate*)FMemory::Malloc(TotalSize);
	uint8* Data = (uint8*)Template;
	for (const FLogTemplateOp& Op : Ops)
	{
		FLogTemplateOp::Save(Op, Data);
	}
	return Template;
}

void FLogTemplate::Destroy(FLogTemplate* Template)
{
	FMemory::Free(Template);
}

template <typename CharType>
void FLogTemplate::FormatTo(TStringBuilderBase<CharType>& Out, const TCHAR* Format, const FCbObjectView& Fields) const
{
	using namespace Logging::Private;

	auto FindField = [&Fields, It = Fields.CreateViewIterator(), Index = 0, Format](FAnsiStringView Name, int32 IndexHint = -1) mutable -> FCbFieldView&
	{
		if (IndexHint >= 0)
		{
			for (; Index < IndexHint && It; ++Index, ++It)
			{
			}
			if (IndexHint < Index)
			{
				It = Fields.CreateViewIterator();
				for (Index = 0; Index < IndexHint && It; ++Index, ++It);
			}
			if (IndexHint == Index && Name.Equals(It.GetName()))
			{
				return It;
			}
		}
		const int32 PrevIndex = Index;
		for (; It; ++Index, ++It)
		{
			if (Name.Equals(It.GetName()))
			{
				return It;
			}
		}
		It = Fields.CreateViewIterator();
		for (Index = 0; Index < PrevIndex && It; ++Index, ++It)
		{
			if (Name.Equals(It.GetName()))
			{
				return It;
			}
		}
		checkf(false, TEXT("Log format requires field '%.*hs' which was not provided. [[%s]]"), Name.Len(), Name.GetData(), Format);
		return It;
	};

	int32 FieldIndexHint = -1;
	const uint8* NextOp = (const uint8*)this;
	const TCHAR* NextFormat = Format;
	for (;;)
	{
		const FLogTemplateOp Op = FLogTemplateOp::Load(NextOp);
		switch (Op.Code)
		{
		case FLogTemplateOp::OpEnd:
			return;
		case FLogTemplateOp::OpText:
			Out.Append(NextFormat, Op.Value);
			break;
		case FLogTemplateOp::OpIndex:
			FieldIndexHint = Op.Value;
			break;
		case FLogTemplateOp::OpName:
			const auto Name = StringCast<ANSICHAR>(NextFormat + 1, Op.Value - 2);
			LogFieldValue(Out, FindField(MakeStringView(Name.Get(), Name.Length()), FieldIndexHint));
			FieldIndexHint = -1;
			break;
		}
		NextFormat += Op.GetSkipSize();
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FLogTime FLogTime::Now()
{
	FLogTime Time;
	Time.UtcTicks = FDateTime::UtcNow().GetTicks();
	Time.Cycles = FPlatformTime::Cycles64();
	return Time;
}

FDateTime FLogTime::GetUtcTime() const
{
	return FDateTime(UtcTicks);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename CharType>
static void FormatRecordMessageTo(TStringBuilderBase<CharType>& Out, const FLogRecord& Record)
{
	const TCHAR* Format = Record.GetFormat();
	if (UNLIKELY(!Format))
	{
		return;
	}

	const FLogTemplate* Template = Record.GetTemplate();
	if (LIKELY(Template))
	{
		return Template->FormatTo(Out, Format, Record.GetFields());
	}

	FLogTemplate* LocalTemplate = FLogTemplate::Create(Format);
	LocalTemplate->FormatTo(Out, Format, Record.GetFields());
	FLogTemplate::Destroy(LocalTemplate);
}

void FLogRecord::FormatMessageTo(FUtf8StringBuilderBase& Out) const
{
	FormatRecordMessageTo(Out, *this);
}

void FLogRecord::FormatMessageTo(FWideStringBuilderBase& Out) const
{
	FormatRecordMessageTo(Out, *this);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // UE

#if !NO_LOGGING

namespace UE::Logging::Private
{

class FLogTemplateFieldIterator
{
public:
	inline FLogTemplateFieldIterator(const FLogTemplate* Template, const TCHAR* Format)
		: NextOp((const uint8*)Template)
		, NextFormat(Format)
	{
		++*this;
	}

	FLogTemplateFieldIterator& operator++();
	inline explicit operator bool() const { return !!NextOp; }
	inline const FStringView& GetName() const { return Name; }

private:
	FStringView Name;
	const uint8* NextOp = nullptr;
	const TCHAR* NextFormat = nullptr;
};

FLogTemplateFieldIterator& FLogTemplateFieldIterator::operator++()
{
	using namespace Logging::Private;

	while (NextOp)
	{
		FLogTemplateOp Op = FLogTemplateOp::Load(NextOp);
		if (Op.Code == FLogTemplateOp::OpName)
		{
			Name = FStringView(NextFormat + 1, Op.Value - 2);
			NextFormat += Op.GetSkipSize();
			return *this;
		}
		if (Op.Code == FLogTemplateOp::OpEnd)
		{
			break;
		}
		NextFormat += Op.GetSkipSize();
	}

	NextOp = nullptr;
	Name.Reset();
	return *this;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FStaticLogDynamicDataManager
{
	std::atomic<FStaticLogDynamicData*> Head = nullptr;

	~FStaticLogDynamicDataManager()
	{
		for (FStaticLogDynamicData* Data = Head.exchange(nullptr); Data; Data = Data->Next)
		{
			if (FLogTemplate* Template = Data->Template.exchange(nullptr))
			{
				FLogTemplate::Destroy(Template);
			}
		}
	}
};

static FStaticLogDynamicDataManager GStaticLogDynamicDataManager;

// Tracing the log happens in its own function because that allows stack space for the message to
// be returned before calling into the output devices.
FORCENOINLINE void LogToTrace(const FStaticLogRecord& Log, const FLogRecord& Record)
{
#if LOGTRACE_ENABLED
	TStringBuilder<1024> Message;
	Record.FormatMessageTo(Message);
	FLogTrace::OutputLogMessageSimple(&Log, TEXT("%s"), *Message);
#endif
}

// Serializing log fields to compact binary happens in its own function because that allows stack
// space for the writer to be returned before calling into the output devices.
FORCENOINLINE FCbObject SerializeLogFields(
	const FStaticLogRecord& Log,
	const FLogTemplate& Template,
	const FLogField* Fields,
	const int32 FieldCount)
{
	if (FieldCount == 0)
	{
		return FCbObject();
	}

	TCbWriter<1024> Writer;
	Writer.BeginObject();

	// Anonymous. Extract names from Template.
	if (!Fields->Name)
	{
		FLogTemplateFieldIterator It(&Template, Log.Format);
		for (const FLogField* FieldsEnd = Fields + FieldCount; Fields != FieldsEnd; ++Fields, ++It)
		{
			check(It);
			const auto Name = StringCast<ANSICHAR>(It.GetName().GetData(), It.GetName().Len());
			Fields->WriteValue(Writer.SetName(MakeStringView(Name.Get(), Name.Length())), Fields->Value);
		}
		check(!It);
	}
	// Named
	else
	{
		for (const FLogField* FieldsEnd = Fields + FieldCount; Fields != FieldsEnd; ++Fields)
		{
			Fields->WriteValue(Writer.SetName(Fields->Name), Fields->Value);
		}
	}

	Writer.EndObject();
	return Writer.Save().AsObject();
}

FORCENOINLINE FLogTemplate& CreateLogTemplate(const FStaticLogRecord& Log, const FLogField* Fields, const int32 FieldCount)
{
#if LOGTRACE_ENABLED
	FLogTrace::OutputLogMessageSpec(&Log, &Log.Category, Log.Verbosity, Log.File, Log.Line, TEXT("%s"));
#endif

	FLogTemplate* NewTemplate = FLogTemplate::Create(Log.Format, Fields, FieldCount);
	if (FLogTemplate* ExistingTemplate = nullptr;
		UNLIKELY(!Log.DynamicData.Template.compare_exchange_strong(ExistingTemplate, NewTemplate, std::memory_order_release, std::memory_order_acquire)))
	{
		FLogTemplate::Destroy(NewTemplate);
		return *ExistingTemplate;
	}

	// Register the template to destroy on exit.
	for (FStaticLogDynamicData* Head = GStaticLogDynamicDataManager.Head.load(std::memory_order_relaxed);;)
	{
		Log.DynamicData.Next = Head;
		if (GStaticLogDynamicDataManager.Head.compare_exchange_weak(Head, &Log.DynamicData, std::memory_order_release, std::memory_order_relaxed))
		{
			break;
		}
	}

	return *NewTemplate;
}

inline FLogTemplate& EnsureLogTemplate(const FStaticLogRecord& Log, const FLogField* Fields, const int32 FieldCount)
{
	if (FLogTemplate* Template = Log.DynamicData.Template.load(std::memory_order_acquire))
	{
		return *Template;
	}
	return CreateLogTemplate(Log, Fields, FieldCount);
}

void LogWithFieldArray(const FStaticLogRecord& Log, const FLogField* Fields, const int32 FieldCount)
{
	FLogTemplate& Template = EnsureLogTemplate(Log, Fields, FieldCount);

	FLogRecord Record;
	Record.SetFormat(Log.Format);
	Record.SetTemplate(&Template);
	Record.SetFields(SerializeLogFields(Log, Template, Fields, FieldCount));
	Record.SetFile(Log.File);
	Record.SetLine(Log.Line);
	Record.SetCategory(Log.Category.GetCategoryName());
	Record.SetVerbosity(Log.Verbosity);
	Record.SetTime(FLogTime::Now());

#if LOGTRACE_ENABLED
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(LogChannel))
	{
		LogToTrace(Log, Record);
	}
#endif

	FOutputDevice* LogOverride = nullptr;
	switch (Log.Verbosity)
	{
	case ELogVerbosity::Error:
	case ELogVerbosity::Warning:
	case ELogVerbosity::Display:
		return GWarn->SerializeRecord(Record);
	default:
		return GLog->SerializeRecord(Record);
	}
}

void LogWithNoFields(const FStaticLogRecord& Log)
{
	// A non-null field pointer enables field validation in FLogTemplate::Create.
	static constexpr FLogField EmptyField{};
	LogWithFieldArray(Log, &EmptyField, 0);
}

} // UE::Logging::Private

#endif // !NO_LOGGING
