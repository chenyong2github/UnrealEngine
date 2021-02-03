// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringView.h"
#include "IO/IoHash.h"
#include "Memory/MemoryView.h"
#include "Memory/SharedBuffer.h"
#include "Misc/EnumClassFlags.h"
#include "Templates/IsTriviallyDestructible.h"

/**
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
 * A field can be constructed as a view of the underlying memory with FCbField or can either view
 * or own (clone or take ownership) the underlying memory with FCbFieldRef. An array provides the
 * same behavior with FCbArray or FCbArrayRef, and an object uses FCbObject or FCbObjectRef.
 *
 * It is optimal use the view types when possible, and reference types only when they are needed,
 * to avoid the overhead of the atomic reference counting of the shared buffer.
 *
 * A suite of validation functionality is provided by ValidateCompactBinary and its siblings. The
 * Default mode provides a guarantee that the data can be consumed without a crash. Documentation
 * of the other modes is available on ECbValidateMode.
 *
 * Example:
 *
 * void BeginBuild(FCbObjectRef Params)
 * {
 *     if (FSharedBuffer Data = Storage().Load(Params["Data"].AsBinaryAttachment()))
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
 */

class FArchive;
class FBlake3;
class FCbArray;
class FCbField;
class FCbObject;
struct FDateTime;
struct FGuid;
struct FTimespan;
template <typename FuncType> class TFunctionRef;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Field types and flags for FCbField.
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
	 * CompactBinaryAttachment is a reference to a compact binary attachment stored externally.
	 *
	 * Payload is a 160-bit hash digest of the referenced compact binary data.
	 */
	CompactBinaryAttachment = 0x0e,
	/**
	 * BinaryAttachment is a reference to a binary attachment stored externally.
	 *
	 * Payload is a 160-bit hash digest of the referenced binary data.
	 */
	BinaryAttachment = 0x0f,

	/** Hash. Payload is a 160-bit hash digest. */
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

