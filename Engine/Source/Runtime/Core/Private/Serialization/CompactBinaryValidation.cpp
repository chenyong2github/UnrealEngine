// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/CompactBinaryValidation.h"

#include "Misc/ByteSwap.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/VarInt.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace CompactBinaryValidationPrivate
{

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

/**
 * Adds the given error(s) to the error mask.
 *
 * This function exists to make validation errors easier to debug by providing one location to set a breakpoint.
 */
FORCENOINLINE static void AddError(ECbValidateError& OutError, const ECbValidateError InError)
{
	OutError |= InError;
}

/**
 * Validate and read a field type from the view.
 *
 * A type argument with the HasFieldType flag indicates that the type will not be read from the view.
 */
static ECbFieldType ValidateCbFieldType(FConstMemoryView& View, ECbValidateMode Mode, ECbValidateError& Error, ECbFieldType Type = ECbFieldType::HasFieldType)
{
	if (FCbFieldType::HasFieldType(Type))
	{
		if (View.GetSize() >= 1)
		{
			Type = *static_cast<const ECbFieldType*>(View.GetData());
			View += 1;
			if (FCbFieldType::HasFieldType(Type))
			{
				AddError(Error, ECbValidateError::InvalidType);
			}
		}
		else
		{
			AddError(Error, ECbValidateError::OutOfBounds);
			View.Reset();
			return ECbFieldType::None;
		}
	}

	if (FCbFieldType::GetType(Type) > ECbFieldType::TimeSpan)
	{
		AddError(Error, ECbValidateError::InvalidType);
		View.Reset();
	}

	return Type;
}

/**
 * Validate and read an unsigned integer from the view.
 *
 * Modifies the view to start at the end of the value, and adds error flags if applicable.
 */
static uint64 ValidateCbUInt(FConstMemoryView& View, ECbValidateMode Mode, ECbValidateError& Error)
{
	if (View.GetSize() > 0 && View.GetSize() >= MeasureVarUInt(View.GetData()))
	{
		uint32 ValueByteCount;
		const uint64 Value = ReadVarUInt(View.GetData(), ValueByteCount);
		if (EnumHasAnyFlags(Mode, ECbValidateMode::Format) && ValueByteCount > MeasureVarUInt(Value))
		{
			AddError(Error, ECbValidateError::InvalidInteger);
		}
		View += ValueByteCount;
		return Value;
	}
	else
	{
		AddError(Error, ECbValidateError::OutOfBounds);
		View.Reset();
		return 0;
	}
}

/**
 * Validate a 64-bit floating point value from the view.
 *
 * Modifies the view to start at the end of the value, and adds error flags if applicable.
 */
static void ValidateCbFloat64(FConstMemoryView& View, ECbValidateMode Mode, ECbValidateError& Error)
{
	if (View.GetSize() >= sizeof(double))
	{
		if (EnumHasAnyFlags(Mode, ECbValidateMode::Format))
		{
			const uint64 RawValue = NETWORK_ORDER64(CompactBinaryValidationPrivate::ReadUnaligned<uint64>(View.GetData()));
			const double Value = reinterpret_cast<const double&>(RawValue);
			if (Value == double(float(Value)))
			{
				AddError(Error, ECbValidateError::InvalidFloat);
			}
		}
		View += sizeof(double);
	}
	else
	{
		AddError(Error, ECbValidateError::OutOfBounds);
		View.Reset();
	}
}

/**
 * Validate and read a string from the view.
 *
 * Modifies the view to start at the end of the string, and adds error flags if applicable.
 */
static FAnsiStringView ValidateCbString(FConstMemoryView& View, ECbValidateMode Mode, ECbValidateError& Error)
{
	const uint64 NameSize = ValidateCbUInt(View, Mode, Error);
	if (View.GetSize() >= NameSize)
	{
		const FAnsiStringView Name(static_cast<const ANSICHAR*>(View.GetData()), static_cast<int32>(NameSize));
		View += NameSize;
		return Name;
	}
	else
	{
		AddError(Error, ECbValidateError::OutOfBounds);
		View.Reset();
		return FAnsiStringView();
	}
}

static FCbField ValidateCbField(FConstMemoryView& View, ECbValidateMode Mode, ECbValidateError& Error, ECbFieldType ExternalType);

/** A type that checks whether all validated fields are of the same type. */
class FCbUniformFieldsValidator
{
public:
	inline explicit FCbUniformFieldsValidator(ECbFieldType InExternalType)
		: ExternalType(InExternalType)
	{
	}

	inline FCbField ValidateField(FConstMemoryView& View, ECbValidateMode Mode, ECbValidateError& Error)
	{
		const void* const FieldData = View.GetData();
		if (FCbField Field = ValidateCbField(View, Mode, Error, ExternalType))
		{
			++FieldCount;
			if (FCbFieldType::HasFieldType(ExternalType))
			{
				const ECbFieldType FieldType = *static_cast<const ECbFieldType*>(FieldData);
				if (FieldCount == 1)
				{
					FirstType = FieldType;
				}
				else if (FieldType != FirstType)
				{
					bUniform = false;
				}
			}
			return Field;
		}

		// It may not safe to check for uniformity if the field was invalid.
		bUniform = false;
		return FCbField();
	}

	inline bool IsUniform() const { return FieldCount > 0 && bUniform; }

private:
	uint32 FieldCount = 0;
	bool bUniform = true;
	ECbFieldType FirstType = ECbFieldType::None;
	ECbFieldType ExternalType;
};

static void ValidateCbObject(FConstMemoryView& View, ECbValidateMode Mode, ECbValidateError& Error, ECbFieldType ObjectType)
{
	const uint64 Size = ValidateCbUInt(View, Mode, Error);
	FConstMemoryView ObjectView = View.Left(Size);
	View += Size;

	if (Size > 0)
	{
		TArray<FAnsiStringView, TInlineAllocator<16>> Names;

		const bool bUniformObject = FCbFieldType::GetType(ObjectType) == ECbFieldType::UniformObject;
		const ECbFieldType ExternalType = bUniformObject ? ValidateCbFieldType(ObjectView, Mode, Error) : ECbFieldType::HasFieldType;
		FCbUniformFieldsValidator UniformValidator(ExternalType);
		do
		{
			if (FCbField Field = UniformValidator.ValidateField(ObjectView, Mode, Error))
			{
				if (EnumHasAnyFlags(Mode, ECbValidateMode::Names))
				{
					if (Field.HasName())
					{
						Names.Add(Field.GetName());
					}
					else
					{
						AddError(Error, ECbValidateError::MissingName);
					}
				}
			}
		}
		while (!ObjectView.IsEmpty());

		if (EnumHasAnyFlags(Mode, ECbValidateMode::Names) && Names.Num() > 1)
		{
			Algo::Sort(Names, [](FAnsiStringView L, FAnsiStringView R) { return L.Compare(R) < 0; });
			for (const FAnsiStringView* NamesIt = Names.GetData(), *NamesEnd = NamesIt + Names.Num() - 1; NamesIt != NamesEnd; ++NamesIt)
			{
				if (NamesIt[0].Equals(NamesIt[1]))
				{
					AddError(Error, ECbValidateError::DuplicateName);
					break;
				}
			}
		}

		if (!bUniformObject && EnumHasAnyFlags(Mode, ECbValidateMode::Format) && UniformValidator.IsUniform())
		{
			AddError(Error, ECbValidateError::NonUniformObject);
		}
	}
}

static void ValidateCbArray(FConstMemoryView& View, ECbValidateMode Mode, ECbValidateError& Error, ECbFieldType ArrayType)
{
	const uint64 Size = ValidateCbUInt(View, Mode, Error);
	FConstMemoryView ArrayView = View.Left(Size);
	View += Size;

	const uint64 Count = ValidateCbUInt(ArrayView, Mode, Error);
	const uint64 FieldsSize = ArrayView.GetSize();
	const bool bUniformArray = FCbFieldType::GetType(ArrayType) == ECbFieldType::UniformArray;
	const ECbFieldType ExternalType = bUniformArray ? ValidateCbFieldType(ArrayView, Mode, Error) : ECbFieldType::HasFieldType;
	FCbUniformFieldsValidator UniformValidator(ExternalType);

	for (uint64 Index = 0; Index < Count; ++Index)
	{
		if (FCbField Field = UniformValidator.ValidateField(ArrayView, Mode, Error))
		{
			if (Field.HasName() && EnumHasAnyFlags(Mode, ECbValidateMode::Names))
			{
				AddError(Error, ECbValidateError::ArrayName);
			}
		}
	}

	if (!bUniformArray && EnumHasAnyFlags(Mode, ECbValidateMode::Format) && UniformValidator.IsUniform() && FieldsSize > Count)
	{
		AddError(Error, ECbValidateError::NonUniformArray);
	}
}

static FCbField ValidateCbField(FConstMemoryView& View, ECbValidateMode Mode, ECbValidateError& Error, const ECbFieldType ExternalType = ECbFieldType::HasFieldType)
{
	const FConstMemoryView FieldView = View;
	const ECbFieldType Type = ValidateCbFieldType(View, Mode, Error, ExternalType);
	const FAnsiStringView Name = FCbFieldType::HasFieldName(Type) ? ValidateCbString(View, Mode, Error) : FAnsiStringView();

	auto ValidateFixedPayload = [&View, &Error](uint32 PayloadSize)
	{
		if (View.GetSize() >= PayloadSize)
		{
			View += PayloadSize;
		}
		else
		{
			AddError(Error, ECbValidateError::OutOfBounds);
			View.Reset();
		}
	};

	if (EnumHasAnyFlags(Error, ECbValidateError::OutOfBounds | ECbValidateError::InvalidType))
	{
		return FCbField();
	}

	switch (FCbFieldType::GetType(Type))
	{
	default:
	case ECbFieldType::None:
		AddError(Error, ECbValidateError::InvalidType);
		View.Reset();
		break;
	case ECbFieldType::Null:
	case ECbFieldType::BoolFalse:
	case ECbFieldType::BoolTrue:
		if (FieldView == View)
		{
			// Reset the view because a zero-sized field can cause infinite field iteration.
			AddError(Error, ECbValidateError::InvalidType);
			View.Reset();
		}
		break;
	case ECbFieldType::Object:
	case ECbFieldType::UniformObject:
		ValidateCbObject(View, Mode, Error, FCbFieldType::GetType(Type));
		break;
	case ECbFieldType::Array:
	case ECbFieldType::UniformArray:
		ValidateCbArray(View, Mode, Error, FCbFieldType::GetType(Type));
		break;
	case ECbFieldType::Binary:
	{
		const uint64 ValueSize = ValidateCbUInt(View, Mode, Error);
		if (View.GetSize() < ValueSize)
		{
			AddError(Error, ECbValidateError::OutOfBounds);
			View.Reset();
		}
		else
		{
			View += ValueSize;
		}
		break;
	}
	case ECbFieldType::String:
		ValidateCbString(View, Mode, Error);
		break;
	case ECbFieldType::IntegerPositive:
		ValidateCbUInt(View, Mode, Error);
		break;
	case ECbFieldType::IntegerNegative:
		ValidateCbUInt(View, Mode, Error);
		break;
	case ECbFieldType::Float32:
		ValidateFixedPayload(4);
		break;
	case ECbFieldType::Float64:
		ValidateCbFloat64(View, Mode, Error);
		break;
	case ECbFieldType::Reference:
	case ECbFieldType::BinaryReference:
	case ECbFieldType::Hash:
		ValidateFixedPayload(32);
		break;
	case ECbFieldType::Uuid:
		ValidateFixedPayload(16);
		break;
	case ECbFieldType::DateTime:
	case ECbFieldType::TimeSpan:
		ValidateFixedPayload(8);
		break;
	}

	if (EnumHasAnyFlags(Error, ECbValidateError::OutOfBounds | ECbValidateError::InvalidType))
	{
		return FCbField();
	}

	return FCbField(FieldView.GetData(), ExternalType);
}

ECbValidateError ValidateCompactBinary(FConstMemoryView View, ECbValidateMode Mode, ECbFieldType Type)
{
	ECbValidateError Error = ECbValidateError::None;
	ValidateCbField(View, Mode, Error, Type);
	if (!View.IsEmpty() && EnumHasAnyFlags(Mode, ECbValidateMode::Padding))
	{
		AddError(Error, ECbValidateError::Padding);
	}
	return Error;
}

ECbValidateError ValidateCompactBinaryRange(FConstMemoryView View, ECbValidateMode Mode)
{
	ECbValidateError Error = ECbValidateError::None;
	while (!View.IsEmpty())
	{
		ValidateCbField(View, Mode, Error);
	}
	return Error;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
