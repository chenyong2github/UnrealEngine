// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CompactBinaryWriter.h"

#include "Containers/StringConv.h"
#include "Misc/Blake3.h"
#include "Misc/ByteSwap.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Timespan.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/VarInt.h"

enum class FCbWriter::EStateFlags : uint8
{
	None = 0,
	/** Whether a name has been written for the current field. */
	Name = 1 << 0,
	/** Whether this state is in the process of writing a field. */
	Field = 1 << 1,
	/** Whether this state is for array fields. */
	Array = 1 << 2,
	/** Whether this state is for object fields. */
	Object = 1 << 3,
};

ENUM_CLASS_FLAGS(FCbWriter::EStateFlags);

/** Whether the field type can be used in a uniform array or uniform object. */
static constexpr bool IsUniformType(const ECbFieldType Type)
{
	if (FCbFieldType::HasFieldName(Type))
	{
		return true;
	}

	switch (Type)
	{
	case ECbFieldType::None:
	case ECbFieldType::Null:
	case ECbFieldType::BoolFalse:
	case ECbFieldType::BoolTrue:
		return false;
	default:
		return true;
	}
}

/** Append the payload from the compact binary value to the array and return its type. */
template <typename T>
static inline ECbFieldType AppendCompactBinary(const T& Value, TArray64<uint8>& OutData)
{
	struct FCopy : public T
	{
		using T::GetCopyType;
		using T::GetPayloadView;
	};
	const FCopy& ValueCopy = static_cast<const FCopy&>(Value);
	const FConstMemoryView SourceView = ValueCopy.GetPayloadView();
	const int64 TargetOffset = OutData.AddUninitialized(SourceView.GetSize());
	FMemory::Memcpy(OutData.GetData() + TargetOffset, SourceView.GetData(), SourceView.GetSize());
	return FCbFieldType::GetType(ValueCopy.GetCopyType());
}

FCbWriter::FCbWriter()
{
	States.Emplace();
}

FCbWriter::FCbWriter(const int64 InitialSize)
	: FCbWriter()
{
	Data.Reserve(InitialSize);
}

FCbWriter::~FCbWriter()
{
}

void FCbWriter::Reset()
{
	Data.Reset();
	States.Reset();
	States.Emplace();
}

FCbFieldRefIterator FCbWriter::Save() const
{
	const uint64 Size = SaveSize();
	FSharedBufferPtr Buffer = FSharedBuffer::Alloc(Size);
	const FCbFieldIterator Output = SaveToMemory(Buffer->GetView());
	return FCbFieldRefIterator(Output, FSharedBuffer::MakeReadOnly(MoveTemp(Buffer)));
}

FCbFieldIterator FCbWriter::SaveToMemory(const FMutableMemoryView Buffer) const
{
	checkf(States.Num() == 1 && States.Last().Flags == EStateFlags::None,
		TEXT("It is invalid to save while there are incomplete write operations."));
	checkf(Data.Num() > 0, TEXT("It is invalid to save when nothing has been written."));
	checkf(Buffer.GetSize() == Data.Num(),
		TEXT("Buffer is %" UINT64_FMT " bytes but %" INT64_FMT " is required."), Buffer.GetSize(), Data.Num());
	FMemory::Memcpy(Buffer.GetData(), Data.GetData(), Data.Num());
	return FCbFieldIterator(FCbField(Buffer.GetData()), Buffer.GetDataEnd());
}

uint64 FCbWriter::SaveSize() const
{
	return Data.Num();
}

void FCbWriter::BeginField()
{
	FState& State = States.Last();
	if ((State.Flags & EStateFlags::Field) == EStateFlags::None)
	{
		State.Flags |= EStateFlags::Field;
		State.Offset = Data.AddZeroed(sizeof(ECbFieldType));
	}
	else
	{
		checkf((State.Flags & EStateFlags::Name) == EStateFlags::Name,
			TEXT("A new field cannot be written until the previous field '%.*hs' is finished."),
			CurrentName().Len(), CurrentName().GetData());
	}
}