/** Functions that operate on ECbFieldType. */
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

	static constexpr ECbFieldType AttachmentMask        = ECbFieldType(0b0011'1110);
	static constexpr ECbFieldType AttachmentBase        = ECbFieldType(0b0000'1110);

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

	static constexpr inline bool IsCompactBinaryAttachment(ECbFieldType Type) { return GetType(Type) == ECbFieldType::CompactBinaryAttachment; }
	static constexpr inline bool IsBinaryAttachment(ECbFieldType Type)        { return GetType(Type) == ECbFieldType::BinaryAttachment; }
	static constexpr inline bool IsAttachment(ECbFieldType Type) { return (Type & AttachmentMask) == AttachmentBase; }

	static constexpr inline bool IsHash(ECbFieldType Type)       { return GetType(Type) == ECbFieldType::Hash; }
	static constexpr inline bool IsUuid(ECbFieldType Type)       { return GetType(Type) == ECbFieldType::Uuid; }

	static constexpr inline bool IsDateTime(ECbFieldType Type)   { return GetType(Type) == ECbFieldType::DateTime; }
	static constexpr inline bool IsTimeSpan(ECbFieldType Type)   { return GetType(Type) == ECbFieldType::TimeSpan; }

	/** Whether the type is or may contain fields of any attachment type. */
	static constexpr inline bool MayContainAttachments(ECbFieldType Type)
	{
		// The use of !! will suppress V792 from static analysis. Using //-V792 did not work.
		return !!IsObject(Type) | !!IsArray(Type) | !!IsAttachment(Type);
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A reference to a function that is used to visit fields. */
using FCbFieldVisitor = TFunctionRef<void (FCbField)>;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Iterator for FCbField[Ref] that can operate on any contiguous range of fields.
 *
 * The iterator *is* the current field that the iterator points to and exposes the full interface
 * of FCbField[Ref]. An iterator that is at the end is equivalent to a field with no value.
 *
 * The iterator represents a range of fields from the current field to the last field.
 */
template <typename FieldType>
class TCbFieldIterator : public FieldType
{
public:
	/** Construct an empty field range. */
	constexpr TCbFieldIterator() = default;

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

	inline TCbFieldIterator operator++(int)
	{
		TCbFieldIterator It(*this);
		++*this;
		return It;
	}

	constexpr inline FieldType& operator*() { return *this; }
	constexpr inline FieldType* operator->() { return this; }

	/** Reset this to an empty field range. */
	inline void Reset() { *this = TCbFieldIterator(); }

	/** Returns the size of the fields in the range in bytes. */
	CORE_API uint64 GetRangeSize() const;

	/** Calculate the hash of every field in the range. */
	CORE_API FIoHash GetRangeHash() const;
	/** Calculate the hash of every field in the range. */
	CORE_API void GetRangeHash(FBlake3& Hash) const;

	using FieldType::Equals;

	template <typename OtherFieldType>
	constexpr inline bool Equals(const TCbFieldIterator<OtherFieldType>& Other) const
	{
		return FieldType::GetPayload() == Other.OtherFieldType::GetPayload() && FieldsEnd == Other.FieldsEnd;
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

	/** Copy the field range into a buffer of exactly GetRangeSize() bytes. */
	CORE_API void CopyRangeTo(FMutableMemoryView Buffer) const;

	/** Copy the field range into an archive, as if calling CopyTo on every field. */
	CORE_API void CopyRangeTo(FArchive& Ar) const;

	/** Invoke the visitor for every attachment in the field range. */
	CORE_API void IterateRangeAttachments(FCbFieldVisitor Visitor) const;

	/** Create a view of every field in the range. */
	inline FMemoryView GetRangeView() const
	{
		return MakeMemoryView(FieldType::GetView().GetData(), FieldsEnd);
	}

	/**
	 * Try to get a view of every field in the range as they would be serialized.
	 *
	 * A serialized view is not available if the underlying fields have an externally-provided type.
	 * Access the serialized form of such ranges using FCbFieldRefIterator::CloneRange.
	 */
	inline bool TryGetSerializedRangeView(FMemoryView& OutView) const
	{
		if (FCbFieldType::HasFieldType(FieldType::GetType()))
		{
			OutView = GetRangeView();
			return true;
		}
		return false;
	}

protected:
	/** Construct a field range that contains exactly one field. */
	constexpr inline explicit TCbFieldIterator(FieldType InField)
		: FieldType(MoveTemp(InField))
		, FieldsEnd(FieldType::GetPayloadEnd())
	{
	}

	/**
	 * Construct a field range from the first field and a pointer to the end of the last field.
	 *
	 * @param InField The first field, or the default field if there are no fields.
	 * @param InFieldsEnd A pointer to the end of the payload of the last field, or null.
	 */
	constexpr inline TCbFieldIterator(FieldType&& InField, const void* InFieldsEnd)
		: FieldType(MoveTemp(InField))
		, FieldsEnd(InFieldsEnd)
	{
	}

	/** Returns the end of the last field, or null for an iterator at the end. */
	template <typename OtherFieldType>
	static inline const void* GetFieldsEnd(const TCbFieldIterator<OtherFieldType>& It)
	{
		return It.FieldsEnd;
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
 * Use FCbFieldRef to hold a reference to the underlying memory when necessary.
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

	/** Returns the name of the field if it has a name, otherwise an empty view. */
	constexpr inline FAnsiStringView GetName() const
	{
		return FAnsiStringView(static_cast<const ANSICHAR*>(Payload) - NameLen, NameLen);
	}

	/** Access the field as an object. Defaults to an empty object on error. */
	CORE_API FCbObject AsObject();

	/** Access the field as an array. Defaults to an empty array on error. */
	CORE_API FCbArray AsArray();

	/** Access the field as binary. Returns the provided default on error. */
	CORE_API FMemoryView AsBinary(FMemoryView Default = FMemoryView());

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

	/** Access the field as a hash referencing a compact binary attachment. Returns the provided default on error. */
	CORE_API FIoHash AsCompactBinaryAttachment(const FIoHash& Default = FIoHash());
	/** Access the field as a hash referencing a binary attachment. Returns the provided default on error. */
	CORE_API FIoHash AsBinaryAttachment(const FIoHash& Default = FIoHash());
	/** Access the field as a hash referencing an attachment. Returns the provided default on error. */
	CORE_API FIoHash AsAttachment(const FIoHash& Default = FIoHash());

	/** Access the field as a hash. Returns the provided default on error. */
	CORE_API FIoHash AsHash(const FIoHash& Default = FIoHash());

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

	constexpr inline bool IsCompactBinaryAttachment() const { return FCbFieldType::IsCompactBinaryAttachment(Type); }
	constexpr inline bool IsBinaryAttachment() const        { return FCbFieldType::IsBinaryAttachment(Type); }
	constexpr inline bool IsAttachment() const              { return FCbFieldType::IsAttachment(Type); }

	constexpr inline bool IsHash() const            { return FCbFieldType::IsHash(Type); }
	constexpr inline bool IsUuid() const            { return FCbFieldType::IsUuid(Type); }

	constexpr inline bool IsDateTime() const        { return FCbFieldType::IsDateTime(Type); }
	constexpr inline bool IsTimeSpan() const        { return FCbFieldType::IsTimeSpan(Type); }

	/** Whether the field has a value. */
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

	/** Returns the size of the field in bytes, including the type and name. */
	CORE_API uint64 GetSize() const;

	/** Calculate the hash of the field, including the type and name. */
	CORE_API FIoHash GetHash() const;
	/** Calculate the hash of the field, including the type and name. */
	CORE_API void GetHash(FBlake3& Hash) const;

	/**
	 * Whether this field is identical to the other field.
	 *
	 * Performs a deep comparison of any contained arrays or objects and their fields. Comparison
	 * assumes that both fields are valid and are written in the canonical format. Fields must be
	 * written in the same order in arrays and objects, and name comparison is case sensitive. If
	 * these assumptions do not hold, this may return false for equivalent inputs. Validation can
	 * be performed with ValidateCompactBinary, except for field order and field name case.
	 */
	CORE_API bool Equals(const FCbField& Other) const;

	/** Copy the field into a buffer of exactly GetSize() bytes, including the type and name. */
	CORE_API void CopyTo(FMutableMemoryView Buffer) const;

	/** Copy the field into an archive, including its type and name. */
	CORE_API void CopyTo(FArchive& Ar) const;

	/** Invoke the visitor for every attachment in the field. */
	CORE_API void IterateAttachments(FCbFieldVisitor Visitor) const;

	/** Returns a view of the field, including the type and name when present. */
	CORE_API FMemoryView GetView() const;

	/**
	 * Try to get a view of the field as it would be serialized, such as by CopyTo.
	 *
	 * A serialized view is not available if the field has an externally-provided type.
	 * Access the serialized form of such fields using CopyTo or FCbFieldRef::Clone.
	 */
	inline bool TryGetSerializedView(FMemoryView& OutView) const
	{
		if (FCbFieldType::HasFieldType(Type))
		{
			OutView = GetView();
			return true;
		}
		return false;
	}

protected:
	/** Returns a view of the name and value payload, which excludes the type. */
	CORE_API FMemoryView GetViewNoType() const;

	/** Returns a view of the value payload, which excludes the type and name. */
	inline FMemoryView GetPayloadView() const { return MakeMemoryView(Payload, GetPayloadSize()); }

	/** Returns the type of the field including flags. */
	constexpr inline ECbFieldType GetType() const { return Type; }

	/** Returns the start of the value payload. */
	constexpr inline const void* GetPayload() const { return Payload; }

	/** Returns the end of the value payload. */
	inline const void* GetPayloadEnd() const { return static_cast<const uint8*>(Payload) + GetPayloadSize(); }

	/** Returns the size of the value payload in bytes, which is the field excluding the type and name. */
	CORE_API uint64 GetPayloadSize() const;

	/** Assign a field from a pointer to its data and an optional externally-provided type. */
	inline void Assign(const void* InData, const ECbFieldType InType)
	{
		static_assert(TIsTriviallyDestructible<FCbField>::Value,
			"This optimization requires FCbField to be trivially destructible!");
		new(this) FCbField(InData, InType);
	}

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

/**
 * Iterator for FCbField.
 *
 * @see TCbFieldIterator
 */
class FCbFieldIterator : public TCbFieldIterator<FCbField>
{
public:
	/** Construct a field range that contains exactly one field. */
	static inline FCbFieldIterator MakeSingle(const FCbField& Field)
	{
		return FCbFieldIterator(Field);
	}

	/**
	 * Construct a field range from a buffer containing zero or more valid fields.
	 *
	 * @param View A buffer containing zero or more valid fields.
	 * @param Type HasFieldType means that View contains the type. Otherwise, use the given type.
	 */
	static inline FCbFieldIterator MakeRange(FMemoryView View, ECbFieldType Type = ECbFieldType::HasFieldType)
	{
		return !View.IsEmpty() ? FCbFieldIterator(FCbField(View.GetData(), Type), View.GetDataEnd()) : FCbFieldIterator();
	}

	/** Construct an empty field range. */
	constexpr FCbFieldIterator() = default;

	/** Construct an iterator from another iterator. */
	template <typename OtherFieldType>
	inline FCbFieldIterator(const TCbFieldIterator<OtherFieldType>& It)
		: TCbFieldIterator(ImplicitConv<FCbField>(It), GetFieldsEnd(It))
	{
	}

private:
	using TCbFieldIterator::TCbFieldIterator;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Array of FCbField that have no names.
 *
 * Accessing a field of the array requires iteration. Access by index is not provided because the
 * cost of accessing an item by index scales linearly with the index.
 *
 * This type only provides a view into memory and does not perform any memory management itself.
 * Use FCbArrayRef to hold a reference to the underlying memory when necessary.
 */
class FCbArray : protected FCbField
{
public:
	/** @see FCbField::FCbField */
	using FCbField::FCbField;

	/** Construct an array with no fields. */
	CORE_API FCbArray();

	/** Returns the number of items in the array. */
	CORE_API uint64 Num() const;

	/** Create an iterator for the fields of this array. */
	CORE_API FCbFieldIterator CreateIterator() const;

	/** Access the array as an array field. */
	inline FCbField AsField() const { return static_cast<const FCbField&>(*this); }

	/** Construct an array from an array field. No type check is performed! */
	static inline FCbArray FromField(const FCbField& Field) { return FCbArray(Field); }

	/** Returns the size of the array in bytes if serialized by itself with no name. */
	CORE_API uint64 GetSize() const;

	/** Calculate the hash of the array if serialized by itself with no name. */
	CORE_API FIoHash GetHash() const;
	/** Calculate the hash of the array if serialized by itself with no name. */
	CORE_API void GetHash(FBlake3& Hash) const;

	/**
	 * Whether this array is identical to the other array.
	 *
	 * Performs a deep comparison of any contained arrays or objects and their fields. Comparison
	 * assumes that both fields are valid and are written in the canonical format. Fields must be
	 * written in the same order in arrays and objects, and name comparison is case sensitive. If
	 * these assumptions do not hold, this may return false for equivalent inputs. Validation can
	 * be done with the All mode to check these assumptions about the format of the inputs.
	 */
	CORE_API bool Equals(const FCbArray& Other) const;

	/** Copy the array into a buffer of exactly GetSize() bytes, with no name. */
	CORE_API void CopyTo(FMutableMemoryView Buffer) const;

	/** Copy the array into an archive. This will write GetSize() bytes, with no name. */
	CORE_API void CopyTo(FArchive& Ar) const;

	/** Invoke the visitor for every attachment in the array. */
	inline void IterateAttachments(FCbFieldVisitor Visitor) const { CreateIterator().IterateRangeAttachments(Visitor); }

	/** Returns a view of the array, including the type and name when present. */
	using FCbField::GetView;

	/**
	 * Try to get a view of the array as it would be serialized, such as by CopyTo.
	 *
	 * A serialized view is not available if the underlying field has an externally-provided type or
	 * a name. Access the serialized form of such arrays using CopyTo or FCbArrayRef::Clone.
	 */
	inline bool TryGetSerializedView(FMemoryView& OutView) const
	{
		return !FCbField::HasName() && FCbField::TryGetSerializedView(OutView);
	}

private:
	friend inline FCbFieldIterator begin(const FCbArray& Array) { return Array.CreateIterator(); }
	friend inline FCbFieldIterator end(const FCbArray&) { return FCbFieldIterator(); }

	/** Construct an array from an array field. No type check is performed! Use via FromField. */
	inline explicit FCbArray(const FCbField& Field) : FCbField(Field) {}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Array of FCbField that have unique names.
 *
 * Accessing the fields of an object is always a safe operation, even if the requested field does
 * not exist. Fields may be accessed by name or through iteration. When a field is requested that
 * is not found in the object, the field that it returns has no value (evaluates to false) though
 * attempting to access the empty field is also safe, as described by FCbField.
 *
 * This type only provides a view into memory and does not perform any memory management itself.
 * Use FCbObjectRef to hold a reference to the underlying memory when necessary.
 */
class FCbObject : protected FCbField
{
public:
	/** @see FCbField::FCbField */
	using FCbField::FCbField;

	/** Construct an object with no fields. */
	CORE_API FCbObject();

	/** Create an iterator for the fields of this object. */
	CORE_API FCbFieldIterator CreateIterator() const;

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

	/** Find a field by case-insensitive name comparison. */
	CORE_API FCbField FindIgnoreCase(FAnsiStringView Name) const;

	/** Find a field by case-sensitive name comparison. */
	inline FCbField operator[](FAnsiStringView Name) const { return Find(Name); }

	/** Access the object as an object field. */
	inline FCbField AsField() const { return static_cast<const FCbField&>(*this); }

	/** Construct an object from an object field. No type check is performed! */
	static inline FCbObject FromField(const FCbField& Field) { return FCbObject(Field); }

	/** Returns the size of the object in bytes if serialized by itself with no name. */
	CORE_API uint64 GetSize() const;

	/** Calculate the hash of the object if serialized by itself with no name. */
	CORE_API FIoHash GetHash() const;
	/** Calculate the hash of the object if serialized by itself with no name. */
	CORE_API void GetHash(FBlake3& Hash) const;

	/**
	 * Whether this object is identical to the other object.
	 *
	 * Performs a deep comparison of any contained arrays or objects and their fields. Comparison
	 * assumes that both fields are valid and are written in the canonical format. Fields must be
	 * written in the same order in arrays and objects, and name comparison is case sensitive. If
	 * these assumptions do not hold, this may return false for equivalent inputs. Validation can
	 * be done with the All mode to check these assumptions about the format of the inputs.
	 */
	CORE_API bool Equals(const FCbObject& Other) const;

	/** Copy the object into a buffer of exactly GetSize() bytes, with no name. */
	CORE_API void CopyTo(FMutableMemoryView Buffer) const;

	/** Copy the object into an archive. This will write GetSize() bytes, with no name. */
	CORE_API void CopyTo(FArchive& Ar) const;

	/** Invoke the visitor for every attachment in the object. */
	inline void IterateAttachments(FCbFieldVisitor Visitor) const { CreateIterator().IterateRangeAttachments(Visitor); }

	/** Returns a view of the object, including the type and name when present. */
	using FCbField::GetView;

	/**
	 * Try to get a view of the object as it would be serialized, such as by CopyTo.
	 *
	 * A serialized view is not available if the underlying field has an externally-provided type or
	 * a name. Access the serialized form of such objects using CopyTo or FCbObjectRef::Clone.
	 */
	inline bool TryGetSerializedView(FMemoryView& OutView) const
	{
		return !FCbField::HasName() && FCbField::TryGetSerializedView(OutView);
	}

private:
	friend inline FCbFieldIterator begin(const FCbObject& Object) { return Object.CreateIterator(); }
	friend inline FCbFieldIterator end(const FCbObject&) { return FCbFieldIterator(); }

	/** Construct an object from an object field. No type check is performed! Use via FromField. */
	inline explicit FCbObject(const FCbField& Field) : FCbField(Field) {}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A reference to a function that is used to allocate buffers for compact binary data. */
using FCbBufferAllocator = TFunctionRef<FUniqueBuffer (uint64 Size)>;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A wrapper that holds a reference to the buffer that contains its compact binary value. */
template <typename BaseType>
class TCbBufferRef : public BaseType
{
public:
	/** Construct a default value. */
	TCbBufferRef() = default;

	/**
	 * Construct a value from a pointer to its data and an optional externally-provided type.
	 *
	 * @param ValueBuffer A buffer that exactly contains the value.
	 * @param Type HasFieldType means that ValueBuffer contains the type. Otherwise, use the given type.
	 */
	inline explicit TCbBufferRef(FSharedBuffer ValueBuffer, ECbFieldType Type = ECbFieldType::HasFieldType)
	{
		if (ValueBuffer)
		{
			BaseType::operator=(BaseType(ValueBuffer.GetData(), Type));
			check(ValueBuffer.GetView().Contains(BaseType::GetView()));
			Buffer = MoveTemp(ValueBuffer);
		}
	}

	/** Construct a value that holds a reference to the buffer that contains it. */
	inline TCbBufferRef(const BaseType& Value, FSharedBuffer OuterBuffer)
		: BaseType(Value)
	{
		if (OuterBuffer)
		{
			check(OuterBuffer.GetView().Contains(BaseType::GetView()));
			Buffer = MoveTemp(OuterBuffer);
		}
	}

	/** Construct a value that holds a reference to the buffer of the outer that contains it. */
	template <typename OtherBaseType>
	inline TCbBufferRef(const BaseType& Value, TCbBufferRef<OtherBaseType> OuterRef)
		: TCbBufferRef(Value, MoveTemp(OuterRef.Buffer))
	{
	}

	/** Reset this to a default value and null buffer. */
	inline void Reset() { *this = TCbBufferRef(); }

	/** Whether this reference has ownership of the memory in its buffer. */
	inline bool IsOwned() const { return Buffer && Buffer.IsOwned(); }

	/** Clone the value, if necessary, to a buffer that this reference has ownership of. */
	inline void MakeOwned()
	{
		if (!IsOwned())
		{
			FUniqueBuffer MutableBuffer = FUniqueBuffer::Alloc(BaseType::GetSize());
			BaseType::CopyTo(MutableBuffer);
			BaseType::operator=(BaseType(MutableBuffer.GetData()));
			Buffer = MoveTemp(MutableBuffer);
		}
	}

	/** Returns a buffer that exactly contains this value. */
	inline FSharedBuffer GetBuffer() const
	{
		const FMemoryView View = BaseType::GetView();
		const FSharedBuffer& OuterBuffer = GetOuterBuffer();
		return View == OuterBuffer.GetView() ? OuterBuffer : FSharedBuffer::MakeView(View, OuterBuffer);
	}

	/** Returns the outer buffer (if any) that contains this value. */
	inline const FSharedBuffer& GetOuterBuffer() const & { return Buffer; }
	inline FSharedBuffer GetOuterBuffer() && { return MoveTemp(Buffer); }

private:
	template <typename OtherType>
	friend class TCbBufferRef;

	FSharedBuffer Buffer;
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

	/** Construct a value from a read-only view of its memory and its optional outer buffer. */
	static inline RefType MakeView(const void* const Data, FSharedBuffer OuterBuffer = FSharedBuffer())
	{
		return MakeView(BaseType(Data), MoveTemp(OuterBuffer));
	}

	/** Construct a value from a read-only view of its memory and its optional outer buffer. */
	static inline RefType MakeView(const BaseType& Value, FSharedBuffer OuterBuffer = FSharedBuffer())
	{
		return RefType(Value, MoveTemp(OuterBuffer));
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCbArrayRef;
class FCbObjectRef;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A field that can hold a reference to the memory that contains it.
 *
 * @see TCbBufferRef
 */
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

/**
 * Iterator for FCbFieldRef.
 *
 * @see TCbFieldIterator
 */
class FCbFieldRefIterator : public TCbFieldIterator<FCbFieldRef>
{
public:
	/** Construct a field range from an owned clone of a range. */
	CORE_API static FCbFieldRefIterator CloneRange(const FCbFieldIterator& It);

	/** Construct a field range from an owned clone of a range. */
	static inline FCbFieldRefIterator CloneRange(const FCbFieldRefIterator& It)
	{
		return CloneRange(FCbFieldIterator(It));
	}

	/** Construct a field range that contains exactly one field. */
	static inline FCbFieldRefIterator MakeSingle(FCbFieldRef Field)
	{
		return FCbFieldRefIterator(MoveTemp(Field));
	}

	/**
	 * Construct a field range from a buffer containing zero or more valid fields.
	 *
	 * @param Buffer A buffer containing zero or more valid fields.
	 * @param Type HasFieldType means that Buffer contains the type. Otherwise, use the given type.
	 */
	static inline FCbFieldRefIterator MakeRange(FSharedBuffer Buffer, ECbFieldType Type = ECbFieldType::HasFieldType)
	{
		if (Buffer.GetSize())
		{
			const void* const DataEnd = Buffer.GetView().GetDataEnd();
			return FCbFieldRefIterator(FCbFieldRef(MoveTemp(Buffer), Type), DataEnd);
		}
		return FCbFieldRefIterator();
	}

	/** Construct a field range from an iterator and its optional outer buffer. */
	static inline FCbFieldRefIterator MakeRangeView(const FCbFieldIterator& It, FSharedBuffer OuterBuffer = FSharedBuffer())
	{
		return FCbFieldRefIterator(FCbFieldRef(It, MoveTemp(OuterBuffer)), GetFieldsEnd(It));
	}

	/** Construct an empty field range. */
	constexpr FCbFieldRefIterator() = default;

	/** Clone the range, if necessary, to a buffer that this reference has ownership of. */
	inline void MakeRangeOwned()
	{
		if (!IsOwned())
		{
			*this = CloneRange(*this);
		}
	}

	/** Returns a buffer that exactly contains the field range. */
	CORE_API FSharedBuffer GetRangeBuffer() const;

private:
	using TCbFieldIterator::TCbFieldIterator;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * An array that can hold a reference to the memory that contains it.
 *
 * @see TCbBufferRef
 */
class FCbArrayRef : public TCbBufferRef<FCbArray>, public TCbBufferRefFactory<FCbArrayRef, FCbArray>
{
public:
	using TCbBufferRef::TCbBufferRef;

	/** Create an iterator for the fields of this array. */
	inline FCbFieldRefIterator CreateRefIterator() const
	{
		return FCbFieldRefIterator::MakeRangeView(CreateIterator(), GetOuterBuffer());
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

/**
 * An object that can hold a reference to the memory that contains it.
 *
 * @see TCbBufferRef
 */
class FCbObjectRef : public TCbBufferRef<FCbObject>, public TCbBufferRefFactory<FCbObjectRef, FCbObject>
{
public:
	using TCbBufferRef::TCbBufferRef;

	/** Create an iterator for the fields of this object. */
	inline FCbFieldRefIterator CreateRefIterator() const
	{
		return FCbFieldRefIterator::MakeRangeView(CreateIterator(), GetOuterBuffer());
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
