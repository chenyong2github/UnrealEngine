// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CompactBinary.h"

#include "Math/UnrealMathUtility.h"
#include "Misc/ByteSwap.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Timespan.h"
#include "Serialization/VarInt.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace CompactBinaryPrivate
{

static constexpr const uint8 GEmptyObjectPayload[] = { 0x00 };
static constexpr const uint8 GEmptyArrayPayload[] = { 0x01, 0x00 };

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

	static_assert(AnyReferenceBase == ECbFieldType::Reference, "AnyReferenceBase is invalid!");
	static_assert((AnyReferenceMask & (AllFlags | ECbFieldType::BinaryReference)) == ECbFieldType::Reference, "AnyReferenceMask is invalid!");
	static_assert(!(AnyReferenceMask & (AnyReferenceBase ^ ECbFieldType::BinaryReference)), "AnyReferenceMask or AnyReferenceBase is invalid!");
	static_assert(TypeMask == (AnyReferenceMask | (AnyReferenceBase ^ ECbFieldType::BinaryReference)), "AnyReferenceMask or AnyReferenceBase is invalid!");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbField::FCbField(const void* const InData, const ECbFieldType InType)
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

FConstMemoryView FCbField::AsMemoryView() const
{
	const uint32 TypeSize = FCbFieldType::HasFieldType(Type) ? sizeof(ECbFieldType) : 0;
	const uint32 NameSize = FCbFieldType::HasFieldName(Type) ? NameLen + MeasureVarUInt(NameLen) : 0;
	const uint64 LocalPayloadSize = GetPayloadSize();
	return MakeMemoryView(static_cast<const uint8*>(Payload) - TypeSize - NameSize, TypeSize + NameSize + GetPayloadSize());
}

uint64 FCbField::GetSize() const
{
	return AsMemoryView().GetSize();
}

uint64 FCbField::GetPayloadSize() const
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
	case ECbFieldType::Reference:
	case ECbFieldType::BinaryReference:
	case ECbFieldType::Hash:
		return 32;
	case ECbFieldType::Uuid:
		return 16;
	case ECbFieldType::DateTime:
	case ECbFieldType::TimeSpan:
		return 8;
	default:
		return 0;
	}
}

bool FCbField::Equals(const FCbField& Other) const
{
	return Type == Other.Type && AsMemoryView().EqualBytes(Other.AsMemoryView());
}

FCbObject FCbField::AsObject()
{
	const ECbFieldType LocalType = Type;
	if (FCbFieldType::IsObject(LocalType))
	{
		Error = ECbFieldError::None;
		return FCbObject(Payload, FCbFieldType::GetType(LocalType));
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return FCbObject();
	}
}

FCbArray FCbField::AsArray()
{
	const ECbFieldType LocalType = Type;
	if (FCbFieldType::IsArray(LocalType))
	{
		Error = ECbFieldError::None;
		return FCbArray(Payload, FCbFieldType::GetType(LocalType));
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return FCbArray();
	}
}

FConstMemoryView FCbField::AsBinary(const FConstMemoryView Default)
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

FAnsiStringView FCbField::AsString(const FAnsiStringView Default)
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