void FCbWriter::EndField(ECbFieldType Type)
{
	FState& State = States.Last();

	if ((State.Flags & EStateFlags::Name) == EStateFlags::Name)
	{
		Type |= ECbFieldType::HasFieldName;
	}
	else
	{
		checkf((State.Flags & EStateFlags::Object) == EStateFlags::None,
			TEXT("It is invalid to write an object field without a unique non-empty name."));
	}

	if (State.Count == 0)
	{
		State.UniformType = Type;
	}
	else if (State.UniformType != Type)
	{
		State.UniformType = ECbFieldType::None;
	}

	State.Flags &= ~(EStateFlags::Name | EStateFlags::Field);
	++State.Count;
	Data[State.Offset] = uint8(Type);
}

FCbWriter& FCbWriter::Name(const FAnsiStringView Name)
{
	FState& State = States.Last();
	checkf((State.Flags & EStateFlags::Array) != EStateFlags::Array,
		TEXT("It is invalid to write a name for an array field. Name '%.*hs'"), Name.Len(), Name.GetData());
	checkf(!Name.IsEmpty(), TEXT("%s"), (State.Flags & EStateFlags::Object) == EStateFlags::Object
		? TEXT("It is invalid to write an empty name for an object field. Specify a unique non-empty name.")
		: TEXT("It is invalid to write an empty name for a top-level field. Specify a name or avoid this call."));
	checkf((State.Flags & (EStateFlags::Name | EStateFlags::Field)) == EStateFlags::None,
		TEXT("A new field '%.*hs' cannot be written until the previous field '%.*hs' is finished."),
		Name.Len(), Name.GetData(), CurrentName().Len(), CurrentName().GetData());

	BeginField();
	State.Flags |= EStateFlags::Name;
	const uint32 NameLenByteCount = MeasureVarUInt(uint32(Name.Len()));
	const int64 NameLenOffset = Data.AddUninitialized(NameLenByteCount);
	WriteVarUInt(uint64(Name.Len()), Data.GetData() + NameLenOffset);
	Data.Append(reinterpret_cast<const uint8*>(Name.GetData()), Name.Len());
	return *this;
}

void FCbWriter::NameOrString(const FAnsiStringView NameOrValue)
{
	// A name is only written if it would begin a new field inside of an object.
	if ((States.Last().Flags & (EStateFlags::Name | EStateFlags::Field | EStateFlags::Object)) == EStateFlags::Object)
	{
		Name(NameOrValue);
	}
	else
	{
		String(NameOrValue);
	}
}

FAnsiStringView FCbWriter::CurrentName() const
{
	const FState& State = States.Last();
	if ((State.Flags & EStateFlags::Name) == EStateFlags::Name)
	{
		const uint8* const EncodedName = Data.GetData() + State.Offset + sizeof(ECbFieldType);
		uint32 NameLenByteCount;
		const uint64 NameLen = ReadVarUInt(EncodedName, NameLenByteCount);
		using NameLenSizeType = FAnsiStringView::SizeType;
		const NameLenSizeType ClampedNameLen = NameLenSizeType(FMath::Clamp<uint64>(NameLen, 0, ~NameLenSizeType(0)));
		return FAnsiStringView(reinterpret_cast<const ANSICHAR*>(EncodedName + NameLenByteCount), ClampedNameLen);
	}
	return FAnsiStringView();
}

