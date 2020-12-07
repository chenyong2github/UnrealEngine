// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CompactBinarySerialization.h"

#include "Serialization/VarInt.h"
#include "Templates/IdentityFunctor.h"
#include "Templates/Invoke.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint64 MeasureCompactBinary(FConstMemoryView View, ECbFieldType Type)
{
	uint64 Size;
	return TryMeasureCompactBinary(View, Type, Size, Type) ? Size : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool TryMeasureCompactBinary(FConstMemoryView View, ECbFieldType& OutType, uint64& OutSize, ECbFieldType Type)
{
	uint64 Size = 0;

	if (FCbFieldType::HasFieldType(Type))
	{
		if (View.GetSize() == 0)
		{
			OutType = ECbFieldType::None;
			OutSize = 1;
			return false;
		}

		Type = *static_cast<const ECbFieldType*>(View.GetData());
		View += 1;
		Size += 1;
	}

	bool bDynamicSize = false;
	uint64 FixedSize = 0;
	switch (FCbFieldType::GetType(Type))
	{
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
		bDynamicSize = true;
		break;
	case ECbFieldType::Float32:
		FixedSize = 4;
		break;
	case ECbFieldType::Float64:
		FixedSize = 8;
		break;
	case ECbFieldType::BoolFalse:
	case ECbFieldType::BoolTrue:
		break;
	case ECbFieldType::Reference:
	case ECbFieldType::BinaryReference:
	case ECbFieldType::Hash:
		FixedSize = 32;
		break;
	case ECbFieldType::Uuid:
		FixedSize = 16;
		break;
	case ECbFieldType::DateTime:
	case ECbFieldType::TimeSpan:
		FixedSize = 8;
		break;
	case ECbFieldType::None:
	default:
		OutType = ECbFieldType::None;
		OutSize = 0;
		return false;
	}

	OutType = Type;

	if (FCbFieldType::HasFieldName(Type))
	{
		if (View.GetSize() == 0)
		{
			OutSize = Size + 1;
			return false;
		}

		uint32 NameLenByteCount = MeasureVarUInt(View.GetData());
		if (View.GetSize() < NameLenByteCount)
		{
			OutSize = Size + NameLenByteCount;
			return false;
		}

		const uint64 NameLen = ReadVarUInt(View.GetData(), NameLenByteCount);
		const uint64 NameSize = NameLen + NameLenByteCount;

		if (bDynamicSize && View.GetSize() < NameSize)
		{
			OutSize = Size + NameSize;
			return false;
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
		if (View.GetSize() == 0)
		{
			OutSize = Size + 1;
			return false;
		}
		else
		{
			uint32 PayloadSizeByteCount = MeasureVarUInt(View.GetData());
			if (View.GetSize() < PayloadSizeByteCount)
			{
				OutSize = Size + PayloadSizeByteCount;
				return false;
			}
			const uint64 PayloadSize = ReadVarUInt(View.GetData(), PayloadSizeByteCount);
			OutSize = Size + PayloadSize + PayloadSizeByteCount;
			return true;
		}
	case ECbFieldType::IntegerPositive:
	case ECbFieldType::IntegerNegative:
		if (View.GetSize() == 0)
		{
			OutSize = Size + 1;
			return false;
		}
		OutSize = Size + MeasureVarUInt(View.GetData());
		return true;
	default:
		OutSize = Size + FixedSize;
		return true;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCbFieldRef LoadCompactBinary(FArchive& Ar, FCbBufferAllocator Allocator)
{
	TArray<uint8, TInlineAllocator<64>> HeaderBytes;
	ECbFieldType FieldType;
	uint64 FieldSize = 1;

	// Read in small increments until the total field size is known, to avoid reading too far.
	for (;;)
	{
		const int32 ReadSize = int32(FieldSize - HeaderBytes.Num());
		const int32 ReadOffset = HeaderBytes.AddUninitialized(ReadSize);
		Ar.Serialize(HeaderBytes.GetData() + ReadOffset, ReadSize);
		if (TryMeasureCompactBinary(MakeMemoryView(HeaderBytes), FieldType, FieldSize))
		{
			break;
		}
		checkf(FieldSize > 0, TEXT("Failed to load from invalid compact binary data."), FieldSize);
	}

	// Allocate the buffer, copy the header, and read the remainder of the field.
	FSharedBufferPtr Buffer = Allocator(FieldType, FieldSize);
	FMutableMemoryView View = Buffer->GetView();
	FMemory::Memcpy(View.GetData(), HeaderBytes.GetData(), HeaderBytes.Num());
	View += HeaderBytes.Num();
	if (!View.IsEmpty())
	{
		Ar.Serialize(View.GetData(), static_cast<int64>(View.GetSize()));
	}
	return FCbFieldRef(MoveTemp(Buffer));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SaveCompactBinary(FArchive& Ar, const FCbField& Field)
{
	check(Ar.IsSaving());
	Field.CopyTo(Ar);
}

void SaveCompactBinary(FArchive& Ar, const FCbArray& Array)
{
	check(Ar.IsSaving());
	Array.CopyTo(Ar);
}

void SaveCompactBinary(FArchive& Ar, const FCbObject& Object)
{
	check(Ar.IsSaving());
	Object.CopyTo(Ar);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename T, typename ConvertType>
static FArchive& SerializeCompactBinary(FArchive& Ar, T& Value, ConvertType&& Convert)
{
	if (Ar.IsLoading())
	{
		Value = Invoke(Forward<ConvertType>(Convert),
			LoadCompactBinary(Ar, [](ECbFieldType Type, uint64 Size)
			{
				return FSharedBuffer::Alloc(Size);
			}));
	}
	else if (Ar.IsSaving())
	{
		Value.CopyTo(Ar);
	}
	else
	{
		checkNoEntry();
	}
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FCbFieldRef& Field)
{
	return SerializeCompactBinary(Ar, Field, FIdentityFunctor());
}

FArchive& operator<<(FArchive& Ar, FCbArrayRef& Array)
{
	return SerializeCompactBinary(Ar, Array, [](FCbFieldRef&& Field) { return MoveTemp(Field).AsArrayRef(); });
}

FArchive& operator<<(FArchive& Ar, FCbObjectRef& Object)
{
	return SerializeCompactBinary(Ar, Object, [](FCbFieldRef&& Field) { return MoveTemp(Field).AsObjectRef(); });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