uint64 FCbField::AsInteger(const uint64 Default, const FIntegerParams Params)
{
	const ECbFieldType LocalType = Type;
	if (FCbFieldType::IsInteger(LocalType))
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

float FCbField::AsFloat(const float Default)
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

double FCbField::AsDouble(const double Default)
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

bool FCbField::AsBool(const bool bDefault)
{
	const ECbFieldType LocalType = Type;
	const bool bIsBool = FCbFieldType::IsBool(LocalType);
	Error = bIsBool ? ECbFieldError::None : ECbFieldError::TypeError;
	return (uint8(bIsBool) & uint8(LocalType) & 1) | ((!bIsBool) & bDefault);
}

FBlake3Hash FCbField::AsReference(const FBlake3Hash& Default)
{
	if (FCbFieldType::IsReference(Type))
	{
		Error = ECbFieldError::None;
		return FBlake3Hash(*static_cast<const FBlake3Hash::ByteArray*>(Payload));
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

FBlake3Hash FCbField::AsBinaryReference(const FBlake3Hash& Default)
{
	if (FCbFieldType::IsBinaryReference(Type))
	{
		Error = ECbFieldError::None;
		return FBlake3Hash(*static_cast<const FBlake3Hash::ByteArray*>(Payload));
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

FBlake3Hash FCbField::AsHash(const FBlake3Hash& Default)
{
	if (FCbFieldType::IsHash(Type))
	{
		Error = ECbFieldError::None;
		return FBlake3Hash(*static_cast<const FBlake3Hash::ByteArray*>(Payload));
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return Default;
	}
}

FGuid FCbField::AsUuid()
{
	return AsUuid(FGuid());
}

FGuid FCbField::AsUuid(const FGuid& Default)
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

int64 FCbField::AsDateTimeTicks(const int64 Default)
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

FDateTime FCbField::AsDateTime()
{
	return FDateTime(AsDateTimeTicks(0));
}

FDateTime FCbField::AsDateTime(FDateTime Default)
{
	return FDateTime(AsDateTimeTicks(Default.GetTicks()));
}

int64 FCbField::AsTimeSpanTicks(const int64 Default)
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

FTimespan FCbField::AsTimeSpan()
{
	return FTimespan(AsTimeSpanTicks(0));
}

FTimespan FCbField::AsTimeSpan(FTimespan Default)
{
	return FTimespan(AsTimeSpanTicks(Default.GetTicks()));
}

void FCbField::CopyTo(const FMutableMemoryView Buffer) const
{
	const FConstMemoryView Source = AsMemoryView();
	FMemory::Memcpy(Buffer.GetData(), Source.GetData(), FMath::Min(Buffer.GetSize(), Source.GetSize()));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbArray::FCbArray()
	: Payload(CompactBinaryPrivate::GEmptyArrayPayload)
	, Type(ECbFieldType::Array)
{
}

FCbArray::FCbArray(const void* const InData, const ECbFieldType InType)
{
	const uint8* Bytes = static_cast<const uint8*>(InData);
	const ECbFieldType LocalType = FCbFieldType::HasFieldType(InType) ? (ECbFieldType(*Bytes++) | ECbFieldType::HasFieldType) : InType;
	uint32 NameLenByteCount = 0;
	const uint64 NameLen = FCbFieldType::HasFieldName(LocalType) ? ReadVarUInt(Bytes, NameLenByteCount) : 0;
	Bytes += NameLen + NameLenByteCount;

	Type = LocalType;
	Payload = Bytes;
}

FConstMemoryView FCbArray::AsMemoryView() const
{
	uint32 PayloadSizeByteCount;
	const uint64 PayloadSize = ReadVarUInt(Payload, PayloadSizeByteCount);
	return MakeMemoryView(Payload, PayloadSizeByteCount + PayloadSize);
}

uint64 FCbArray::GetSize() const
{
	return sizeof(ECbFieldType) + AsMemoryView().GetSize();
}

uint64 FCbArray::Num() const
{
	const uint8* PayloadBytes = static_cast<const uint8*>(Payload);
	PayloadBytes += MeasureVarUInt(PayloadBytes);
	uint32 NumByteCount;
	return ReadVarUInt(PayloadBytes, NumByteCount);
}

FCbFieldIterator FCbArray::CreateIterator() const
{
	const uint8* PayloadBytes = static_cast<const uint8*>(Payload);
	uint32 PayloadSizeByteCount;
	const uint64 PayloadSize = ReadVarUInt(PayloadBytes, PayloadSizeByteCount);
	PayloadBytes += PayloadSizeByteCount;
	const uint64 NumByteCount = MeasureVarUInt(PayloadBytes);
	if (PayloadSize > NumByteCount)
	{
		const void* const PayloadEnd = PayloadBytes + PayloadSize;
		PayloadBytes += NumByteCount;
		const ECbFieldType UniformType = FCbFieldType::GetType(Type) == ECbFieldType::UniformArray ?
			ECbFieldType(*PayloadBytes++) : ECbFieldType::HasFieldType;
		return FCbFieldIterator(FCbField(PayloadBytes, UniformType), PayloadEnd);
	}
	return FCbFieldIterator();
}

bool FCbArray::Equals(const FCbArray& Other) const
{
	return FCbFieldType::GetType(Type) == FCbFieldType::GetType(Other.Type) && AsMemoryView().EqualBytes(Other.AsMemoryView());
}

void FCbArray::CopyTo(const FMutableMemoryView Buffer) const
{
	uint8* BufferBytes = static_cast<uint8*>(Buffer.GetData());
	*BufferBytes++ = uint8(FCbFieldType::GetType(Type));
	const FConstMemoryView Source = AsMemoryView();
	FMemory::Memcpy(BufferBytes, Source.GetData(), FMath::Min(Buffer.GetSize() - 1, Source.GetSize()));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbObject::FCbObject()
	: Payload(CompactBinaryPrivate::GEmptyObjectPayload)
	, Type(ECbFieldType::Object)
{
}

FCbObject::FCbObject(const void* const InData, const ECbFieldType InType)
{
	const uint8* Bytes = static_cast<const uint8*>(InData);
	const ECbFieldType LocalType = FCbFieldType::HasFieldType(InType) ? (ECbFieldType(*Bytes++) | ECbFieldType::HasFieldType) : InType;
	uint32 NameLenByteCount = 0;
	const uint64 NameLen = FCbFieldType::HasFieldName(LocalType) ? ReadVarUInt(Bytes, NameLenByteCount) : 0;
	Bytes += NameLen + NameLenByteCount;

	Type = LocalType;
	Payload = Bytes;
}

FConstMemoryView FCbObject::AsMemoryView() const
{
	uint32 PayloadSizeByteCount;
	const uint64 PayloadSize = ReadVarUInt(Payload, PayloadSizeByteCount);
	return MakeMemoryView(Payload, PayloadSizeByteCount + PayloadSize);
}

uint64 FCbObject::GetSize() const
{
	return sizeof(ECbFieldType) + AsMemoryView().GetSize();
}

FCbFieldIterator FCbObject::CreateIterator() const
{
	const uint8* PayloadBytes = static_cast<const uint8*>(Payload);
	uint32 PayloadSizeByteCount;
	const uint64 PayloadSize = ReadVarUInt(PayloadBytes, PayloadSizeByteCount);
	PayloadBytes += PayloadSizeByteCount;
	if (PayloadSize)
	{
		const void* const PayloadEnd = PayloadBytes + PayloadSize;
		const ECbFieldType UniformType = FCbFieldType::GetType(Type) == ECbFieldType::UniformObject ?
			ECbFieldType(*PayloadBytes++) : ECbFieldType::HasFieldType;
		return FCbFieldIterator(FCbField(PayloadBytes, UniformType), PayloadEnd);
	}
	return FCbFieldIterator();
}

bool FCbObject::Equals(const FCbObject& Other) const
{
	return FCbFieldType::GetType(Type) == FCbFieldType::GetType(Other.Type) && AsMemoryView().EqualBytes(Other.AsMemoryView());
}

FCbField FCbObject::Find(const FAnsiStringView Name) const
{
	for (const FCbField Field : *this)
	{
		if (Name.Equals(Field.GetName(), ESearchCase::CaseSensitive))
		{
			return Field;
		}
	}
	return FCbField();
}

FCbField FCbObject::FindIgnoreCase(const FAnsiStringView Name) const
{
	for (const FCbField Field : *this)
	{
		if (Name.Equals(Field.GetName(), ESearchCase::IgnoreCase))
		{
			return Field;
		}
	}
	return FCbField();
}

void FCbObject::CopyTo(const FMutableMemoryView Buffer) const
{
	uint8* BufferBytes = static_cast<uint8*>(Buffer.GetData());
	*BufferBytes++ = uint8(FCbFieldType::GetType(Type));
	const FConstMemoryView Source = AsMemoryView();
	FMemory::Memcpy(BufferBytes, Source.GetData(), FMath::Min(Buffer.GetSize() - 1, Source.GetSize()));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint64 MeasureCompactBinary(FConstMemoryView View, ECbFieldType Type)
{
	uint64 Size = 0;

	if (FCbFieldType::HasFieldType(Type))
	{
		if (View.GetSize() == 0)
		{
			return 0;
		}

		Type = *static_cast<const ECbFieldType*>(View.GetData());
		View += 1;
		Size += 1;
	}

	bool bVariableSize = false;
	switch (FCbFieldType::GetType(Type))
	{
	case ECbFieldType::None:
	case ECbFieldType::Null:
		break;
	case ECbFieldType::Object:
	case ECbFieldType::UniformObject:
	case ECbFieldType::Array:
	case ECbFieldType::UniformArray:
	case ECbFieldType::Binary:
	case ECbFieldType::String:
	case ECbFieldType::IntegerPositive:
	case ECbFieldType::IntegerNegative:
		bVariableSize = true;
		break;
	case ECbFieldType::Float32:
		Size += 4;
		break;
	case ECbFieldType::Float64:
		Size += 8;
		break;
	case ECbFieldType::BoolFalse:
	case ECbFieldType::BoolTrue:
		break;
	case ECbFieldType::Reference:
	case ECbFieldType::BinaryReference:
	case ECbFieldType::Hash:
		Size += 32;
		break;
	case ECbFieldType::Uuid:
		Size += 16;
		break;
	case ECbFieldType::DateTime:
	case ECbFieldType::TimeSpan:
		Size += 8;
		break;
	default:
		return 0;
	}

	if (FCbFieldType::HasFieldName(Type))
	{
		if (View.GetSize() == 0 || View.GetSize() < MeasureVarUInt(View.GetData()))
		{
			return 0;
		}

		uint32 NameLenByteCount;
		const uint64 NameLen = ReadVarUInt(View.GetData(), NameLenByteCount);
		const uint64 NameSize = NameLen + NameLenByteCount;

		if (bVariableSize && View.GetSize() < NameSize)
		{
			return 0;
		}

		View += NameSize;
		Size += NameSize;
	}

	switch (FCbFieldType::GetType(Type))
	{
	case ECbFieldType::Object:
	case ECbFieldType::UniformObject:
	case ECbFieldType::Array:
	case ECbFieldType::UniformArray:
	case ECbFieldType::Binary:
	case ECbFieldType::String:
		if (View.GetSize() == 0 || View.GetSize() < MeasureVarUInt(View.GetData()))
		{
			return 0;
		}
		else
		{
			uint32 PayloadSizeByteCount;
			const uint64 PayloadSize = ReadVarUInt(View.GetData(), PayloadSizeByteCount);
			return Size + PayloadSize + PayloadSizeByteCount;
		}
	case ECbFieldType::IntegerPositive:
	case ECbFieldType::IntegerNegative:
		if (View.GetSize() == 0)
		{
			return 0;
		}
		return Size + MeasureVarUInt(View.GetData());
	default:
		return Size;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