void FCbWriter::MakeFieldsUniform(const int64 FieldBeginOffset, const int64 FieldEndOffset)
{
	FMutableMemoryView SourceView(Data.GetData() + FieldBeginOffset, uint64(FieldEndOffset - FieldBeginOffset));
	FMutableMemoryView TargetView = SourceView + sizeof(ECbFieldType);
	while (!SourceView.IsEmpty())
	{
		const uint64 FieldSize = MeasureCompactBinary(SourceView) - sizeof(ECbFieldType);
		SourceView += sizeof(ECbFieldType);
		if (TargetView.GetData() != SourceView.GetData())
		{
			FMemory::Memmove(TargetView.GetData(), SourceView.GetData(), FieldSize);
		}
		SourceView += FieldSize;
		TargetView += FieldSize;
	}
	if (!TargetView.IsEmpty())
	{
		Data.RemoveAt(FieldEndOffset - TargetView.GetSize(), TargetView.GetSize());
	}
}

void FCbWriter::Field(const FCbField& Value)
{
	checkf(Value.HasValue(), TEXT("It is invalid to write a field with no value."));
	BeginField();
	EndField(AppendCompactBinary(Value, Data));
}

void FCbWriter::BeginObject()
{
	BeginField();
	States.Push(FState());
	States.Last().Flags |= EStateFlags::Object;
}

void FCbWriter::EndObject()
{
	checkf(States.Num() > 1 && (States.Last().Flags & EStateFlags::Object) == EStateFlags::Object,
		TEXT("It is invalid to end an object when an object is not at the top of the stack."));
	checkf((States.Last().Flags & EStateFlags::Field) == EStateFlags::None,
		TEXT("It is invalid to end an object until the previous field is finished."));
	const bool bUniform = IsUniformType(States.Last().UniformType);
	const uint64 Count = States.Last().Count;
	States.Pop();

	// Calculate the offset of the payload.
	const FState& State = States.Last();
	int64 PayloadOffset = State.Offset + 1;
	if ((State.Flags & EStateFlags::Name) == EStateFlags::Name)
	{
		uint32 NameLenByteCount;
		const uint64 NameLen = ReadVarUInt(Data.GetData() + PayloadOffset, NameLenByteCount);
		PayloadOffset += NameLen + NameLenByteCount;
	}

	// Remove redundant field types for uniform objects.
	if (bUniform && Count > 1)
	{
		MakeFieldsUniform(PayloadOffset, Data.Num());
	}

	// Insert the object size.
	const uint64 Size = uint64(Data.Num() - PayloadOffset);
	const uint32 SizeByteCount = MeasureVarUInt(Size);
	Data.InsertUninitialized(PayloadOffset, SizeByteCount);
	WriteVarUInt(Size, Data.GetData() + PayloadOffset);

	EndField(bUniform ? ECbFieldType::UniformObject : ECbFieldType::Object);
}

void FCbWriter::Object(const FCbObject& Value)
{
	BeginField();
	EndField(AppendCompactBinary(Value, Data));
}

void FCbWriter::BeginArray()
{
	BeginField();
	States.Push(FState());
	States.Last().Flags |= EStateFlags::Array;
}

void FCbWriter::EndArray()
{
	checkf(States.Num() > 1 && (States.Last().Flags & EStateFlags::Array) == EStateFlags::Array,
		TEXT("Invalid attempt to end an array when an array is not at the top of the stack."));
	checkf((States.Last().Flags & EStateFlags::Field) == EStateFlags::None,
		TEXT("It is invalid to end an array until the previous field is finished."));
	const bool bUniform = IsUniformType(States.Last().UniformType);
	const uint64 Count = States.Last().Count;
	States.Pop();

	// Calculate the offset of the payload.
	const FState& State = States.Last();
	int64 PayloadOffset = State.Offset + 1;
	if ((State.Flags & EStateFlags::Name) == EStateFlags::Name)
	{
		uint32 NameLenByteCount;
		const uint64 NameLen = ReadVarUInt(Data.GetData() + PayloadOffset, NameLenByteCount);
		PayloadOffset += NameLen + NameLenByteCount;
	}

	// Remove redundant field types for uniform arrays.
	if (bUniform && Count > 1)
	{
		MakeFieldsUniform(PayloadOffset, Data.Num());
	}

	// Insert the array size and field count.
	const uint32 CountByteCount = MeasureVarUInt(Count);
	const uint64 Size = uint64(Data.Num() - PayloadOffset) + CountByteCount;
	const uint32 SizeByteCount = MeasureVarUInt(Size);
	Data.InsertUninitialized(PayloadOffset, SizeByteCount + CountByteCount);
	WriteVarUInt(Size, Data.GetData() + PayloadOffset);
	WriteVarUInt(Count, Data.GetData() + PayloadOffset + SizeByteCount);

	EndField(bUniform ? ECbFieldType::UniformArray : ECbFieldType::Array);
}

