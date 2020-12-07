// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CompactBinary.h"

#include "Math/UnrealMathUtility.h"
#include "Misc/ByteSwap.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Timespan.h"
#include "Serialization/Archive.h"
#include "Serialization/VarInt.h"

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

FCbObject FCbField::AsObject()
{
	if (FCbFieldType::IsObject(Type))
	{
		Error = ECbFieldError::None;
		return FCbObject::FromField(*this);
	}
	else
	{
		Error = ECbFieldError::TypeError;
		return FCbObject();
	}
}

FCbArray FCbField::AsArray()
{
	if (FCbFieldType::IsArray(Type))
	{
		Error = ECbFieldError::None;
		return FCbArray::FromField(*this);
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

uint64 FCbField::GetSize() const
{
	return sizeof(ECbFieldType) + GetViewNoType().GetSize();
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

FBlake3Hash FCbField::GetHash() const
{
	FBlake3 Hash;
	GetHash(Hash);
	return Hash.Finalize();
}

void FCbField::GetHash(FBlake3& Hash) const
{
	const ECbFieldType SerializedType = FCbFieldType::GetSerializedType(Type);
	Hash.Update(&SerializedType, sizeof(SerializedType));
	Hash.Update(GetViewNoType());
}

bool FCbField::Equals(const FCbField& Other) const
{
	return FCbFieldType::GetSerializedType(Type) == FCbFieldType::GetSerializedType(Other.Type) &&
		GetViewNoType().EqualBytes(Other.GetViewNoType());
}

void FCbField::CopyTo(FMutableMemoryView Buffer) const
{
	const FConstMemoryView Source = GetViewNoType();
	checkf(Buffer.GetSize() == sizeof(ECbFieldType) + Source.GetSize(),
		TEXT("A buffer of %" UINT64_FMT " bytes was provided when %" UINT64_FMT " bytes are required"),
		Buffer.GetSize(), sizeof(ECbFieldType) + Source.GetSize());
	*static_cast<ECbFieldType*>(Buffer.GetData()) = FCbFieldType::GetSerializedType(Type);
	Buffer.RightChopInline(sizeof(ECbFieldType));
	FMemory::Memcpy(Buffer.GetData(), Source.GetData(), Source.GetSize());
}

void FCbField::CopyTo(FArchive& Ar) const
{
	check(Ar.IsSaving());
	const FConstMemoryView Source = GetViewNoType();
	ECbFieldType SerializedType = FCbFieldType::GetSerializedType(Type);
	Ar.Serialize(&SerializedType, sizeof(SerializedType));
	Ar.Serialize(const_cast<void*>(Source.GetData()), static_cast<int64>(Source.GetSize()));
}

void FCbField::IterateReferences(FCbFieldVisitor Visitor) const
{
	switch (FCbFieldType::GetType(Type))
	{
	case ECbFieldType::Object:
	case ECbFieldType::UniformObject:
		return FCbObject::FromField(*this).IterateReferences(Visitor);
	case ECbFieldType::Array:
	case ECbFieldType::UniformArray:
		return FCbArray::FromField(*this).IterateReferences(Visitor);
	case ECbFieldType::Reference:
	case ECbFieldType::BinaryReference:
		return Visitor(*this);
	default:
		return;
	}
}

FConstMemoryView FCbField::GetView() const
{
	const uint32 TypeSize = FCbFieldType::HasFieldType(Type) ? sizeof(ECbFieldType) : 0;
	const uint32 NameSize = FCbFieldType::HasFieldName(Type) ? NameLen + MeasureVarUInt(NameLen) : 0;
	const uint64 PayloadSize = GetPayloadSize();
	return MakeMemoryView(static_cast<const uint8*>(Payload) - TypeSize - NameSize, TypeSize + NameSize + PayloadSize);
}

FConstMemoryView FCbField::GetViewNoType() const
{
	const uint32 NameSize = FCbFieldType::HasFieldName(Type) ? NameLen + MeasureVarUInt(NameLen) : 0;
	const uint64 PayloadSize = GetPayloadSize();
	return MakeMemoryView(static_cast<const uint8*>(Payload) - NameSize, NameSize + PayloadSize);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbArray::FCbArray()
	: FCbField(CompactBinaryPrivate::GEmptyArrayPayload)
{
}

uint64 FCbArray::Num() const
{
	const uint8* PayloadBytes = static_cast<const uint8*>(GetPayload());
	PayloadBytes += MeasureVarUInt(PayloadBytes);
	uint32 NumByteCount;
	return ReadVarUInt(PayloadBytes, NumByteCount);
}

FCbFieldIterator FCbArray::CreateIterator() const
{
	const uint8* PayloadBytes = static_cast<const uint8*>(GetPayload());
	uint32 PayloadSizeByteCount;
	const uint64 PayloadSize = ReadVarUInt(PayloadBytes, PayloadSizeByteCount);
	PayloadBytes += PayloadSizeByteCount;
	const uint64 NumByteCount = MeasureVarUInt(PayloadBytes);
	if (PayloadSize > NumByteCount)
	{
		const void* const PayloadEnd = PayloadBytes + PayloadSize;
		PayloadBytes += NumByteCount;
		const ECbFieldType UniformType = FCbFieldType::GetType(GetType()) == ECbFieldType::UniformArray ?
			ECbFieldType(*PayloadBytes++) : ECbFieldType::HasFieldType;
		return FCbFieldIterator::MakeRange(MakeMemoryView(PayloadBytes, PayloadEnd), UniformType);
	}
	return FCbFieldIterator();
}

uint64 FCbArray::GetSize() const
{
	return sizeof(ECbFieldType) + GetPayloadSize();
}

FBlake3Hash FCbArray::GetHash() const
{
	FBlake3 Hash;
	GetHash(Hash);
	return Hash.Finalize();
}

void FCbArray::GetHash(FBlake3& Hash) const
{
	const ECbFieldType SerializedType = FCbFieldType::GetType(GetType());
	Hash.Update(&SerializedType, sizeof(SerializedType));
	Hash.Update(GetPayloadView());
}

bool FCbArray::Equals(const FCbArray& Other) const
{
	return FCbFieldType::GetType(GetType()) == FCbFieldType::GetType(Other.GetType()) &&
		GetPayloadView().EqualBytes(Other.GetPayloadView());
}

void FCbArray::CopyTo(FMutableMemoryView Buffer) const
{
	const FConstMemoryView Source = GetPayloadView();
	checkf(Buffer.GetSize() == sizeof(ECbFieldType) + Source.GetSize(),
		TEXT("Buffer is %" UINT64_FMT " bytes but %" UINT64_FMT " is required."),
		Buffer.GetSize(), sizeof(ECbFieldType) + Source.GetSize());
	*static_cast<ECbFieldType*>(Buffer.GetData()) = FCbFieldType::GetType(GetType());
	Buffer.RightChopInline(sizeof(ECbFieldType));
	FMemory::Memcpy(Buffer.GetData(), Source.GetData(), Source.GetSize());
}

void FCbArray::CopyTo(FArchive& Ar) const
{
	check(Ar.IsSaving());
	const FConstMemoryView Source = GetPayloadView();
	ECbFieldType SerializedType = FCbFieldType::GetType(GetType());
	Ar.Serialize(&SerializedType, sizeof(SerializedType));
	Ar.Serialize(const_cast<void*>(Source.GetData()), static_cast<int64>(Source.GetSize()));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbObject::FCbObject()
	: FCbField(CompactBinaryPrivate::GEmptyObjectPayload)
{
}

FCbFieldIterator FCbObject::CreateIterator() const
{
	const uint8* PayloadBytes = static_cast<const uint8*>(GetPayload());
	uint32 PayloadSizeByteCount;
	const uint64 PayloadSize = ReadVarUInt(PayloadBytes, PayloadSizeByteCount);
	PayloadBytes += PayloadSizeByteCount;
	if (PayloadSize)
	{
		const void* const PayloadEnd = PayloadBytes + PayloadSize;
		const ECbFieldType UniformType = FCbFieldType::GetType(GetType()) == ECbFieldType::UniformObject ?
			ECbFieldType(*PayloadBytes++) : ECbFieldType::HasFieldType;
		return FCbFieldIterator::MakeRange(MakeMemoryView(PayloadBytes, PayloadEnd), UniformType);
	}
	return FCbFieldIterator();
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

uint64 FCbObject::GetSize() const
{
	return sizeof(ECbFieldType) + GetPayloadSize();
}

FBlake3Hash FCbObject::GetHash() const
{
	FBlake3 Hash;
	GetHash(Hash);
	return Hash.Finalize();
}

void FCbObject::GetHash(FBlake3& Hash) const
{
	const ECbFieldType SerializedType = FCbFieldType::GetType(GetType());
	Hash.Update(&SerializedType, sizeof(SerializedType));
	Hash.Update(GetPayloadView());
}

bool FCbObject::Equals(const FCbObject& Other) const
{
	return FCbFieldType::GetType(GetType()) == FCbFieldType::GetType(Other.GetType()) &&
		GetPayloadView().EqualBytes(Other.GetPayloadView());
}

void FCbObject::CopyTo(FMutableMemoryView Buffer) const
{
	const FConstMemoryView Source = GetPayloadView();
	checkf(Buffer.GetSize() == sizeof(ECbFieldType) + Source.GetSize(),
		TEXT("Buffer is %" UINT64_FMT " bytes but %" UINT64_FMT " is required."),
		Buffer.GetSize(), sizeof(ECbFieldType) + Source.GetSize());
	*static_cast<ECbFieldType*>(Buffer.GetData()) = FCbFieldType::GetType(GetType());
	Buffer.RightChopInline(sizeof(ECbFieldType));
	FMemory::Memcpy(Buffer.GetData(), Source.GetData(), Source.GetSize());
}

void FCbObject::CopyTo(FArchive& Ar) const
{
	check(Ar.IsSaving());
	const FConstMemoryView Source = GetPayloadView();
	ECbFieldType SerializedType = FCbFieldType::GetType(GetType());
	Ar.Serialize(&SerializedType, sizeof(SerializedType));
	Ar.Serialize(const_cast<void*>(Source.GetData()), static_cast<int64>(Source.GetSize()));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename FieldType>
uint64 TCbFieldIterator<FieldType>::GetRangeSize() const
{
	FConstMemoryView View;
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
	GetRangeHash(Hash);
	return Hash.Finalize();
}

template <typename FieldType>
void TCbFieldIterator<FieldType>::GetRangeHash(FBlake3& Hash) const
{
	FConstMemoryView View;
	if (TryGetSerializedRangeView(View))
	{
		Hash.Update(View);
	}
	else
	{
		for (TCbFieldIterator It(*this); It; ++It)
		{
			It.GetHash(Hash);
		}
	}
}

template <typename FieldType>
void TCbFieldIterator<FieldType>::CopyRangeTo(FMutableMemoryView Buffer) const
{
	FConstMemoryView Source;
	if (TryGetSerializedRangeView(Source))
	{
		checkf(Buffer.GetSize() == Source.GetSize(),
			TEXT("Buffer is %" UINT64_FMT " bytes but %" UINT64_FMT " is required."),
			Buffer.GetSize(), Source.GetSize());
		FMemory::Memcpy(Buffer.GetData(), Source.GetData(), Source.GetSize());
	}
	else
	{
		for (TCbFieldIterator It(*this); It; ++It)
		{
			const uint64 Size = It.GetSize();
			It.CopyTo(Buffer.Left(Size));
			Buffer += Size;
		}
	}
}

template <typename FieldType>
void TCbFieldIterator<FieldType>::CopyRangeTo(FArchive& Ar) const
{
	check(Ar.IsSaving());
	FConstMemoryView Source;
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
void TCbFieldIterator<FieldType>::IterateRangeReferences(FCbFieldVisitor Visitor) const
{
	// Always iterate over non-uniform ranges because we do not know if they contain a reference.
	if (FCbFieldType::HasFieldType(FieldType::GetType()))
	{
		for (TCbFieldIterator It(*this); It; ++It)
		{
			if (FCbFieldType::MayContainReferences(It.GetType()))
			{
				It.IterateReferences(Visitor);
			}
		}
	}
	// Only iterate over uniform ranges if the uniform type may contain a reference.
	else
	{
		if (FCbFieldType::MayContainReferences(FieldType::GetType()))
		{
			for (TCbFieldIterator It(*this); It; ++It)
			{
				It.IterateReferences(Visitor);
			}
		}
	}
}

template class TCbFieldIterator<FCbField>;
template class TCbFieldIterator<FCbFieldRef>;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbFieldRefIterator FCbFieldRefIterator::CloneRange(const FCbFieldIterator& It)
{
	FSharedBufferPtr Buffer;
	FConstMemoryView View;
	if (It.TryGetSerializedRangeView(View))
	{
		Buffer = FSharedBuffer::Clone(View);
	}
	else
	{
		Buffer = FSharedBuffer::Alloc(It.GetRangeSize());
		It.CopyRangeTo(*Buffer);
	}
	return MakeRange(MoveTemp(Buffer));
}

FSharedBufferConstPtr FCbFieldRefIterator::GetRangeBuffer() const
{
	const FConstMemoryView RangeView = GetRangeView();
	const FSharedBufferConstPtr& OuterBuffer = GetOuterBuffer();
	if (OuterBuffer)
	{
		if (OuterBuffer->GetView() == RangeView)
		{
			return OuterBuffer;
		}
		else
		{
			return FSharedBuffer::MakeView(RangeView, *OuterBuffer);
		}
	}
	else
	{
		return FSharedBuffer::MakeView(RangeView);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
