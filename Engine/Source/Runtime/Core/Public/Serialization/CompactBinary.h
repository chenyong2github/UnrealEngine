// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringView.h"
#include "Memory/MemoryView.h"
#include "Memory/SharedBuffer.h"
#include "Misc/Blake3.h"
#include "Misc/EnumClassFlags.h"
#include "Templates/IsTriviallyDestructible.h"

/**
 * \file
 *
 * This file declares a compact binary data format that is compatible with JSON and only slightly
 * more expressive. The format is designed to achieve fairly small encoded sizes while also being
 * efficient to read both sequentially and through random access. An atom of data in this compact
 * binary format is called a field, which can be an object, array, byte string, character string,
 * integer, floating point value, boolean, null, uuid, date/time, time span, or a hash digest for
 * referencing blobs or compact binary data that are stored externally.
 *
 * An object is a collection of name-field pairs, and an array is a collection of fields. Encoded
 * objects and arrays are both written such that they may be interpreted as a field that can then
 * be cast to an object or array. This attribute means that a blob containing compact binary data
 * is always safe to interpret as a field, which allows for easy validation as described later.
 *
 * A field can be constructed as a view of the underlying memory with \ref FCbField or can either
 * view (wrap) or own (clone or assume ownership) the underlying memory with \ref FCbFieldRef. An
 * array provides the same functionality through \ref FCbArray or \ref FCbArrayRef, and an object
 * uses \ref FCbObject or \ref FCbObjectRef.
 *
 * It is optimal use the view types when possible, and reference types only when they are needed,
 * to avoid the overhead of the atomic reference counting of the shared buffer.
 *
 * A range of data validation functionality is provided through \ref ValidateCompactBinary. Using
 * the Default mode of validation is sufficient to be able to read the data without crashing. The
 * additional validation modes are required for compatibility with other formats (Names), correct
 * byte-wise comparisons of the encoded data (Format), or storage without arbitrary padding bytes
 * leading to non-deterministic values being stored (Padding).
 *
 * Example:
 *
 * \code
 * void BeginBuild(FCbObjectRef Params)
 * {
 *     if (FSharedBufferConstPtr Data = Storage().Load(Params["Data"].AsBinaryReference()))
 *     {
 *         SetData(Data);
 *     }
 *
 *     if (Params["Resize"].AsBool())
 *     {
 *         FCbField MaxWidthField = Params["MaxWidth"]
 *         FCbField MaxHeightField = Params["MaxHeight"];
 *         if (MaxWidthField && MaxHeightField)
 *         {
 *             Resize(MaxWidthField.AsInt32(), MaxHeightField.AsInt32());
 *         }
 *     }
 *
 *     for (FCbField Format : Params["Formats"].AsArray())
 *     {
 *         BeginCompress(FName(Format.AsString()));
 *     }
 * }
 * \endcode
 */

class FCbArray;
class FCbField;
class FCbObject;
struct FDateTime;
struct FGuid;
struct FTimespan;
template <typename FuncType> class TFunctionRef;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Field types and flags for \ref FCbField.
 *
 * This is a private type and is only declared here to enable inline use below.
 *
 * DO NOT CHANGE THE VALUE OF ANY MEMBERS OF THIS ENUM!
 * BACKWARD COMPATIBILITY REQUIRES THAT THESE VALUES BE FIXED!
 * SERIALIZATION USES HARD-CODED CONSTANTS BASED ON THESE VALUES!
 */
enum class ECbFieldType : uint8
{
	/** A field type that does not occur in a valid object. */
	None            = 0x00,

	/** Null. Payload is empty. */
	Null            = 0x01,

	/**
	 * Object is an array of fields with unique non-empty names.
	 *
	 * Payload is a VarUInt byte count for the encoded fields followed by the fields.
	 */
	Object          = 0x02,
	/**
	 * UniformObject is an array of fields with the same field types and unique non-empty names.
	 *
	 * Payload is a VarUInt byte count for the encoded fields followed by the fields.
	 */
	UniformObject   = 0x03,

	/**
	 * Array is an array of fields with no name that may be of different types.
	 *
	 * Payload is a VarUInt byte count, followed by a VarUInt item count, followed by the fields.
	 */
	Array           = 0x04,
	/**
	 * UniformArray is an array of fields with no name and with the same field type.
	 *
	 * Payload is a VarUInt byte count, followed by a VarUInt item count, followed by field type,
	 * followed by the fields without their field type.
	 */
	UniformArray    = 0x05,

	/** Binary. Payload is a VarUInt byte count followed by the data. */
	Binary          = 0x06,

	/** String in UTF-8. Payload is a VarUInt byte count then an unterminated UTF-8 string. */
	String          = 0x07,

	/**
	 * Non-negative integer with the range of a 64-bit unsigned integer.
	 *
	 * Payload is the value encoded as a VarUInt.
	 */
	IntegerPositive = 0x08,
	/**
	 * Negative integer with the range of a 64-bit signed integer.
	 *
	 * Payload is the ones' complement of the value encoded as a VarUInt.
	 */
	IntegerNegative = 0x09,

	/** Single precision float. Payload is one big endian IEEE 754 binary32 float. */
	Float32         = 0x0a,
	/** Double precision float. Payload is one big endian IEEE 754 binary64 float. */
	Float64         = 0x0b,

	/** Boolean false value. Payload is empty. */
	BoolFalse       = 0x0c,
	/** Boolean true value. Payload is empty. */
	BoolTrue        = 0x0d,

	/**
	 * Reference is a reference to compact binary stored externally.
	 *
	 * Payload is a 256-bit hash digest of the compact binary.
	 */
	Reference       = 0x0e,
	/**
	 * BinaryReference is a reference to a blob stored externally.
	 *
	 * Payload is a 256-bit hash digest of the blob.
	 */
	BinaryReference = 0x0f,

	/** Hash. Payload is a 256-bit hash digest. */
	Hash            = 0x10,
	/** UUID/GUID. Payload is a 128-bit UUID as defined by RFC 4122. */
	Uuid            = 0x11,