void FCbWriter::Array(const FCbArray& Value)
{
	BeginField();
	EndField(AppendCompactBinary(Value, Data));
}

void FCbWriter::Null()
{
	BeginField();
	EndField(ECbFieldType::Null);
}

void FCbWriter::Binary(const void* const Value, const uint64 Size)
{
	BeginField();
	const uint32 SizeByteCount = MeasureVarUInt(Size);
	const int64 SizeOffset = Data.AddUninitialized(SizeByteCount);
	WriteVarUInt(Size, Data.GetData() + SizeOffset);
	Data.Append(static_cast<const uint8*>(Value), Size);
	EndField(ECbFieldType::Binary);
}

void FCbWriter::String(const FAnsiStringView Value)
{
	BeginField();
	const uint64 Size = uint64(Value.Len());
	const uint32 SizeByteCount = MeasureVarUInt(Size);
	const int64 Offset = Data.AddUninitialized(SizeByteCount + Size);
	uint8* StringData = Data.GetData() + Offset;
	WriteVarUInt(Size, StringData);
	StringData += SizeByteCount;
	if (Size > 0)
	{
		FMemory::Memcpy(StringData, Value.GetData(), Value.Len() * sizeof(ANSICHAR));
	}
	EndField(ECbFieldType::String);
}

void FCbWriter::String(const FWideStringView Value)
{
	BeginField();
	const uint32 Size = uint32(FTCHARToUTF8_Convert::ConvertedLength(Value.GetData(), Value.Len()));
	const uint32 SizeByteCount = MeasureVarUInt(Size);
	const int64 Offset = Data.AddUninitialized(SizeByteCount + Size);
	uint8* StringData = Data.GetData() + Offset;
	WriteVarUInt(Size, StringData);
	StringData += SizeByteCount;
	if (Size > 0)
	{
		FTCHARToUTF8_Convert::Convert(reinterpret_cast<ANSICHAR*>(StringData), Size, Value.GetData(), Value.Len());
	}
	EndField(ECbFieldType::String);
}

void FCbWriter::Integer(const int32 Value)
{
	if (Value >= 0)
	{
		return Integer(uint32(Value));
	}
	BeginField();
	const uint32 Magnitude = ~uint32(Value);
	const uint32 MagnitudeByteCount = MeasureVarUInt(Magnitude);
	const int64 Offset = Data.AddUninitialized(MagnitudeByteCount);
	WriteVarUInt(Magnitude, Data.GetData() + Offset);
	EndField(ECbFieldType::IntegerNegative);
}

void FCbWriter::Integer(const int64 Value)
{
	if (Value >= 0)
	{
		return Integer(uint64(Value));
	}
	BeginField();
	const uint64 Magnitude = ~uint64(Value);
	const uint32 MagnitudeByteCount = MeasureVarUInt(Magnitude);
	const int64 Offset = Data.AddUninitialized(MagnitudeByteCount);
	WriteVarUInt(Magnitude, Data.GetData() + Offset);
	EndField(ECbFieldType::IntegerNegative);
}

