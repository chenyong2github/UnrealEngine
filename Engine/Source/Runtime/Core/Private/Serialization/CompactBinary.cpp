// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CompactBinary.h"

#include "Hash/Blake3.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/ByteSwap.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Timespan.h"
#include "Serialization/Archive.h"
#include "Serialization/VarInt.h"
#include "String/BytesToHex.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace CompactBinaryPrivate
{

static constexpr const uint8 GEmptyObjectPayload[] = { uint8(ECbFieldType::Object), 0x00 };
static constexpr const uint8 GEmptyArrayPayload[] = { uint8(ECbFieldType::Array), 0x01, 0x00 };

template <typename T>
static constexpr FORCEINLINE T ReadUnaligned(const void* const Memory)
{
#if PLATFORM_SUPPORTS_UNALIGNED_LOADS
	return *static_cast<const T*>(Memory);
#else
	T Value;
	FMemory::Memcpy(&Value, Memory, sizeof(Value));
	return Value;
#endif
}

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FCbFieldType::StaticAssertTypeConstants()
{
	constexpr ECbFieldType AllFlags = ECbFieldType::HasFieldName | ECbFieldType::HasFieldType;
	static_assert(!(TypeMask & AllFlags), "TypeMask is invalid!");

	static_assert(ObjectBase == ECbFieldType::Object, "ObjectBase is invalid!");
	static_assert((ObjectMask & (AllFlags | ECbFieldType::UniformObject)) == ECbFieldType::Object, "ObjectMask is invalid!");
	static_assert(!(ObjectMask & (ObjectBase ^ ECbFieldType::UniformObject)), "ObjectMask or ObjectBase is invalid!");
	static_assert(TypeMask == (ObjectMask | (ObjectBase ^ ECbFieldType::UniformObject)), "ObjectMask or ObjectBase is invalid!");

	static_assert(ArrayBase == ECbFieldType::Array, "ArrayBase is invalid!");
	static_assert((ArrayMask & (AllFlags | ECbFieldType::UniformArray)) == ECbFieldType::Array, "ArrayMask is invalid!");
	static_assert(!(ArrayMask & (ArrayBase ^ ECbFieldType::UniformArray)), "ArrayMask or ArrayBase is invalid!");
	static_assert(TypeMask == (ArrayMask | (ArrayBase ^ ECbFieldType::UniformArray)), "ArrayMask or ArrayBase is invalid!");

	static_assert(IntegerBase == ECbFieldType::IntegerPositive, "IntegerBase is invalid!");
	static_assert((IntegerMask & (AllFlags | ECbFieldType::IntegerNegative)) == ECbFieldType::IntegerPositive, "IntegerMask is invalid!");
	static_assert(!(IntegerMask & (IntegerBase ^ ECbFieldType::IntegerNegative)), "IntegerMask or IntegerBase is invalid!");
	static_assert(TypeMask == (IntegerMask | (IntegerBase ^ ECbFieldType::IntegerNegative)), "IntegerMask or IntegerBase is invalid!");

	static_assert(FloatBase == ECbFieldType::IntegerPositive, "FloatBase is invalid!");
	static_assert((FloatMask & (AllFlags | ECbFieldType::IntegerPositive)) == ECbFieldType::IntegerPositive, "FloatMask is invalid!");
	static_assert(!(FloatMask & (FloatBase ^ ECbFieldType::Float64)), "FloatMask or FloatBase is invalid!");
	static_assert(TypeMask == (FloatMask | (FloatBase ^ ECbFieldType::Float64)), "FloatMask or FloatBase is invalid!");

	static_assert(BoolBase == ECbFieldType::BoolFalse, "BoolBase is invalid!");
	static_assert((BoolMask & (AllFlags | ECbFieldType::BoolTrue)) == ECbFieldType::BoolFalse, "BoolMask is invalid!");
	static_assert(!(BoolMask & (BoolBase ^ ECbFieldType::BoolTrue)), "BoolMask or BoolBase is invalid!");
	static_assert(TypeMask == (BoolMask | (BoolBase ^ ECbFieldType::BoolTrue)), "BoolMask or BoolBase is invalid!");

	static_assert(AttachmentBase == ECbFieldType::CompactBinaryAttachment, "AttachmentBase is invalid!");
	static_assert((AttachmentMask & (AllFlags | ECbFieldType::BinaryAttachment)) == ECbFieldType::CompactBinaryAttachment, "AttachmentMask is invalid!");
	static_assert(!(AttachmentMask & (AttachmentBase ^ ECbFieldType::BinaryAttachment)), "AttachmentMask or AttachmentBase is invalid!");
	static_assert(TypeMask == (AttachmentMask | (AttachmentBase ^ ECbFieldType::BinaryAttachment)), "AttachmentMask or AttachmentBase is invalid!");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbObjectId::FCbObjectId(const FMemoryView ObjectId)
{
	checkf(ObjectId.GetSize() == sizeof(Bytes),
		TEXT("FCbObjectId cannot be constructed from a view of %" UINT64_FMT " bytes."), ObjectId.GetSize());
	FMemory::Memcpy(Bytes, ObjectId.GetData(), sizeof(Bytes));
}

void FCbObjectId::ToString(FAnsiStringBuilderBase& Builder) const
{
	UE::String::BytesToHexLower(Bytes, Builder);
}

void FCbObjectId::ToString(FWideStringBuilderBase& Builder) const
{
	UE::String::BytesToHexLower(Bytes, Builder);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbFieldView::FCbFieldView(const void* const InData, const ECbFieldType InType)
{
	const uint8* Bytes = static_cast<const uint8*>(InData);
	const ECbFieldType LocalType = FCbFieldType::HasFieldType(InType) ? (ECbFieldType(*Bytes++) | ECbFieldType::HasFieldType) : InType;
	uint32 NameLenByteCount = 0;
	const uint64 NameLen64 = FCbFieldType::HasFieldName(LocalType) ? ReadVarUInt(Bytes, NameLenByteCount) : 0;
	Bytes += NameLen64 + NameLenByteCount;

	Type = LocalType;
	NameLen = uint32(FMath::Clamp<uint64>(NameLen64, 0, ~uint32(0)));
	Payload = Bytes;
}

FCbObjectView FCbFieldView::AsObjectView()
{
	if (FCbFieldType::IsObject(Type))
	{
		Error = ECbFieldError::None;
		return FCbObjectView::FromFieldNoCheck(*this);
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return FCbObjectView();
	}
}

FCbArrayView FCbFieldView::AsArrayView()
{
	if (FCbFieldType::IsArray(Type))
	{
		Error = ECbFieldError::None;
		return FCbArrayView::FromFieldNoCheck(*this);
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return FCbArrayView();
	}
}

FMemoryView FCbFieldView::AsBinaryView(const FMemoryView Default)
{
	if (FCbFieldType::IsBinary(Type))
	{
		const uint8* const PayloadBytes = static_cast<const uint8*>(Payload);
		uint32 ValueSizeByteCount;
		const uint64 ValueSize = ReadVarUInt(PayloadBytes, ValueSizeByteCount);

		Error = ECbFieldError::None;
		return MakeMemoryView(PayloadBytes + ValueSizeByteCount, ValueSize);
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

FAnsiStringView FCbFieldView::AsString(const FAnsiStringView Default)
{
	if (FCbFieldType::IsString(Type))
	{
		const ANSICHAR* const PayloadChars = static_cast<const ANSICHAR*>(Payload);
		uint32 ValueSizeByteCount;
		const uint64 ValueSize = ReadVarUInt(PayloadChars, ValueSizeByteCount);

		if (ValueSize >= (uint64(1) << 31))
		{
			Error = ECbFieldError::RangeError;
			return Default;
		}

		Error = ECbFieldError::None;
		return FAnsiStringView(PayloadChars + ValueSizeByteCount, int32(ValueSize));
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

uint64 FCbFieldView::AsInteger(const uint64 Default, const FIntegerParams Params)
{
	if (FCbFieldType::IsInteger(Type))
	{
		// A shift of a 64-bit value by 64 is undefined so shift by one less because magnitude is never zero.
		const uint64 OutOfRangeMask = uint64(-2) << (Params.MagnitudeBits - 1);
		const uint64 IsNegative = uint8(Type) & 1;

		uint32 MagnitudeByteCount;
		const uint64 Magnitude = ReadVarUInt(Payload, MagnitudeByteCount);
		const uint64 Value = Magnitude ^ -int64(IsNegative);

		const uint64 IsInRange = (!(Magnitude & OutOfRangeMask)) & ((!IsNegative) | Params.IsSigned);
		Error = IsInRange ? ECbFieldError::None : ECbFieldError::RangeError;

		const uint64 UseValueMask = -int64(IsInRange);
		return (Value & UseValueMask) | (Default & ~UseValueMask);
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

float FCbFieldView::AsFloat(const float Default)
{
	switch (FCbFieldType::GetType(Type))
	{
	case ECbFieldType::IntegerPositive:
	case ECbFieldType::IntegerNegative:
	{
		const uint64 IsNegative = uint8(Type) & 1;
		constexpr uint64 OutOfRangeMask = ~((uint64(1) << /*FLT_MANT_DIG*/ 24) - 1);

		uint32 MagnitudeByteCount;
		const int64 Magnitude = ReadVarUInt(Payload, MagnitudeByteCount) + IsNegative;
		const uint64 IsInRange = !(Magnitude & OutOfRangeMask);
		Error = IsInRange ? ECbFieldError::None : ECbFieldError::RangeError;
		return IsInRange ? float(IsNegative ? -Magnitude : Magnitude) : Default;
	}
	case ECbFieldType::Float32:
	{
		Error = ECbFieldError::None;
		const uint32 Value = NETWORK_ORDER32(CompactBinaryPrivate::ReadUnaligned<uint32>(Payload));
		return reinterpret_cast<const float&>(Value);
	}
	case ECbFieldType::Float64:
		Error = ECbFieldError::RangeError;
		return Default;
	default:
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

double FCbFieldView::AsDouble(const double Default)
{
	switch (FCbFieldType::GetType(Type))
	{
	case ECbFieldType::IntegerPositive:
	case ECbFieldType::IntegerNegative:
	{
		const uint64 IsNegative = uint8(Type) & 1;
		constexpr uint64 OutOfRangeMask = ~((uint64(1) << /*DBL_MANT_DIG*/ 53) - 1);

		uint32 MagnitudeByteCount;
		const int64 Magnitude = ReadVarUInt(Payload, MagnitudeByteCount) + IsNegative;
		const uint64 IsInRange = !(Magnitude & OutOfRangeMask);
		Error = IsInRange ? ECbFieldError::None : ECbFieldError::RangeError;
		return IsInRange ? double(IsNegative ? -Magnitude : Magnitude) : Default;
	}
	case ECbFieldType::Float32:
	{
		Error = ECbFieldError::None;
		const uint32 Value = NETWORK_ORDER32(CompactBinaryPrivate::ReadUnaligned<uint32>(Payload));
		return reinterpret_cast<const float&>(Value);
	}
	case ECbFieldType::Float64:
	{
		Error = ECbFieldError::None;
		const uint64 Value = NETWORK_ORDER64(CompactBinaryPrivate::ReadUnaligned<uint64>(Payload));
		return reinterpret_cast<const double&>(Value);
	}
	default:
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

bool FCbFieldView::AsBool(const bool bDefault)
{
	const ECbFieldType LocalType = Type;
	const bool bIsBool = FCbFieldType::IsBool(LocalType);
	Error = bIsBool ? ECbFieldError::None : ECbFieldError::TypeError;
	return (uint8(bIsBool) & uint8(LocalType) & 1) | ((!bIsBool) & bDefault);
}

FIoHash FCbFieldView::AsCompactBinaryAttachment(const FIoHash& Default)
{
	if (FCbFieldType::IsCompactBinaryAttachment(Type))
	{
		Error = ECbFieldError::None;
		return FIoHash(*static_cast<const FIoHash::ByteArray*>(Payload));
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

FIoHash FCbFieldView::AsBinaryAttachment(const FIoHash& Default)
{
	if (FCbFieldType::IsBinaryAttachment(Type))
	{
		Error = ECbFieldError::None;
		return FIoHash(*static_cast<const FIoHash::ByteArray*>(Payload));
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

FIoHash FCbFieldView::AsAttachment(const FIoHash& Default)
{
	if (FCbFieldType::IsAttachment(Type))
	{
		Error = ECbFieldError::None;
		return FIoHash(*static_cast<const FIoHash::ByteArray*>(Payload));
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

FIoHash FCbFieldView::AsHash(const FIoHash& Default)
{
	if (FCbFieldType::IsHash(Type))
	{
		Error = ECbFieldError::None;
		return FIoHash(*static_cast<const FIoHash::ByteArray*>(Payload));
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

FGuid FCbFieldView::AsUuid()
{
	return AsUuid(FGuid());
}

FGuid FCbFieldView::AsUuid(const FGuid& Default)
{
	if (FCbFieldType::IsUuid(Type))
	{
		Error = ECbFieldError::None;
		FGuid Value;
		FMemory::Memcpy(&Value, Payload, sizeof(FGuid));
		Value.A = NETWORK_ORDER32(Value.A);
		Value.B = NETWORK_ORDER32(Value.B);
		Value.C = NETWORK_ORDER32(Value.C);
		Value.D = NETWORK_ORDER32(Value.D);
		return Value;
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

int64 FCbFieldView::AsDateTimeTicks(const int64 Default)
{
	if (FCbFieldType::IsDateTime(Type))
	{
		Error = ECbFieldError::None;
		return NETWORK_ORDER64(CompactBinaryPrivate::ReadUnaligned<int64>(Payload));
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

FDateTime FCbFieldView::AsDateTime()
{
	return FDateTime(AsDateTimeTicks(0));
}

FDateTime FCbFieldView::AsDateTime(FDateTime Default)
{
	return FDateTime(AsDateTimeTicks(Default.GetTicks()));
}

int64 FCbFieldView::AsTimeSpanTicks(const int64 Default)
{
	if (FCbFieldType::IsTimeSpan(Type))
	{
		Error = ECbFieldError::None;
		return NETWORK_ORDER64(CompactBinaryPrivate::ReadUnaligned<int64>(Payload));
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

FTimespan FCbFieldView::AsTimeSpan()
{
	return FTimespan(AsTimeSpanTicks(0));
}

FTimespan FCbFieldView::AsTimeSpan(FTimespan Default)
{
	return FTimespan(AsTimeSpanTicks(Default.GetTicks()));
}

FCbObjectId FCbFieldView::AsObjectId(const FCbObjectId& Default)
{
	static_assert(sizeof(FCbObjectId) == 12, "FCbObjectId is expected to be 12 bytes.");
	if (FCbFieldType::IsObjectId(Type))
	{
		Error = ECbFieldError::None;
		return FCbObjectId(MakeMemoryView(Payload, sizeof(FCbObjectId)));
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

FCbCustomById FCbFieldView::AsCustomById(FCbCustomById Default)
{
	if (FCbFieldType::IsCustomById(Type))
	{
		const uint8* PayloadBytes = static_cast<const uint8*>(Payload);
		uint32 PayloadSizeByteCount;
		const uint64 PayloadSize = ReadVarUInt(PayloadBytes, PayloadSizeByteCount);
		PayloadBytes += PayloadSizeByteCount;

		FCbCustomById Value;
		uint32 TypeIdByteCount;
		Value.Id = ReadVarUInt(PayloadBytes, TypeIdByteCount);
		Value.Data = MakeMemoryView(PayloadBytes + TypeIdByteCount, PayloadSize - TypeIdByteCount);
		Error = ECbFieldError::None;
		return Value;
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

FCbCustomByName FCbFieldView::AsCustomByName(FCbCustomByName Default)
{
	if (FCbFieldType::IsCustomByName(Type))
	{
		const uint8* PayloadBytes = static_cast<const uint8*>(Payload);
		uint32 PayloadSizeByteCount;
		const uint64 PayloadSize = ReadVarUInt(PayloadBytes, PayloadSizeByteCount);
		PayloadBytes += PayloadSizeByteCount;

		uint32 TypeNameLenByteCount;
		const uint64 TypeNameLen = ReadVarUInt(PayloadBytes, TypeNameLenByteCount);
		PayloadBytes += TypeNameLenByteCount;

		FCbCustomByName Value;
		Value.Name = FAnsiStringView(
			reinterpret_cast<const ANSICHAR*>(PayloadBytes),
			static_cast<FAnsiStringView::SizeType>(TypeNameLen));
		Value.Data = MakeMemoryView(PayloadBytes + TypeNameLen, PayloadSize - TypeNameLen - TypeNameLenByteCount);
		Error = ECbFieldError::None;
		return Value;
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

FMemoryView FCbFieldView::AsCustom(uint64 Id, FMemoryView Default)
{
	FCbCustomById Custom = AsCustomById(FCbCustomById{Id, Default});
	if (Custom.Id == Id)
	{
		return Custom.Data;
	}
	else
	{
		Error = ECbFieldError::RangeError;
		return Default;
	}
}

FMemoryView FCbFieldView::AsCustom(FAnsiStringView Name, FMemoryView Default)
{
	const FCbCustomByName Custom = AsCustomByName(FCbCustomByName{Name, Default});
	if (Custom.Name.Equals(Name, ESearchCase::CaseSensitive))
	{
		return Custom.Data;
	}
	else
	{
		Error = ECbFieldError::RangeError;
		return Default;
	}
}

uint64 FCbFieldView::GetSize() const
{
	return sizeof(ECbFieldType) + GetViewNoType().GetSize();
}

uint64 FCbFieldView::GetPayloadSize() const
{
	switch (FCbFieldType::GetType(Type))
	{
	case ECbFieldType::None:
	case ECbFieldType::Null:
		return 0;
	case ECbFieldType::Object:
	case ECbFieldType::UniformObject:
	case ECbFieldType::Array:
	case ECbFieldType::UniformArray:
	case ECbFieldType::Binary:
	case ECbFieldType::String:
	case ECbFieldType::CustomById:
	case ECbFieldType::CustomByName:
	{
		uint32 PayloadSizeByteCount;
		const uint64 PayloadSize = ReadVarUInt(Payload, PayloadSizeByteCount);
		return PayloadSize + PayloadSizeByteCount;
	}
	case ECbFieldType::IntegerPositive:
	case ECbFieldType::IntegerNegative:
		return MeasureVarUInt(Payload);
	case ECbFieldType::Float32:
		return 4;
	case ECbFieldType::Float64:
		return 8;
	case ECbFieldType::BoolFalse:
	case ECbFieldType::BoolTrue:
		return 0;
	case ECbFieldType::CompactBinaryAttachment:
	case ECbFieldType::BinaryAttachment:
	case ECbFieldType::Hash:
		return 20;
	case ECbFieldType::Uuid:
		return 16;
	case ECbFieldType::DateTime:
	case ECbFieldType::TimeSpan:
		return 8;
	case ECbFieldType::ObjectId:
		return 12;
	default:
		return 0;
	}
}

FBlake3Hash FCbFieldView::GetHash() const
{
	FBlake3 Hash;
	AppendHash(Hash);
	return Hash.Finalize();
}

void FCbFieldView::AppendHash(FBlake3& Builder) const
{
	const ECbFieldType SerializedType = FCbFieldType::GetSerializedType(Type);
	Builder.Update(&SerializedType, sizeof(SerializedType));
	Builder.Update(GetViewNoType());
}

bool FCbFieldView::Equals(const FCbFieldView& Other) const
{
	return FCbFieldType::GetSerializedType(Type) == FCbFieldType::GetSerializedType(Other.Type) &&
		GetViewNoType().EqualBytes(Other.GetViewNoType());
}

void FCbFieldView::CopyTo(FMutableMemoryView Buffer) const
{
	const FMemoryView Source = GetViewNoType();
	checkf(Buffer.GetSize() == sizeof(ECbFieldType) + Source.GetSize(),
		TEXT("Buffer is %" UINT64_FMT " bytes but %" UINT64_FMT " is required."),
		Buffer.GetSize(), sizeof(ECbFieldType) + Source.GetSize());
	const ECbFieldType SerializedType = FCbFieldType::GetSerializedType(Type);
	Buffer.CopyFrom(MakeMemoryView(&SerializedType, 1)).CopyFrom(Source);
}

void FCbFieldView::CopyTo(FArchive& Ar) const
{
	check(Ar.IsSaving());
	const FMemoryView Source = GetViewNoType();
	ECbFieldType SerializedType = FCbFieldType::GetSerializedType(Type);
	Ar.Serialize(&SerializedType, sizeof(SerializedType));
	Ar.Serialize(const_cast<void*>(Source.GetData()), static_cast<int64>(Source.GetSize()));
}

void FCbFieldView::IterateAttachments(FCbFieldVisitor Visitor) const
{
	switch (FCbFieldType::GetType(Type))
	{
	case ECbFieldType::Object:
	case ECbFieldType::UniformObject:
		return FCbObjectView::FromFieldNoCheck(*this).IterateAttachments(Visitor);
	case ECbFieldType::Array:
	case ECbFieldType::UniformArray:
		return FCbArrayView::FromFieldNoCheck(*this).IterateAttachments(Visitor);
	case ECbFieldType::CompactBinaryAttachment:
	case ECbFieldType::BinaryAttachment:
		return Visitor(*this);
	default:
		return;
	}
}

FMemoryView FCbFieldView::GetView() const
{
	const uint32 TypeSize = FCbFieldType::HasFieldType(Type) ? sizeof(ECbFieldType) : 0;
	const uint32 NameSize = FCbFieldType::HasFieldName(Type) ? NameLen + MeasureVarUInt(NameLen) : 0;
	const uint64 PayloadSize = GetPayloadSize();
	return MakeMemoryView(static_cast<const uint8*>(Payload) - TypeSize - NameSize, TypeSize + NameSize + PayloadSize);
}

FMemoryView FCbFieldView::GetViewNoType() const
{
	const uint32 NameSize = FCbFieldType::HasFieldName(Type) ? NameLen + MeasureVarUInt(NameLen) : 0;
	const uint64 PayloadSize = GetPayloadSize();
	return MakeMemoryView(static_cast<const uint8*>(Payload) - NameSize, NameSize + PayloadSize);
}

FCbFieldViewIterator FCbFieldView::CreateViewIterator() const
{
	const ECbFieldType LocalType = Type;
	if (FCbFieldType::HasFields(LocalType))
	{
		const uint8* PayloadBytes = static_cast<const uint8*>(GetPayload());
		uint32 PayloadSizeByteCount;
		const uint64 PayloadSize = ReadVarUInt(PayloadBytes, PayloadSizeByteCount);
		PayloadBytes += PayloadSizeByteCount;
		const uint64 NumByteCount = FCbFieldType::IsArray(LocalType) ? MeasureVarUInt(PayloadBytes) : 0;
		if (PayloadSize > NumByteCount)
		{
			const void* const PayloadEnd = PayloadBytes + PayloadSize;
			PayloadBytes += NumByteCount;
			const ECbFieldType UniformType = FCbFieldType::HasUniformFields(LocalType) ?
				ECbFieldType(*PayloadBytes++) : ECbFieldType::HasFieldType;
			return FCbFieldViewIterator::MakeRange(MakeMemoryView(PayloadBytes, PayloadEnd), UniformType);
		}
	}
	return FCbFieldViewIterator();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbArrayView::FCbArrayView()
	: FCbFieldView(CompactBinaryPrivate::GEmptyArrayPayload)
{
}

uint64 FCbArrayView::Num() const
{
	const uint8* PayloadBytes = static_cast<const uint8*>(GetPayload());
	PayloadBytes += MeasureVarUInt(PayloadBytes);
	uint32 NumByteCount;
	return ReadVarUInt(PayloadBytes, NumByteCount);
}

uint64 FCbArrayView::GetSize() const
{
	return sizeof(ECbFieldType) + GetPayloadSize();
}

FBlake3Hash FCbArrayView::GetHash() const
{
	FBlake3 Hash;
	AppendHash(Hash);
	return Hash.Finalize();
}

void FCbArrayView::AppendHash(FBlake3& Builder) const
{
	const ECbFieldType SerializedType = FCbFieldType::GetType(GetType());
	Builder.Update(&SerializedType, sizeof(SerializedType));
	Builder.Update(GetPayloadView());
}

bool FCbArrayView::Equals(const FCbArrayView& Other) const
{
	return FCbFieldType::GetType(GetType()) == FCbFieldType::GetType(Other.GetType()) &&
		GetPayloadView().EqualBytes(Other.GetPayloadView());
}

void FCbArrayView::CopyTo(const FMutableMemoryView Buffer) const
{
	const FMemoryView Source = GetPayloadView();
	checkf(Buffer.GetSize() == sizeof(ECbFieldType) + Source.GetSize(),
		TEXT("Buffer is %" UINT64_FMT " bytes but %" UINT64_FMT " is required."),
		Buffer.GetSize(), sizeof(ECbFieldType) + Source.GetSize());
	const ECbFieldType SerializedType = FCbFieldType::GetType(GetType());
	Buffer.CopyFrom(MakeMemoryView(&SerializedType, 1)).CopyFrom(Source);
}

void FCbArrayView::CopyTo(FArchive& Ar) const
{
	check(Ar.IsSaving());
	const FMemoryView Source = GetPayloadView();
	ECbFieldType SerializedType = FCbFieldType::GetType(GetType());
	Ar.Serialize(&SerializedType, sizeof(SerializedType));
	Ar.Serialize(const_cast<void*>(Source.GetData()), static_cast<int64>(Source.GetSize()));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbObjectView::FCbObjectView()
	: FCbFieldView(CompactBinaryPrivate::GEmptyObjectPayload)
{
}

FCbFieldView FCbObjectView::FindView(const FAnsiStringView Name) const
{
	for (const FCbFieldView Field : *this)
	{
		if (Name.Equals(Field.GetName(), ESearchCase::CaseSensitive))
		{
			return Field;
		}
	}
	return FCbFieldView();
}

FCbFieldView FCbObjectView::FindViewIgnoreCase(const FAnsiStringView Name) const
{
	for (const FCbFieldView Field : *this)
	{
		if (Name.Equals(Field.GetName(), ESearchCase::IgnoreCase))
		{
			return Field;
		}
	}
	return FCbFieldView();
}

FCbObjectView::operator bool() const
{
	return GetSize() > sizeof(CompactBinaryPrivate::GEmptyObjectPayload);
}

uint64 FCbObjectView::GetSize() const
{
	return sizeof(ECbFieldType) + GetPayloadSize();
}

FBlake3Hash FCbObjectView::GetHash() const
{
	FBlake3 Hash;
	AppendHash(Hash);
	return Hash.Finalize();
}

void FCbObjectView::AppendHash(FBlake3& Builder) const
{
	const ECbFieldType SerializedType = FCbFieldType::GetType(GetType());
	Builder.Update(&SerializedType, sizeof(SerializedType));
	Builder.Update(GetPayloadView());
}

bool FCbObjectView::Equals(const FCbObjectView& Other) const
{
	return FCbFieldType::GetType(GetType()) == FCbFieldType::GetType(Other.GetType()) &&
		GetPayloadView().EqualBytes(Other.GetPayloadView());
}

void FCbObjectView::CopyTo(const FMutableMemoryView Buffer) const
{
	const FMemoryView Source = GetPayloadView();
	checkf(Buffer.GetSize() == sizeof(ECbFieldType) + Source.GetSize(),
		TEXT("Buffer is %" UINT64_FMT " bytes but %" UINT64_FMT " is required."),
		Buffer.GetSize(), sizeof(ECbFieldType) + Source.GetSize());
	const ECbFieldType SerializedType = FCbFieldType::GetType(GetType());
	Buffer.CopyFrom(MakeMemoryView(&SerializedType, 1)).CopyFrom(Source);
}

void FCbObjectView::CopyTo(FArchive& Ar) const
{
	check(Ar.IsSaving());
	const FMemoryView Source = GetPayloadView();
	ECbFieldType SerializedType = FCbFieldType::GetType(GetType());
	Ar.Serialize(&SerializedType, sizeof(SerializedType));
	Ar.Serialize(const_cast<void*>(Source.GetData()), static_cast<int64>(Source.GetSize()));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename FieldType>
TCbFieldIterator<FieldType>& TCbFieldIterator<FieldType>::operator++()
{
	const void* const PayloadEnd = FieldType::GetPayloadEnd();
	const int64 AtEndMask = int64(PayloadEnd == FieldsEnd) - 1;
	const ECbFieldType NextType = ECbFieldType(int64(FieldType::GetType()) & AtEndMask);
	const void* const NextField = reinterpret_cast<const void*>(int64(PayloadEnd) & AtEndMask);
	const void* const NextFieldsEnd = reinterpret_cast<const void*>(int64(FieldsEnd) & AtEndMask);
	FieldType::Assign(NextField, NextType);
	FieldsEnd = NextFieldsEnd;
	return *this;
}

template <typename FieldType>
uint64 TCbFieldIterator<FieldType>::GetRangeSize() const
{
	FMemoryView View;
	if (TryGetSerializedRangeView(View))
	{
		return View.GetSize();
	}
	else
	{
		uint64 Size = 0;
		for (TCbFieldIterator It(*this); It; ++It)
		{
			Size += It.GetSize();
		}
		return Size;
	}
}

template <typename FieldType>
FBlake3Hash TCbFieldIterator<FieldType>::GetRangeHash() const
{
	FBlake3 Hash;
	AppendRangeHash(Hash);
	return Hash.Finalize();
}

template <typename FieldType>
void TCbFieldIterator<FieldType>::AppendRangeHash(FBlake3& Builder) const
{
	FMemoryView View;
	if (TryGetSerializedRangeView(View))
	{
		Builder.Update(View);
	}
	else
	{
		for (TCbFieldIterator It(*this); It; ++It)
		{
			It.AppendHash(Builder);
		}
	}
}

template <typename FieldType>
void TCbFieldIterator<FieldType>::CopyRangeTo(FMutableMemoryView InBuffer) const
{
	FMemoryView Source;
	if (TryGetSerializedRangeView(Source))
	{
		checkf(InBuffer.GetSize() == Source.GetSize(),
			TEXT("Buffer is %" UINT64_FMT " bytes but %" UINT64_FMT " is required."),
			InBuffer.GetSize(), Source.GetSize());
		InBuffer.CopyFrom(Source);
	}
	else
	{
		for (TCbFieldIterator It(*this); It; ++It)
		{
			const uint64 Size = It.GetSize();
			It.CopyTo(InBuffer.Left(Size));
			InBuffer += Size;
		}
	}
}

template <typename FieldType>
void TCbFieldIterator<FieldType>::CopyRangeTo(FArchive& Ar) const
{
	check(Ar.IsSaving());
	FMemoryView Source;
	if (TryGetSerializedRangeView(Source))
	{
		Ar.Serialize(const_cast<void*>(Source.GetData()), static_cast<int64>(Source.GetSize()));
	}
	else
	{
		for (TCbFieldIterator It(*this); It; ++It)
		{
			It.CopyTo(Ar);
		}
	}
}

template <typename FieldType>
void TCbFieldIterator<FieldType>::IterateRangeAttachments(FCbFieldVisitor Visitor) const
{
	// Always iterate over non-uniform ranges because we do not know if they contain an attachment.
	if (FCbFieldType::HasFieldType(FieldType::GetType()))
	{
		for (TCbFieldIterator It(*this); It; ++It)
		{
			if (FCbFieldType::MayContainAttachments(It.GetType()))
			{
				It.IterateAttachments(Visitor);
			}
		}
	}
	// Only iterate over uniform ranges if the uniform type may contain an attachment.
	else
	{
		if (FCbFieldType::MayContainAttachments(FieldType::GetType()))
		{
			for (TCbFieldIterator It(*this); It; ++It)
			{
				It.IterateAttachments(Visitor);
			}
		}
	}
}

template class TCbFieldIterator<FCbFieldView>;
template class TCbFieldIterator<FCbField>;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbFieldIterator FCbFieldIterator::CloneRange(const FCbFieldViewIterator& It)
{
	FMemoryView View;
	if (It.TryGetSerializedRangeView(View))
	{
		return MakeRange(FSharedBuffer::Clone(View));
	}
	else
	{
		FUniqueBuffer Buffer = FUniqueBuffer::Alloc(It.GetRangeSize());
		It.CopyRangeTo(Buffer);
		return MakeRange(Buffer.MoveToShared());
	}
}

FSharedBuffer FCbFieldIterator::GetRangeBuffer() const
{
	const FMemoryView RangeView = GetRangeView();
	const FSharedBuffer& OuterBuffer = GetOuterBuffer();
	return OuterBuffer.GetView() == RangeView ? OuterBuffer : FSharedBuffer::MakeView(RangeView, OuterBuffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