	/**
	 * Date and time between 0001-01-01 00:00:00.0000000 and 9999-12-31 23:59:59.9999999.
	 *
	 * Payload is a big endian int64 count of 100ns ticks since 0001-01-01 00:00:00.0000000.
	 */
	DateTime        = 0x12,
	/**
	 * Difference between two date/time values.
	 *
	 * Payload is a big endian int64 count of 100ns ticks in the span, and may be negative.
	 */
	TimeSpan        = 0x13,

	/**
	 * A transient flag which indicates that the object or array containing this field has stored
	 * the field type before the payload and name. Non-uniform objects and fields will set this.
	 *
	 * Note: Since the flag must never be serialized, this bit may be repurposed in the future.
	 */
	HasFieldType    = 0x40,

	/** A persisted flag which indicates that the field has a name stored before the payload. */
	HasFieldName    = 0x80,
};

ENUM_CLASS_FLAGS(ECbFieldType);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Functions that operate on \ref ECbFieldType. */
class FCbFieldType
{
	static constexpr ECbFieldType SerializedTypeMask    = ECbFieldType(0b1011'1111);
	static constexpr ECbFieldType TypeMask              = ECbFieldType(0b0011'1111);

	static constexpr ECbFieldType ObjectMask            = ECbFieldType(0b0011'1110);
	static constexpr ECbFieldType ObjectBase            = ECbFieldType(0b0000'0010);

	static constexpr ECbFieldType ArrayMask             = ECbFieldType(0b0011'1110);
	static constexpr ECbFieldType ArrayBase             = ECbFieldType(0b0000'0100);

	static constexpr ECbFieldType IntegerMask           = ECbFieldType(0b0011'1110);
	static constexpr ECbFieldType IntegerBase           = ECbFieldType(0b0000'1000);

	static constexpr ECbFieldType FloatMask             = ECbFieldType(0b0011'1100);
	static constexpr ECbFieldType FloatBase             = ECbFieldType(0b0000'1000);

	static constexpr ECbFieldType BoolMask              = ECbFieldType(0b0011'1110);
	static constexpr ECbFieldType BoolBase              = ECbFieldType(0b0000'1100);

	static constexpr ECbFieldType AnyReferenceMask      = ECbFieldType(0b0011'1110);
	static constexpr ECbFieldType AnyReferenceBase      = ECbFieldType(0b0000'1110);

	static void StaticAssertTypeConstants();

public:
	/** The type with flags removed. */
	static constexpr inline ECbFieldType GetType(ECbFieldType Type)             { return Type & TypeMask; }
	/** The type with transient flags removed. */
	static constexpr inline ECbFieldType GetSerializedType(ECbFieldType Type)   { return Type & SerializedTypeMask; }

	static constexpr inline bool HasFieldType(ECbFieldType Type) { return EnumHasAnyFlags(Type, ECbFieldType::HasFieldType); }
	static constexpr inline bool HasFieldName(ECbFieldType Type) { return EnumHasAnyFlags(Type, ECbFieldType::HasFieldName); }

	static constexpr inline bool IsNone(ECbFieldType Type)       { return GetType(Type) == ECbFieldType::None; }
	static constexpr inline bool IsNull(ECbFieldType Type)       { return GetType(Type) == ECbFieldType::Null; }

	static constexpr inline bool IsObject(ECbFieldType Type)     { return (Type & ObjectMask) == ObjectBase; }
	static constexpr inline bool IsArray(ECbFieldType Type)      { return (Type & ArrayMask) == ArrayBase; }

	static constexpr inline bool IsBinary(ECbFieldType Type)     { return GetType(Type) == ECbFieldType::Binary; }
	static constexpr inline bool IsString(ECbFieldType Type)     { return GetType(Type) == ECbFieldType::String; }

	static constexpr inline bool IsInteger(ECbFieldType Type)    { return (Type & IntegerMask) == IntegerBase; }
	/** Whether the field is a float, or integer due to implicit conversion. */
	static constexpr inline bool IsFloat(ECbFieldType Type)      { return (Type & FloatMask) == FloatBase; }
	static constexpr inline bool IsBool(ECbFieldType Type)       { return (Type & BoolMask) == BoolBase; }

	static constexpr inline bool IsReference(ECbFieldType Type)       { return GetType(Type) == ECbFieldType::Reference; }
	static constexpr inline bool IsBinaryReference(ECbFieldType Type) { return GetType(Type) == ECbFieldType::BinaryReference; }
	static constexpr inline bool IsAnyReference(ECbFieldType Type)    { return (Type & AnyReferenceMask) == AnyReferenceBase; }

	static constexpr inline bool IsHash(ECbFieldType Type)       { return GetType(Type) == ECbFieldType::Hash; }
	static constexpr inline bool IsUuid(ECbFieldType Type)       { return GetType(Type) == ECbFieldType::Uuid; }

	static constexpr inline bool IsDateTime(ECbFieldType Type)   { return GetType(Type) == ECbFieldType::DateTime; }
	static constexpr inline bool IsTimeSpan(ECbFieldType Type)   { return GetType(Type) == ECbFieldType::TimeSpan; }

	/** Whether the type is or may contain fields of any reference type. */
	static constexpr inline bool MayContainReferences(ECbFieldType Type)
	{
		return IsObject(Type) | IsArray(Type) | IsAnyReference(Type);
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A reference to a function that is used to visit fields. */
using FCbFieldVisitor = TFunctionRef<void (FCbField)>;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Iterator for \ref FCbField[Ref] that can operate on any contiguous range of fields.
 *
 * The iterator *is* the current field that the iterator points to and exposes the full interface
 * of \ref FCbField[Ref]. An iterator that is at the end is equivalent to a field with no value.
 */
template <typename FieldType>
class TCbFieldIterator : public FieldType
{
public:
	/** Construct a field iterator that has no fields to iterate. */
	constexpr TCbFieldIterator() = default;

	/**
	 * Construct a field iterator.
	 *
	 * @param InField The first field, or the default field if there are no fields.
	 * @param InFieldsEnd A pointer to the end of the payload of the last field, or null.
	 */
	constexpr inline TCbFieldIterator(FieldType&& InField, const void* InFieldsEnd)
		: FieldType(MoveTemp(InField))
		, FieldsEnd(InFieldsEnd)
	{
	}

	/**
	 * Construct a field iterator for one field.
	 *
	 * @param InField The single field to be iterated.
	 */
	constexpr inline explicit TCbFieldIterator(const FieldType& InField)
		: FieldType(InField)
		, FieldsEnd(FieldType::GetPayloadEnd())
	{
	}

	/**
	 * Construct a field iterator for one field.
	 *
	 * @param InField The single field to be iterated.
	 */
	constexpr inline explicit TCbFieldIterator(FieldType&& InField)
		: FieldType(MoveTemp(InField))
		, FieldsEnd(FieldType::GetPayloadEnd())
	{
	}

	inline TCbFieldIterator& operator++()
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

	constexpr inline FieldType& operator*() { return *this; }
	constexpr inline FieldType* operator->() { return this; }

	using FieldType::Equals;

	template <typename OtherFieldType>
	constexpr inline bool Equals(const TCbFieldIterator<OtherFieldType>& Other) const
	{
		return FieldType::GetPayload() == Other.FieldType::GetPayload();
	}

	template <typename OtherFieldType>
	constexpr inline bool operator==(const TCbFieldIterator<OtherFieldType>& Other) const
	{
		return Equals(Other);
	}

	template <typename OtherFieldType>
	constexpr inline bool operator!=(const TCbFieldIterator<OtherFieldType>& Other) const
	{
		return !Equals(Other);
	}

	/** Calculate the hash of the field range by hashing their underlying memory. */
	inline FBlake3Hash GetRangeHash() const { return FBlake3::HashBuffer(GetFieldRangeView(*this)); }
	/** Calculate the hash of the field range by hashing their underlying memory. */
	inline void GetRangeHash(FBlake3& Hash) const { Hash.Update(GetFieldRangeView(*this)); }

	/** Invoke the visitor for every reference or binary reference in the field range. */
	inline void IterateRangeReferences(FCbFieldVisitor Visitor) const
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

protected:
	/** Returns the end of the last field, or null for an iterator at the end. */
	template <typename OtherFieldType>
	static inline const void* GetFieldsEnd(const TCbFieldIterator<OtherFieldType>& It)
	{
		return It.FieldsEnd;
	}

	/** Create a view of the field iterator. */
	template <typename OtherFieldType>
	static inline FConstMemoryView GetFieldRangeView(const TCbFieldIterator<OtherFieldType>& It)
	{
		return MakeMemoryView(It.OtherFieldType::GetFieldView().GetData(), It.FieldsEnd);
	}

private:
	friend inline TCbFieldIterator begin(const TCbFieldIterator& Iterator) { return Iterator; }
	friend inline TCbFieldIterator end(const TCbFieldIterator&) { return TCbFieldIterator(); }

private:
	template <typename OtherType>
	friend class TCbFieldIterator;

	/** Pointer to the first byte past the end of the last field. Set to null at the end. */
	const void* FieldsEnd = nullptr;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Errors that can occur when accessing a field. */
enum class ECbFieldError : uint8
{
	/** The field is not in an error state. */
	None,
	/** The value type does not match the requested type. */
	TypeError,
	/** The value is out of range for the requested type. */
	RangeError,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * An atom of data in the compact binary format.
 *
 * Accessing the value of a field is always a safe operation, even if accessed as the wrong type.
 * An invalid access will return a default value for the requested type, and set an error code on
 * the field that can be checked with GetLastError and HasLastError. A valid access will clear an
 * error from a previous invalid access.
 *
 * A field is encoded in one or more bytes, depending on its type and the type of object or array
 * that contains it. A field of an object or array which is non-uniform encodes its field type in
 * the first byte, and includes the HasFieldName flag for a field in an object. The field name is
 * encoded in a variable-length unsigned integer of its size in bytes, for named fields, followed
 * by that many bytes of the UTF-8 encoding of the name with no null terminator. The remainder of
 * the field is the payload and is described in the field type enum. Every field must be uniquely
 * addressable when encoded, which means a zero-byte field is not permitted, and only arises in a
 * uniform array of fields with no payload, where the answer is to encode as a non-uniform array.
 *
 * This type only provides a view into memory and does not perform any memory management itself.
 * Use \ref FCbFieldRef to hold a reference to the underlying memory when necessary.
 */
class FCbField
{
public:
	/** Construct a field with no name and no value. */
	constexpr FCbField() = default;

	/**
	 * Construct a field from a pointer to its data and an optional externally-provided type.
	 *
	 * @param Data Pointer to the start of the field data.
	 * @param Type HasFieldType means that Data contains the type. Otherwise, use the given type.
	 */
	CORE_API explicit FCbField(const void* Data, ECbFieldType Type = ECbFieldType::HasFieldType);

	/** Name of the field if it has one, otherwise empty. */
	constexpr inline FAnsiStringView GetName() const
	{
		return FAnsiStringView(static_cast<const ANSICHAR*>(Payload) - NameLen, NameLen);
	}

	/** Size of the field in bytes. */
	CORE_API uint64 GetSize() const;

	/**
	 * Whether this field is identical to the other field.
	 *
	 * Performs a deep comparison of any contained arrays or objects and their fields. Comparison
	 * assumes that both fields are valid and are written in the canonical format. Fields must be
	 * written in the same order in arrays and objects, and name comparison is case sensitive. If
	 * these assumptions do not hold, this may return false for equivalent inputs. Validation can
	 * be performed with \ref ValidateCompactBinary, except for field order and field name case.
	 */
	CORE_API bool Equals(const FCbField& Other) const;

	/** Calculate the hash of the field by hashing its underlying memory. */
	inline FBlake3Hash GetHash() const { return FBlake3::HashBuffer(GetFieldView()); }
	/** Calculate the hash of the field by hashing its underlying memory. */
	inline void GetHash(FBlake3& Hash) const { Hash.Update(GetFieldView()); }

	/** Invoke the visitor for every reference or binary reference in the field. */
	CORE_API void IterateReferences(FCbFieldVisitor Visitor) const;

	/** Access the field as an object. Defaults to an empty object on error. */
	CORE_API FCbObject AsObject();

	/** Access the field as an array. Defaults to an empty array on error. */
	CORE_API FCbArray AsArray();

	/** Access the field as binary. Returns the provided default on error. */
	CORE_API FConstMemoryView AsBinary(FConstMemoryView Default = FConstMemoryView());

	/** Access the field as a UTF-8 string. Returns the provided default on error. */
	CORE_API FAnsiStringView AsString(FAnsiStringView Default = FAnsiStringView());

	/** Access the field as an int8. Returns the provided default on error. */
	inline int8 AsInt8(int8 Default = 0)       { return AsInteger<int8>(Default); }
	/** Access the field as an int16. Returns the provided default on error. */
	inline int16 AsInt16(int16 Default = 0)    { return AsInteger<int16>(Default); }
	/** Access the field as an int32. Returns the provided default on error. */
	inline int32 AsInt32(int32 Default = 0)    { return AsInteger<int32>(Default); }
	/** Access the field as an int64. Returns the provided default on error. */
	inline int64 AsInt64(int64 Default = 0)    { return AsInteger<int64>(Default); }
	/** Access the field as a uint8. Returns the provided default on error. */
	inline uint8 AsUInt8(uint8 Default = 0)    { return AsInteger<uint8>(Default); }
	/** Access the field as a uint16. Returns the provided default on error. */
	inline uint16 AsUInt16(uint16 Default = 0) { return AsInteger<uint16>(Default); }
	/** Access the field as a uint32. Returns the provided default on error. */
	inline uint32 AsUInt32(uint32 Default = 0) { return AsInteger<uint32>(Default); }
	/** Access the field as a uint64. Returns the provided default on error. */
	inline uint64 AsUInt64(uint64 Default = 0) { return AsInteger<uint64>(Default); }

	/** Access the field as a float. Returns the provided default on error. */
	CORE_API float AsFloat(float Default = 0.0f);
	/** Access the field as a double. Returns the provided default on error. */
	CORE_API double AsDouble(double Default = 0.0);

	/** Access the field as a bool. Returns the provided default on error. */
	CORE_API bool AsBool(bool bDefault = false);

	/** Access the field as a hash referencing compact binary. Returns the provided default on error. */
	CORE_API FBlake3Hash AsReference(const FBlake3Hash& Default = FBlake3Hash());
	/** Access the field as a hash referencing a blob. Returns the provided default on error. */
	CORE_API FBlake3Hash AsBinaryReference(const FBlake3Hash& Default = FBlake3Hash());

	/** Access the field as a hash. Returns the provided default on error. */
	CORE_API FBlake3Hash AsHash(const FBlake3Hash& Default = FBlake3Hash());

	/** Access the field as a UUID. Returns a nil UUID on error. */
	CORE_API FGuid AsUuid();
	/** Access the field as a UUID. Returns the provided default on error. */
	CORE_API FGuid AsUuid(const FGuid& Default);

	/** Access the field as a date/time tick count. Returns the provided default on error. */
	CORE_API int64 AsDateTimeTicks(int64 Default = 0);

	/** Access the field as a date/time. Returns a date/time at the epoch on error. */
	CORE_API FDateTime AsDateTime();
	/** Access the field as a date/time. Returns the provided default on error. */
	CORE_API FDateTime AsDateTime(FDateTime Default);

	/** Access the field as a timespan tick count. Returns the provided default on error. */
	CORE_API int64 AsTimeSpanTicks(int64 Default = 0);

	/** Access the field as a timespan. Returns an empty timespan on error. */
	CORE_API FTimespan AsTimeSpan();
	/** Access the field as a timespan. Returns the provided default on error. */
	CORE_API FTimespan AsTimeSpan(FTimespan Default);

	/** True if the field has a name. */
	constexpr inline bool HasName() const           { return FCbFieldType::HasFieldName(Type); }

	constexpr inline bool IsNull() const            { return FCbFieldType::IsNull(Type); }

	constexpr inline bool IsObject() const          { return FCbFieldType::IsObject(Type); }
	constexpr inline bool IsArray() const           { return FCbFieldType::IsArray(Type); }

	constexpr inline bool IsBinary() const          { return FCbFieldType::IsBinary(Type); }
	constexpr inline bool IsString() const          { return FCbFieldType::IsString(Type); }

	/** Whether the field is an integer of unspecified range and sign. */
	constexpr inline bool IsInteger() const         { return FCbFieldType::IsInteger(Type); }
	/** Whether the field is a float, or integer that supports implicit conversion. */
	constexpr inline bool IsFloat() const           { return FCbFieldType::IsFloat(Type); }
	constexpr inline bool IsBool() const            { return FCbFieldType::IsBool(Type); }

	constexpr inline bool IsReference() const       { return FCbFieldType::IsReference(Type); }
	constexpr inline bool IsBinaryReference() const { return FCbFieldType::IsBinaryReference(Type); }
	constexpr inline bool IsAnyReference() const    { return FCbFieldType::IsAnyReference(Type); }

	constexpr inline bool IsHash() const            { return FCbFieldType::IsHash(Type); }
	constexpr inline bool IsUuid() const            { return FCbFieldType::IsUuid(Type); }

	constexpr inline bool IsDateTime() const        { return FCbFieldType::IsDateTime(Type); }
	constexpr inline bool IsTimeSpan() const        { return FCbFieldType::IsTimeSpan(Type); }

	/** Whether the field has a value. \see \ref FCbField::HasValue() */
	constexpr inline explicit operator bool() const { return HasValue(); }

	/**
	 * Whether the field has a value.
	 *
	 * All fields in a valid object or array have a value. A field with no value is returned when
	 * finding a field by name fails or when accessing an iterator past the end.
	 */
	constexpr inline bool HasValue() const          { return !FCbFieldType::IsNone(Type); };

	/** Whether the last field access encountered an error. */
	constexpr inline bool HasError() const          { return Error != ECbFieldError::None; }
	/** The type of error that occurred on the last field access, or None. */
	constexpr inline ECbFieldError GetError() const { return Error; }

protected:
	/** Returns the type of the field including any relevant flags. */
	constexpr inline ECbFieldType GetType() const { return Type; }

	/** Returns the type to use when constructing a new field after using CopyTo. */
	constexpr inline ECbFieldType GetCopyType() const { return Type; }

	/** Size of the field payload in bytes. That is the field excluding its type and name. */
	CORE_API uint64 GetPayloadSize() const;

	/** Returns the start of the value payload. */
	constexpr inline const void* GetPayload() const { return Payload; }

	/** Returns the end of the value payload. */
	inline const void* GetPayloadEnd() const { return static_cast<const uint8*>(Payload) + GetPayloadSize(); }

	/** Returns a view of the value payload, which excludes the type and name. */
	inline FConstMemoryView GetPayloadView() const { return MakeMemoryView(Payload, GetPayloadSize()); }

	/** Assign a field from a pointer to its data and an optional externally-provided type. */
	inline void Assign(const void* InData, const ECbFieldType InType)
	{
		static_assert(TIsTriviallyDestructible<FCbField>::Value,
			"This optimization requires FCbField to be trivially destructible!");
		new(this) FCbField(InData, InType);
	}

	/**
	 * Copy the field into a buffer of at least GetSize() bytes.
	 *
	 * The field type will only be copied if HasFieldType is set.
	 */
	CORE_API void CopyTo(FMutableMemoryView Buffer) const;

	/** Create a view of the field, including the type and name when present. */
	CORE_API FConstMemoryView GetFieldView() const;

private:
	/** Parameters for converting to an integer. */
	struct FIntegerParams
	{
		/** Whether the output type has a sign bit. */
		uint32 IsSigned : 1;
		/** Bits of magnitude. (7 for int8) */
		uint32 MagnitudeBits : 31;
	};

	/** Make integer params for the given integer type. */
	template <typename IntType>
	static constexpr inline FIntegerParams MakeIntegerParams()
	{
		FIntegerParams Params;
		Params.IsSigned = IntType(-1) < IntType(0);
		Params.MagnitudeBits = 8 * sizeof(IntType) - Params.IsSigned;
		return Params;
	}

	/**
	 * Access the field as the given integer type.
	 *
	 * Returns the provided default if the value cannot be represented in the output type.
	 */
	template <typename IntType>
	inline IntType AsInteger(IntType Default)
	{
		return IntType(AsInteger(uint64(Default), MakeIntegerParams<IntType>()));
	}

	CORE_API uint64 AsInteger(uint64 Default, FIntegerParams Params);

private:
	/** The field type, with the transient HasFieldType flag if the field contains its type. */
	ECbFieldType Type = ECbFieldType::None;
	/** The error (if any) that occurred on the last field access. */
	ECbFieldError Error = ECbFieldError::None;
	/** The number of bytes for the name stored before the payload. */
	uint32 NameLen = 0;
	/** The value payload, which also points to the end of the name. */
	const void* Payload = nullptr;
};

/** Iterator for \ref FCbField. \see \ref TCbFieldIterator */
class FCbFieldIterator : public TCbFieldIterator<FCbField>
{
public:
	using TCbFieldIterator::TCbFieldIterator;

	FCbFieldIterator() = default;

	/** Construct an iterator from another iterator. */
	template <typename OtherFieldType>
	inline FCbFieldIterator(const TCbFieldIterator<OtherFieldType>& It)
		: TCbFieldIterator(ImplicitConv<FCbField>(It), GetFieldsEnd(It))
	{
	}

	/** Construct an iterator for the fields in the view. */
	inline explicit FCbFieldIterator(FConstMemoryView View)
		: FCbFieldIterator(MakeFromView(View))
	{
	}

private:
	/** Construct an iterator for the fields in the view. */
	static inline FCbFieldIterator MakeFromView(FConstMemoryView View)
	{
		return !View.IsEmpty() ? FCbFieldIterator(FCbField(View.GetData()), View.GetDataEnd()) : FCbFieldIterator();
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Array of \ref FCbField that have no names.
 *
 * Accessing a field of the array requires iteration. Access by index is not provided because the
 * cost of accessing an item by index scales linearly with the index.
 *
 * This type only provides a view into memory and does not perform any memory management itself.
 * Use \ref FCbArrayRef to hold a reference to the underlying memory when necessary.
 */
class FCbArray : protected FCbField
{
public:
	/** Construct an array with no fields. */
	CORE_API FCbArray();

	/**
	 * Construct an array from a pointer to its data and an optional externally-provided type.
	 *
	 * @param Data Pointer to the start of the array data.
	 * @param Type HasFieldType means that Data contains the type. Otherwise, use the given type.
	 */
	inline explicit FCbArray(const void* Data, ECbFieldType Type = ECbFieldType::HasFieldType)
		: FCbField(Data, Type)
	{
	}

	/** Size of the array in bytes if serialized by itself with no name. */
	CORE_API uint64 GetSize() const;

	/** Number of items in the array. */
	CORE_API uint64 Num() const;

	/** Create an iterator for the fields of this array. */
	CORE_API FCbFieldIterator CreateIterator() const;

	/**
	 * Whether this array is identical to the other array.
	 *
	 * Performs a deep comparison of any contained arrays or objects and their fields. Comparison
	 * assumes that both fields are valid and are written in the canonical format. Fields must be
	 * written in the same order in arrays and objects, and name comparison is case sensitive. If
	 * these assumptions do not hold, this may return false for equivalent inputs. Validation can
	 * be done with the `All` mode to check these assumptions about the format of the inputs.
	 */
	CORE_API bool Equals(const FCbArray& Other) const;

	/** Access the array as an array field. */
	inline FCbField AsField() const { return static_cast<const FCbField&>(*this); }

	/** Construct an array from an array field. No type check is performed! */
	static FCbArray FromField(const FCbField& Field) { return FCbArray(Field); }

	/** Calculate the hash of the array if serialized by itself with no name. */
	CORE_API FBlake3Hash GetHash() const;
	/** Calculate the hash of the array if serialized by itself with no name. */
	CORE_API void GetHash(FBlake3& Hash) const;

	/** Invoke the visitor for every reference or binary reference in the array. */
	inline void IterateReferences(FCbFieldVisitor Visitor) const { CreateIterator().IterateRangeReferences(Visitor); }

protected:
	/** Returns the type to use when constructing a new array after using CopyTo. */
	constexpr inline ECbFieldType GetCopyType() const
	{
		return FCbFieldType::GetType(GetType()) | ECbFieldType::HasFieldType;
	}

	/** Copy the array into a buffer of at least GetSize() bytes. */
	CORE_API void CopyTo(FMutableMemoryView Buffer) const;

private:
	friend inline FCbFieldIterator begin(const FCbArray& Array) { return Array.CreateIterator(); }
	friend inline FCbFieldIterator end(const FCbArray&) { return FCbFieldIterator(); }

	inline explicit FCbArray(const FCbField& Field) : FCbField(Field) {}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Array of \ref FCbField that have unique names.
 *
 * Accessing the fields of an object is always a safe operation, even if the requested field does
 * not exist. Fields may be accessed by name or through iteration. When a field is requested that
 * is not found in the object, the field that it returns has no value (evaluates to false) though
 * attempting to access the empty field is also safe, as described by \ref FCbField.
 *
 * This type only provides a view into memory and does not perform any memory management itself.
 * Use \ref FCbObjectRef to hold a reference to the underlying memory when necessary.
 */
class FCbObject : protected FCbField
{
public:
	/** Construct an object with no fields. */
	CORE_API FCbObject();

	/**
	 * Construct an object from a pointer to its data and an optional externally-provided type.
	 *
	 * @param Data Pointer to the start of the object data.
	 * @param Type HasFieldType means that Data contains the type. Otherwise, use the given type.
	 */
	inline explicit FCbObject(const void* Data, ECbFieldType Type = ECbFieldType::HasFieldType)
		: FCbField(Data, Type)
	{
	}

	/** Size of the object in bytes if serialized by itself with no name. */
	CORE_API uint64 GetSize() const;

	/** Create an iterator for the fields of this object. */
	CORE_API FCbFieldIterator CreateIterator() const;

	/**
	 * Whether this object is identical to the other object.
	 *
	 * Performs a deep comparison of any contained arrays or objects and their fields. Comparison
	 * assumes that both fields are valid and are written in the canonical format. Fields must be
	 * written in the same order in arrays and objects, and name comparison is case sensitive. If
	 * these assumptions do not hold, this may return false for equivalent inputs. Validation can
	 * be done with the `All` mode to check these assumptions about the format of the inputs.
	 */
	CORE_API bool Equals(const FCbObject& Other) const;

	/**
	 * Find a field by case-sensitive name comparison.
	 *
	 * The cost of this operation scales linearly with the number of fields in the object. Prefer
	 * to iterate over the fields only once when consuming an object.
	 *
	 * @param Name The name of the field.
	 * @return The matching field if found, otherwise a field with no value.
	 */
	CORE_API FCbField Find(FAnsiStringView Name) const;

	/** Find a field by case-insensitive name comparison. \see \ref FCbObject::Find */
	CORE_API FCbField FindIgnoreCase(FAnsiStringView Name) const;

	/** Find a field by case-sensitive name comparison. \see \ref FCbObject::Find */
	inline FCbField operator[](FAnsiStringView Name) const { return Find(Name); }

	/** Access the object as an object field. */
	inline FCbField AsField() const { return static_cast<const FCbField&>(*this); }

	/** Construct an object from an object field. No type check is performed! */
	static FCbObject FromField(const FCbField& Field) { return FCbObject(Field); }

	/** Calculate the hash of the object if serialized by itself with no name. */
	CORE_API FBlake3Hash GetHash() const;
	/** Calculate the hash of the object if serialized by itself with no name. */
	CORE_API void GetHash(FBlake3& Hash) const;

	/** Invoke the visitor for every reference or binary reference in the object. */
	inline void IterateReferences(FCbFieldVisitor Visitor) const { CreateIterator().IterateRangeReferences(Visitor); }

protected:
	/** Returns the type to use when constructing a new object after using CopyTo. */
	constexpr inline ECbFieldType GetCopyType() const
	{
		return FCbFieldType::GetType(GetType()) | ECbFieldType::HasFieldType;
	}

	/** Copy the object into a buffer of at least GetSize() bytes. */
	CORE_API void CopyTo(FMutableMemoryView Buffer) const;

private:
	friend inline FCbFieldIterator begin(const FCbObject& Object) { return Object.CreateIterator(); }
	friend inline FCbFieldIterator end(const FCbObject&) { return FCbFieldIterator(); }

	inline explicit FCbObject(const FCbField& Field) : FCbField(Field) {}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A wrapper that can hold a reference to the memory that contains its value.
 *
 * When constructed with an owned buffer, or by assuming ownership, or by cloning, this reference
 * type will prevent the memory containing its value from being released. To minimize overhead, a
 * buffer pointer is only stored for owned buffers.
 */
template <typename BaseType>
class TCbBufferRef : public BaseType
{
public:
	/** Construct a default value. */
	TCbBufferRef() = default;

	/** Construct a value and hold a reference to the buffer that contains it. */
	inline explicit TCbBufferRef(const FSharedBufferConstRef& ValueBuffer)
		: BaseType(ValueBuffer->GetData())
	{
		if (ValueBuffer->IsOwned())
		{
			Buffer = ValueBuffer;
		}
	}

	/** Construct a value and hold a reference to the buffer that contains it. */
	inline explicit TCbBufferRef(const FSharedBufferConstPtr& ValueBuffer)
	{
		if (ValueBuffer)
		{
			static_cast<BaseType&>(*this) = BaseType(ValueBuffer->GetData());
			if (ValueBuffer->IsOwned())
			{
				Buffer = ValueBuffer;
			}
		}
	}

	/** Construct a value and hold a reference to the buffer that contains it. */
	inline explicit TCbBufferRef(FSharedBufferConstPtr&& ValueBuffer)
	{
		if (ValueBuffer)
		{
			static_cast<BaseType&>(*this) = BaseType(ValueBuffer->GetData());
			if (ValueBuffer->IsOwned())
			{
				Buffer = MoveTemp(ValueBuffer);
			}
		}
	}

	/** Construct a value and hold a reference to the buffer that contains it. */
	inline explicit TCbBufferRef(const FSharedBufferRef& ValueBuffer)
		: TCbBufferRef(FSharedBufferConstRef(ValueBuffer))
	{
	}

	/** Construct a value that holds a reference to the buffer that contains it. */
	inline TCbBufferRef(const BaseType& Value, const FSharedBufferConstPtr& ValueBuffer)
		: BaseType(Value)
	{
		if (ValueBuffer)
		{
			check(ValueBuffer->GetView().Contains(BaseType::GetFieldView()));
			if (ValueBuffer->IsOwned())
			{
				Buffer = ValueBuffer;
			}
		}
	}

	/** Construct a value that holds a reference to the buffer that contains it. */
	inline TCbBufferRef(const BaseType& Value, FSharedBufferConstPtr&& ValueBuffer)
		: BaseType(Value)
	{
		if (ValueBuffer)
		{
			check(ValueBuffer->GetView().Contains(BaseType::GetFieldView()));
			if (ValueBuffer->IsOwned())
			{
				Buffer = MoveTemp(ValueBuffer);
			}
		}
	}

	/** Construct a value that holds a reference to the buffer of the outer that contains it. */
	template <typename OtherBaseType>
	inline TCbBufferRef(const BaseType& Value, const TCbBufferRef<OtherBaseType>& OuterRef)
		: TCbBufferRef(Value, OuterRef.Buffer)
	{
	}

	/** Construct a value that holds a reference to the buffer of the outer that contains it. */
	template <typename OtherBaseType>
	inline TCbBufferRef(const BaseType& Value, TCbBufferRef<OtherBaseType>&& OuterRef)
		: TCbBufferRef(Value, MoveTemp(OuterRef.Buffer))
	{
	}

	/** Whether this reference has ownership of the memory in its buffer. */
	inline bool IsOwned() const { return Buffer && Buffer->IsOwned(); }

	/** Clone the value, if necessary, to a buffer that this reference has ownership of. */
	inline void MakeOwned()
	{
		if (!IsOwned())
		{
			MakeReadOnly();
		}
	}

	/** Whether this reference is backed by a read-only buffer. */
	inline bool IsReadOnly() const { return Buffer && Buffer->IsReadOnly(); }

	/** Clone the value, if necessary, to a read-only buffer. */
	inline void MakeReadOnly()
	{
		if (!IsReadOnly())
		{
			FSharedBufferPtr MutableBuffer = FSharedBuffer::Alloc(BaseType::GetSize());
			BaseType::CopyTo(*MutableBuffer);
			static_cast<BaseType&>(*this) = BaseType(MutableBuffer->GetData(), BaseType::GetCopyType());
			Buffer = FSharedBuffer::MakeReadOnly(MoveTemp(MutableBuffer));
		}
	}

	/** The buffer (if any) that contains this value. */
	inline const FSharedBufferConstPtr& GetBuffer() const { return Buffer; }

private:
	template <typename OtherType>
	friend class TCbBufferRef;

	FSharedBufferConstPtr Buffer;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Factory functions for types derived from TCbBufferRef.
 *
 * This uses the curiously recurring template pattern to construct the correct type of reference.
 * The derived type inherits from TCbBufferRef and this type to expose the factory functions.
 */
template <typename RefType, typename BaseType>
class TCbBufferRefFactory
{
public:
	/** Construct a value from an owned clone of its memory. */
	static inline RefType Clone(const void* const Data)
	{
		return Clone(BaseType(Data));
	}

	/** Construct a value from an owned clone of its memory. */
	static inline RefType Clone(const BaseType& Value)
	{
		RefType Ref = MakeView(Value);
		Ref.MakeOwned();
		return Ref;
	}

	/** Construct a value from a read-only view of its memory. */
	static inline RefType MakeView(const void* const Data)
	{
		return MakeView(BaseType(Data));
	}

	/** Construct a value from a read-only view of its memory. */
	static inline RefType MakeView(const BaseType& Value)
	{
		return RefType(Value, FSharedBufferConstPtr());
	}

	/** Construct a value and take ownership its memory. */
	template <typename DeleteFunctionOrBufferOwnerType>
	static inline RefType TakeOwnership(const void* const Data, DeleteFunctionOrBufferOwnerType&& DeleteFunctionOrBufferOwner)
	{
		const BaseType Value(Data);
		FSharedBufferConstPtr Buffer = FSharedBuffer::TakeOwnership(Data, Value.GetSize(),
			Forward<DeleteFunctionOrBufferOwnerType>(DeleteFunctionOrBufferOwner));
		return RefType(Value, FSharedBuffer::MakeReadOnly(MoveTemp(Buffer)));
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCbArrayRef;
class FCbObjectRef;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A field that can hold a reference to the memory that contains it. \see \ref TCbBufferRef */
class FCbFieldRef : public TCbBufferRef<FCbField>, public TCbBufferRefFactory<FCbFieldRef, FCbField>
{
public:
	using TCbBufferRef::TCbBufferRef;

	/** Access the field as an object. Defaults to an empty object on error. */
	inline FCbObjectRef AsObjectRef() &;

	/** Access the field as an object. Defaults to an empty object on error. */
	inline FCbObjectRef AsObjectRef() &&;

	/** Access the field as an array. Defaults to an empty array on error. */
	inline FCbArrayRef AsArrayRef() &;

	/** Access the field as an array. Defaults to an empty array on error. */
	inline FCbArrayRef AsArrayRef() &&;
};

/** Iterator for \ref FCbFieldRef. \see \ref TCbFieldIterator */
class FCbFieldRefIterator : public TCbFieldIterator<FCbFieldRef>
{
public:
	using TCbFieldIterator::TCbFieldIterator;

	/** Construct a field iterator that has no fields to iterate. */
	constexpr FCbFieldRefIterator() = default;

	/** Construct an iterator that holds a reference to the buffer that contains its fields. */
	inline FCbFieldRefIterator(const FCbFieldIterator& It, const FSharedBufferConstPtr& ValueBuffer)
		: TCbFieldIterator(MakeFieldRef(It, ValueBuffer), GetFieldsEnd(It))
	{
	}

	/** Construct an iterator that holds a reference to the buffer that contains its fields. */
	inline FCbFieldRefIterator(const FCbFieldIterator& It, FSharedBufferConstPtr&& ValueBuffer)
		: TCbFieldIterator(MakeFieldRef(It, MoveTemp(ValueBuffer)), GetFieldsEnd(It))
	{
	}

	/** Construct an iterator and hold a reference to the buffer that contains its fields. */
	inline explicit FCbFieldRefIterator(const FSharedBufferConstPtr& ValueBuffer)
		: FCbFieldRefIterator(MakeFieldIterator(ValueBuffer), ValueBuffer)
	{
	}

	/** Construct an iterator and hold a reference to the buffer that contains its fields. */
	inline explicit FCbFieldRefIterator(FSharedBufferConstPtr&& ValueBuffer)
		: FCbFieldRefIterator(MakeFieldIterator(ValueBuffer), MoveTemp(ValueBuffer))
	{
	}

private:
	/** Make a field ref from an iterator and the buffer that contains its fields. */
	template <typename BufferType>
	static inline FCbFieldRef MakeFieldRef(const FCbFieldIterator& It, BufferType&& ValueBuffer)
	{
		if (ValueBuffer)
		{
			check(ValueBuffer->GetView().Contains(GetFieldRangeView(It)));
		}
		return FCbFieldRef(It, Forward<BufferType>(ValueBuffer));
	}

	/** Make a field iterator from the possibly-null buffer that contains its fields. */
	static inline FCbFieldIterator MakeFieldIterator(const FSharedBufferConstPtr& ValueBuffer)
	{
		return ValueBuffer ? FCbFieldIterator(ValueBuffer->GetView()) : FCbFieldIterator();
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** An array that can hold a reference to the memory that contains it. \see \ref TCbBufferRef */
class FCbArrayRef : public TCbBufferRef<FCbArray>, public TCbBufferRefFactory<FCbArrayRef, FCbArray>
{
public:
	using TCbBufferRef::TCbBufferRef;

	/** Create an iterator for the fields of this array. */
	inline FCbFieldRefIterator CreateRefIterator() const
	{
		return FCbFieldRefIterator(CreateIterator(), GetBuffer());
	}

	/** Access the array as an array field. */
	inline FCbFieldRef AsFieldRef() const &
	{
		return FCbFieldRef(FCbArray::AsField(), *this);
	}

	/** Access the array as an array field. */
	inline FCbFieldRef AsFieldRef() &&
	{
		return FCbFieldRef(FCbArray::AsField(), MoveTemp(*this));
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** An object that can hold a reference to the memory that contains it. \see \ref TCbBufferRef */
class FCbObjectRef : public TCbBufferRef<FCbObject>, public TCbBufferRefFactory<FCbObjectRef, FCbObject>
{
public:
	using TCbBufferRef::TCbBufferRef;

	/** Create an iterator for the fields of this object. */
	inline FCbFieldRefIterator CreateRefIterator() const
	{
		return FCbFieldRefIterator(CreateIterator(), GetBuffer());
	}

	/** Find a field by case-sensitive name comparison. */
	inline FCbFieldRef FindRef(FAnsiStringView Name) const
	{
		if (::FCbField Field = Find(Name))
		{
			return FCbFieldRef(Field, *this);
		}
		return FCbFieldRef();
	}

	/** Find a field by case-insensitive name comparison. */
	inline FCbFieldRef FindRefIgnoreCase(FAnsiStringView Name) const
	{
		if (::FCbField Field = FindIgnoreCase(Name))
		{
			return FCbFieldRef(Field, *this);
		}
		return FCbFieldRef();
	}

	/** Access the object as an object field. */
	inline FCbFieldRef AsFieldRef() const &
	{
		return FCbFieldRef(FCbObject::AsField(), *this);
	}

	/** Access the object as an object field. */
	inline FCbFieldRef AsFieldRef() &&
	{
		return FCbFieldRef(FCbObject::AsField(), MoveTemp(*this));
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline FCbObjectRef FCbFieldRef::AsObjectRef() &
{
	return IsObject() ? FCbObjectRef(AsObject(), *this) : FCbObjectRef();
}

inline FCbObjectRef FCbFieldRef::AsObjectRef() &&
{
	return IsObject() ? FCbObjectRef(AsObject(), MoveTemp(*this)) : FCbObjectRef();
}

inline FCbArrayRef FCbFieldRef::AsArrayRef() &
{
	return IsArray() ? FCbArrayRef(AsArray(), *this) : FCbArrayRef();
}

inline FCbArrayRef FCbFieldRef::AsArrayRef() &&
{
	return IsArray() ? FCbArrayRef(AsArray(), MoveTemp(*this)) : FCbArrayRef();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Determine the size in bytes of a one compact binary field at the start of the view.
 *
 * This may be called on an incomplete or invalid field, in which case the returned size is zero.
 * A size can always be extracted from a valid field with no name if a view of at least the first
 * 10 bytes is provided, regardless of field size. For fields with names, the size of view needed
 * to calculate a size is at most 10 + MaxNameLen + MeasureVarUInt(MaxNameLen).
 *
 * This function can be used when streaming a field, for example, to determine the size of buffer
 * to fill before attempting to construct a field from it.
 *
 * @param View A memory view that may contain the start of a field.
 * @param Type HasFieldType means that View contains the type. Otherwise, use the given type.
 */
CORE_API uint64 MeasureCompactBinary(FConstMemoryView View, ECbFieldType Type = ECbFieldType::HasFieldType);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