void FCbWriter::Integer(const uint32 Value)
{
	BeginField();
	const uint32 ValueByteCount = MeasureVarUInt(Value);
	const int64 Offset = Data.AddUninitialized(ValueByteCount);
	WriteVarUInt(Value, Data.GetData() + Offset);
	EndField(ECbFieldType::IntegerPositive);
}

void FCbWriter::Integer(const uint64 Value)
{
	BeginField();
	const uint32 ValueByteCount = MeasureVarUInt(Value);
	const int64 Offset = Data.AddUninitialized(ValueByteCount);
	WriteVarUInt(Value, Data.GetData() + Offset);
	EndField(ECbFieldType::IntegerPositive);
}

void FCbWriter::Float(const float Value)
{
	BeginField();
	const uint32 RawValue = NETWORK_ORDER32(reinterpret_cast<const uint32&>(Value));
	Data.Append(reinterpret_cast<const uint8*>(&RawValue), sizeof(uint32));
	EndField(ECbFieldType::Float32);
}

void FCbWriter::Float(const double Value)
{
	const float Value32 = float(Value);
	if (Value == double(Value32))
	{
		return Float(Value32);
	}
	BeginField();
	const uint64 RawValue = NETWORK_ORDER64(reinterpret_cast<const uint64&>(Value));
	Data.Append(reinterpret_cast<const uint8*>(&RawValue), sizeof(uint64));
	EndField(ECbFieldType::Float64);
}

void FCbWriter::Bool(const bool bValue)
{
	BeginField();
	EndField(bValue ? ECbFieldType::BoolTrue : ECbFieldType::BoolFalse);
}

void FCbWriter::Reference(const FBlake3Hash& Value)
{
	BeginField();
	Data.Append(Value.Bytes(), sizeof(decltype(Value.Bytes())));
	EndField(ECbFieldType::Reference);
}

void FCbWriter::BinaryReference(const FBlake3Hash& Value)
{
	BeginField();
	Data.Append(Value.Bytes(), sizeof(decltype(Value.Bytes())));
	EndField(ECbFieldType::BinaryReference);
}

void FCbWriter::Hash(const FBlake3Hash& Value)
{
	BeginField();
	Data.Append(Value.Bytes(), sizeof(decltype(Value.Bytes())));
	EndField(ECbFieldType::Hash);
}

void FCbWriter::Uuid(const FGuid& Value)
{
	const auto AppendSwappedBytes = [this](uint32 In)
	{
		In = NETWORK_ORDER32(In);
		Data.Append(reinterpret_cast<const uint8*>(&In), sizeof(In));
	};
	BeginField();
	AppendSwappedBytes(Value.A);
	AppendSwappedBytes(Value.B);
	AppendSwappedBytes(Value.C);
	AppendSwappedBytes(Value.D);
	EndField(ECbFieldType::Uuid);
}

void FCbWriter::DateTimeTicks(const int64 Ticks)
{
	BeginField();
	const uint64 RawValue = NETWORK_ORDER64(uint64(Ticks));
	Data.Append(reinterpret_cast<const uint8*>(&RawValue), sizeof(uint64));
	EndField(ECbFieldType::DateTime);
}

void FCbWriter::DateTime(const FDateTime Value)
{
	DateTimeTicks(Value.GetTicks());
}

void FCbWriter::TimeSpanTicks(const int64 Ticks)
{
	BeginField();
	const uint64 RawValue = NETWORK_ORDER64(uint64(Ticks));
	Data.Append(reinterpret_cast<const uint8*>(&RawValue), sizeof(uint64));
	EndField(ECbFieldType::TimeSpan);
}

void FCbWriter::TimeSpan(const FTimespan Value)
{
	TimeSpanTicks(Value.GetTicks());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbWriter& operator<<(FCbWriter& Writer, const FDateTime Value)
{
	Writer.DateTime(Value);
	return Writer;
}

FCbWriter& operator<<(FCbWriter& Writer, const FTimespan Value)
{
	Writer.TimeSpan(Value);
	return Writer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
