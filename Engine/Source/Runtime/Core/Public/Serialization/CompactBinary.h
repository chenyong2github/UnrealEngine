// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
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
 * A field can be constructed as a view of the underlying memory with FCbFieldView, or a FCbField
 * can used when ownership of the underlying memory is required. An object provides this behavior
 * with FCbObjectView or FCbObject, and an array uses FCbArrayView or FCbArray.
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
 * void BeginBuild(FCbObject Params)
 * {
 *     if (FSharedBuffer Data = Storage().Load(Params["Data"].AsBinaryAttachment()))
 *     {
 *         SetData(Data);
 *     }
 *
 *     if (Params["Resize"].AsBool())
 *     {
 *         FCbFieldView MaxWidthField = Params["MaxWidth"]
 *         FCbFieldView MaxHeightField = Params["MaxHeight"];
 *         if (MaxWidthField && MaxHeightField)
 *         {
 *             Resize(MaxWidthField.AsInt32(), MaxHeightField.AsInt32());
 *         }
 *     }
 *
 *     for (FCbFieldView Format : Params["Formats"].AsArrayView())
 *     {
 *         BeginCompress(FName(Format.AsString()));
 *     }
 * }
 */

class FArchive;
class FBlake3;
class FCbArrayView;
class FCbFieldView;
class FCbObjectId;
class FCbObjectView;
struct FDateTime;
struct FGuid;
struct FTimespan;
template <typename FuncType> class TFunctionRef;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Field types and flags for FCbField[View].
 *
 * DO NOT CHANGE THE VALUE OF ANY MEMBERS OF THIS ENUM!
 * BACKWARD COMPATIBILITY REQUIRES THAT THESE VALUES BE FIXED!
 * SERIALIZATION USES HARD-CODED CONSTANTS BASED ON THESE VALUES!
 */
enum class ECbFieldType : uint8
{
	/** A field type that does not occur in a valid object. */
	None                             = 0x00,

	/** Null. Payload is empty. */
	Null                             = 0x01,

	/**
	 * Object is an array of fields with unique non-empty names.
	 *
	 * Payload is a VarUInt byte count for the encoded fields followed by the fields.
	 */
	Object                           = 0x02,
	/**
	 * UniformObject is an array of fields with the same field types and unique non-empty names.
	 *
	 * Payload is a VarUInt byte count for the encoded fields followed by the fields.
	 */
	UniformObject                    = 0x03,

	/**
	 * Array is an array of fields with no name that may be of different types.
	 *
	 * Payload is a VarUInt byte count, followed by a VarUInt item count, followed by the fields.
	 */
	Array                            = 0x04,
	/**
	 * UniformArray is an array of fields with no name and with the same field type.
	 *
	 * Payload is a VarUInt byte count, followed by a VarUInt item count, followed by field type,
	 * followed by the fields without their field type.
	 */
	UniformArray                     = 0x05,

	/** Binary. Payload is a VarUInt byte count followed by the data. */
	Binary                           = 0x06,

	/** String in UTF-8. Payload is a VarUInt byte count then an unterminated UTF-8 string. */
	String                           = 0x07,

	/**
	 * Non-negative integer with the range of a 64-bit unsigned integer.
	 *
	 * Payload is the value encoded as a VarUInt.
	 */
	IntegerPositive                  = 0x08,
	/**
	 * Negative integer with the range of a 64-bit signed integer.
	 *
	 * Payload is the ones' complement of the value encoded as a VarUInt.
	 */
	IntegerNegative                  = 0x09,

	/** Single precision float. Payload is one big endian IEEE 754 binary32 float. */
	Float32                          = 0x0a,
	/** Double precision float. Payload is one big endian IEEE 754 binary64 float. */
	Float64                          = 0x0b,

	/** Boolean false value. Payload is empty. */
	BoolFalse                        = 0x0c,
	/** Boolean true value. Payload is empty. */
	BoolTrue                         = 0x0d,

	/**
	 * CompactBinaryAttachment is a reference to a compact binary attachment stored externally.
	 *
	 * Payload is a 160-bit hash digest of the referenced compact binary data.
	 */
	CompactBinaryAttachment          = 0x0e,
	/**
	 * BinaryAttachment is a reference to a binary attachment stored externally.
	 *
	 * Payload is a 160-bit hash digest of the referenced binary data.
	 */
	BinaryAttachment                 = 0x0f,

	/** Hash. Payload is a 160-bit hash digest. */
	Hash                             = 0x10,
	/** UUID/GUID. Payload is a 128-bit UUID as defined by RFC 4122. */
	Uuid                             = 0x11,

	/**
	 * Date and time between 0001-01-01 00:00:00.0000000 and 9999-12-31 23:59:59.9999999.
	 *
	 * Payload is a big endian int64 count of 100ns ticks since 0001-01-01 00:00:00.0000000.
	 */
	DateTime                         = 0x12,
	/**
	 * Difference between two date/time values.
	 *
	 * Payload is a big endian int64 count of 100ns ticks in the span, and may be negative.
	 */
	TimeSpan                         = 0x13,

	/**
	 * ObjectId is an opaque object identifier. See FCbObjectId.
	 *
	 * Payload is a 12-byte object identifier.
	 */
	ObjectId                         = 0x14,

	/**
	 * CustomById identifies the sub-type of its payload by an integer identifier.
	 *
	 * Payload is a VarUInt byte count of the sub-type identifier and the sub-type payload, followed
	 * by a VarUInt of the sub-type identifier then the payload of the sub-type.
	 */
	CustomById                       = 0x1e,
	/**
	 * CustomByType identifies the sub-type of its payload by a string identifier.
	 *
	 * Payload is a VarUInt byte count of the sub-type identifier and the sub-type payload, followed
	 * by a VarUInt byte count of the unterminated sub-type identifier, then the sub-type identifier
	 * without termination, then the payload of the sub-type.
	 */
	CustomByName                     = 0x1f,

	/** Reserved for future use as a flag. Do not add types in this range. */
	Reserved                         = 0x20,

	/**
	 * A transient flag which indicates that the object or array containing this field has stored
	 * the field type before the payload and name. Non-uniform objects and fields will set this.
	 *
	 * Note: Since the flag must never be serialized, this bit may be repurposed in the future.
	 */
	HasFieldType                     = 0x40,

	/** A persisted flag which indicates that the field has a name stored before the payload. */
	HasFieldName                     = 0x80,
};

ENUM_CLASS_FLAGS(ECbFieldType);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Functions that operate on ECbFieldType. */
class FCbFieldType
{
	static constexpr ECbFieldType SerializedTypeMask    = ECbFieldType(0b1001'1111);
	static constexpr ECbFieldType TypeMask              = ECbFieldType(0b0001'1111);

	static constexpr ECbFieldType ObjectMask            = ECbFieldType(0b0001'1110);
	static constexpr ECbFieldType ObjectBase            = ECbFieldType(0b0000'0010);

	static constexpr ECbFieldType ArrayMask             = ECbFieldType(0b0001'1110);
	static constexpr ECbFieldType ArrayBase             = ECbFieldType(0b0000'0100);

	static constexpr ECbFieldType IntegerMask           = ECbFieldType(0b0001'1110);
	static constexpr ECbFieldType IntegerBase           = ECbFieldType(0b0000'1000);

	static constexpr ECbFieldType FloatMask             = ECbFieldType(0b0001'1100);
	static constexpr ECbFieldType FloatBase             = ECbFieldType(0b0000'1000);

	static constexpr ECbFieldType BoolMask              = ECbFieldType(0b0001'1110);
	static constexpr ECbFieldType BoolBase              = ECbFieldType(0b0000'1100);

	static constexpr ECbFieldType AttachmentMask        = ECbFieldType(0b0001'1110);
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
	static constexpr inline bool IsAttachment(ECbFieldType Type)              { return (Type & AttachmentMask) == AttachmentBase; }

	static constexpr inline bool IsHash(ECbFieldType Type)       { return GetType(Type) == ECbFieldType::Hash; }
	static constexpr inline bool IsUuid(ECbFieldType Type)       { return GetType(Type) == ECbFieldType::Uuid; }

	static constexpr inline bool IsDateTime(ECbFieldType Type)   { return GetType(Type) == ECbFieldType::DateTime; }
	static constexpr inline bool IsTimeSpan(ECbFieldType Type)   { return GetType(Type) == ECbFieldType::TimeSpan; }

	static constexpr inline bool IsObjectId(ECbFieldType Type)   { return GetType(Type) == ECbFieldType::ObjectId; }

	static constexpr inline bool IsCustomById(ECbFieldType Type)   { return GetType(Type) == ECbFieldType::CustomById; }
	static constexpr inline bool IsCustomByName(ECbFieldType Type) { return GetType(Type) == ECbFieldType::CustomByName; }

	/** Whether the type is or may contain fields of any attachment type. */
	static constexpr inline bool MayContainAttachments(ECbFieldType Type)
	{
		// The use of !! will suppress V792 from static analysis. Using //-V792 did not work.
		return !!IsObject(Type) | !!IsArray(Type) | !!IsAttachment(Type);
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A reference to a function that is used to visit fields. */
using FCbFieldVisitor = TFunctionRef<void (FCbFieldView)>;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Iterator for FCbField[View] that can operate on any contiguous range of fields.
 *
 * The iterator *is* the current field that the iterator points to and exposes the full interface
 * of FCbField[View]. An iterator that is at the end is equivalent to a field with no value.
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
	 * Access the serialized form of such ranges using FCbFieldIterator::CloneRange.
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
 * An opaque 12-byte object identifier.
 *
 * It has no intrinsic meaning, and can only be properly interpreted in the context of its usage.
 */
class FCbObjectId
{
public:
	/** Construct an ObjectId with every byte initialized to zero. */
	FCbObjectId() = default;

	/** Construct an ObjectId from a view of 12 bytes. */
	CORE_API explicit FCbObjectId(FMemoryView ObjectId);

	/** Convert the ObjectId to a 24-character hex string. */
	CORE_API void ToString(FAnsiStringBuilderBase& Builder) const;
	CORE_API void ToString(FWideStringBuilderBase& Builder) const;

	/** Returns a view of the raw byte array for the ObjectId. */
	constexpr inline FMemoryView GetView() const { return MakeMemoryView(Bytes); }

private:
	alignas(uint32) uint8 Bytes[12]{};
};

inline bool operator==(const FCbObjectId& A, const FCbObjectId& B)
{
	return FMemory::Memcmp(&A, &B, sizeof(FCbObjectId)) == 0;
}

inline bool operator!=(const FCbObjectId& A, const FCbObjectId& B)
{
	return FMemory::Memcmp(&A, &B, sizeof(FCbObjectId)) != 0;
}

inline bool operator<(const FCbObjectId& A, const FCbObjectId& B)
{
	return FMemory::Memcmp(&A, &B, sizeof(FCbObjectId)) <= 0;
}

inline uint32 GetTypeHash(const FCbObjectId& Id)
{
	return *reinterpret_cast<const uint32*>(&Id);
}

template <typename CharType>
inline TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FCbObjectId& Id)
{
	Id.ToString(Builder);
	return Builder;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A custom compact binary field type with an integer identifier. */
struct FCbCustomById
{
	/** An identifier for the sub-type of the field. */
	uint64 Id = 0;
	/** A view of the value. Lifetime is tied to the field that the value is associated with. */
	FMemoryView Data;
};

/** A custom compact binary field type with a string identifier. */
struct FCbCustomByName
{
	/** An identifier for the sub-type of the field. Lifetime is tied to the field that the name is associated with. */
	FAnsiStringView Name;
	/** A view of the value. Lifetime is tied to the field that the value is associated with. */
	FMemoryView Data;
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
 * Use FCbField to hold a reference to the underlying memory when necessary.
 */
class FCbFieldView
{
public:
	/** Construct a field with no name and no value. */
	constexpr FCbFieldView() = default;

	/**
	 * Construct a field from a pointer to its data and an optional externally-provided type.
	 *
	 * @param Data Pointer to the start of the field data.
	 * @param Type HasFieldType means that Data contains the type. Otherwise, use the given type.
	 */
	CORE_API explicit FCbFieldView(const void* Data, ECbFieldType Type = ECbFieldType::HasFieldType);

	/** Returns the name of the field if it has a name, otherwise an empty view. */
	constexpr inline FAnsiStringView GetName() const
	{
		return FAnsiStringView(static_cast<const ANSICHAR*>(Payload) - NameLen, NameLen);
	}

	/** Access the field as an object. Defaults to an empty object on error. */
	CORE_API FCbObjectView AsObjectView();

	/** Access the field as an array. Defaults to an empty array on error. */
	CORE_API FCbArrayView AsArrayView();

	/** Access the field as binary. Returns the provided default on error. */
	CORE_API FMemoryView AsBinaryView(FMemoryView Default = FMemoryView());

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

	/** Access the field as an object identifier. Returns the provided default on error. */
	CORE_API FCbObjectId AsObjectId(const FCbObjectId& Default = FCbObjectId());

	/** Access the field as a custom sub-type with an integer identifier. Returns the provided default on error. */
	CORE_API FCbCustomById AsCustomById(FCbCustomById Default = FCbCustomById());
	/** Access the field as a custom sub-type with a string identifier. Returns the provided default on error. */
	CORE_API FCbCustomByName AsCustomByName(FCbCustomByName Default = FCbCustomByName());

	/** Access the field as a custom sub-type with an integer identifier. Returns the provided default on error. */
	CORE_API FMemoryView AsCustom(uint64 Id, FMemoryView Default = FMemoryView());
	/** Access the field as a custom sub-type with a string identifier. Returns the provided default on error. */
	CORE_API FMemoryView AsCustom(FAnsiStringView Name, FMemoryView Default = FMemoryView());

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

	constexpr inline bool IsObjectId() const        { return FCbFieldType::IsObjectId(Type); }

	constexpr inline bool IsCustomById() const      { return FCbFieldType::IsCustomById(Type); }
	constexpr inline bool IsCustomByName() const    { return FCbFieldType::IsCustomByName(Type); }

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
	CORE_API bool Equals(const FCbFieldView& Other) const;

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
	 * Access the serialized form of such fields using CopyTo or FCbField::Clone.
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
		static_assert(TIsTriviallyDestructible<FCbFieldView>::Value,
			"This optimization requires FCbFieldView to be trivially destructible!");
		new(this) FCbFieldView(InData, InType);
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
 * Iterator for FCbFieldView.
 *
 * @see TCbFieldIterator
 */
class FCbFieldViewIterator : public TCbFieldIterator<FCbFieldView>
{
public:
	/** Construct a field range that contains exactly one field. */
	static inline FCbFieldViewIterator MakeSingle(const FCbFieldView& Field)
	{
		return FCbFieldViewIterator(Field);
	}

	/**
	 * Construct a field range from a buffer containing zero or more valid fields.
	 *
	 * @param View A buffer containing zero or more valid fields.
	 * @param Type HasFieldType means that View contains the type. Otherwise, use the given type.
	 */
	static inline FCbFieldViewIterator MakeRange(FMemoryView View, ECbFieldType Type = ECbFieldType::HasFieldType)
	{
		return !View.IsEmpty() ? FCbFieldViewIterator(FCbFieldView(View.GetData(), Type), View.GetDataEnd()) : FCbFieldViewIterator();
	}

	/** Construct an empty field range. */
	constexpr FCbFieldViewIterator() = default;

	/** Construct an iterator from another iterator. */
	template <typename OtherFieldType>
	inline FCbFieldViewIterator(const TCbFieldIterator<OtherFieldType>& It)
		: TCbFieldIterator(ImplicitConv<FCbFieldView>(It), GetFieldsEnd(It))
	{
	}

private:
	using TCbFieldIterator::TCbFieldIterator;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Array of FCbField[View] that have no names.
 *
 * Accessing a field of the array requires iteration. Access by index is not provided because the
 * cost of accessing an item by index scales linearly with the index.
 *
 * This type only provides a view into memory and does not perform any memory management itself.
 * Use FCbArray to hold a reference to the underlying memory when necessary.
 */
class FCbArrayView : protected FCbFieldView
{
public:
	/** @see FCbFieldView::FCbFieldView */
	using FCbFieldView::FCbFieldView;

	/** Construct an array with no fields. */
	CORE_API FCbArrayView();

	/** Returns the number of items in the array. */
	CORE_API uint64 Num() const;

	/** Create an iterator for the fields of this array. */
	CORE_API FCbFieldViewIterator CreateViewIterator() const;

	/** Access the array as an array field. */
	inline FCbFieldView AsFieldView() const { return static_cast<const FCbFieldView&>(*this); }

	/** Construct an array from an array field. No type check is performed! */
	static inline FCbArrayView FromFieldNoCheck(const FCbFieldView& Field) { return FCbArrayView(Field); }

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
	CORE_API bool Equals(const FCbArrayView& Other) const;

	/** Copy the array into a buffer of exactly GetSize() bytes, with no name. */
	CORE_API void CopyTo(FMutableMemoryView Buffer) const;

	/** Copy the array into an archive. This will write GetSize() bytes, with no name. */
	CORE_API void CopyTo(FArchive& Ar) const;

	/** Invoke the visitor for every attachment in the array. */
	inline void IterateAttachments(FCbFieldVisitor Visitor) const { CreateViewIterator().IterateRangeAttachments(Visitor); }

	/** Returns a view of the array, including the type and name when present. */
	using FCbFieldView::GetView;

	/**
	 * Try to get a view of the array as it would be serialized, such as by CopyTo.
	 *
	 * A serialized view is not available if the underlying field has an externally-provided type or
	 * a name. Access the serialized form of such arrays using CopyTo or FCbArray::Clone.
	 */
	inline bool TryGetSerializedView(FMemoryView& OutView) const
	{
		return !FCbFieldView::HasName() && FCbFieldView::TryGetSerializedView(OutView);
	}

private:
	friend inline FCbFieldViewIterator begin(const FCbArrayView& Array) { return Array.CreateViewIterator(); }
	friend inline FCbFieldViewIterator end(const FCbArrayView&) { return FCbFieldViewIterator(); }

	/** Construct an array from an array field. No type check is performed! Use via FromFieldNoCheck. */
	inline explicit FCbArrayView(const FCbFieldView& Field) : FCbFieldView(Field) {}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Array of FCbField[View] that have unique names.
 *
 * Accessing the fields of an object is always a safe operation, even if the requested field does
 * not exist. Fields may be accessed by name or through iteration. When a field is requested that
 * is not found in the object, the field that it returns has no value (evaluates to false) though
 * attempting to access the empty field is also safe, as described by FCbFieldView.
 *
 * This type only provides a view into memory and does not perform any memory management itself.
 * Use FCbObject to hold a reference to the underlying memory when necessary.
 */
class FCbObjectView : protected FCbFieldView
{
public:
	/** @see FCbFieldView::FCbFieldView */
	using FCbFieldView::FCbFieldView;

	/** Construct an object with no fields. */
	CORE_API FCbObjectView();

	/** Create an iterator for the fields of this object. */
	CORE_API FCbFieldViewIterator CreateViewIterator() const;

	/**
	 * Find a field by case-sensitive name comparison.
	 *
	 * The cost of this operation scales linearly with the number of fields in the object. Prefer to
	 * iterate over the fields only once when consuming an object.
	 *
	 * @param Name The name of the field.
	 * @return The matching field if found, otherwise a field with no value.
	 */
	CORE_API FCbFieldView FindView(FAnsiStringView Name) const;

	/** Find a field by case-insensitive name comparison. */
	CORE_API FCbFieldView FindViewIgnoreCase(FAnsiStringView Name) const;

	/** Find a field by case-sensitive name comparison. */
	inline FCbFieldView operator[](FAnsiStringView Name) const { return FindView(Name); }

	/** Access the object as an object field. */
	inline FCbFieldView AsFieldView() const { return static_cast<const FCbFieldView&>(*this); }

	/** Construct an object from an object field. No type check is performed! */
	static inline FCbObjectView FromFieldNoCheck(const FCbFieldView& Field) { return FCbObjectView(Field); }

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
	CORE_API bool Equals(const FCbObjectView& Other) const;

	/** Copy the object into a buffer of exactly GetSize() bytes, with no name. */
	CORE_API void CopyTo(FMutableMemoryView Buffer) const;

	/** Copy the object into an archive. This will write GetSize() bytes, with no name. */
	CORE_API void CopyTo(FArchive& Ar) const;

	/** Invoke the visitor for every attachment in the object. */
	inline void IterateAttachments(FCbFieldVisitor Visitor) const { CreateViewIterator().IterateRangeAttachments(Visitor); }

	/** Returns a view of the object, including the type and name when present. */
	using FCbFieldView::GetView;

	/**
	 * Try to get a view of the object as it would be serialized, such as by CopyTo.
	 *
	 * A serialized view is not available if the underlying field has an externally-provided type or
	 * a name. Access the serialized form of such objects using CopyTo or FCbObject::Clone.
	 */
	inline bool TryGetSerializedView(FMemoryView& OutView) const
	{
		return !FCbFieldView::HasName() && FCbFieldView::TryGetSerializedView(OutView);
	}

private:
	friend inline FCbFieldViewIterator begin(const FCbObjectView& Object) { return Object.CreateViewIterator(); }
	friend inline FCbFieldViewIterator end(const FCbObjectView&) { return FCbFieldViewIterator(); }

	/** Construct an object from an object field. No type check is performed! Use via FromFieldNoCheck. */
	inline explicit FCbObjectView(const FCbFieldView& Field) : FCbFieldView(Field) {}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A reference to a function that is used to allocate buffers for compact binary data. */
using FCbBufferAllocator = TFunctionRef<FUniqueBuffer (uint64 Size)>;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A wrapper that holds a reference to the buffer that contains its compact binary value. */
template <typename ViewType>
class TCbBuffer : public ViewType
{
public:
	/** Construct a default value. */
	TCbBuffer() = default;

	/**
	 * Construct a value from a pointer to its data and an optional externally-provided type.
	 *
	 * @param ValueBuffer A buffer that exactly contains the value.
	 * @param Type HasFieldType means that ValueBuffer contains the type. Otherwise, use the given type.
	 */
	inline explicit TCbBuffer(FSharedBuffer ValueBuffer, ECbFieldType Type = ECbFieldType::HasFieldType)
	{
		if (ValueBuffer)
		{
			ViewType::operator=(ViewType(ValueBuffer.GetData(), Type));
			check(ValueBuffer.GetView().Contains(ViewType::GetView()));
			Buffer = MoveTemp(ValueBuffer);
		}
	}

	/** Construct a value that holds a reference to the buffer that contains it. */
	inline TCbBuffer(const ViewType& Value, FSharedBuffer OuterBuffer)
		: ViewType(Value)
	{
		if (OuterBuffer)
		{
			check(OuterBuffer.GetView().Contains(ViewType::GetView()));
			Buffer = MoveTemp(OuterBuffer);
		}
	}

	/** Construct a value that holds a reference to the buffer of the outer that contains it. */
	template <typename OtherViewType>
	inline TCbBuffer(const ViewType& Value, TCbBuffer<OtherViewType> OuterBuffer)
		: TCbBuffer(Value, MoveTemp(OuterBuffer.Buffer))
	{
	}

	/** Reset this to a default value and null buffer. */
	inline void Reset() { *this = TCbBuffer(); }

	/** Whether this reference has ownership of the memory in its buffer. */
	inline bool IsOwned() const { return Buffer && Buffer.IsOwned(); }

	/** Clone the value, if necessary, to a buffer that this reference has ownership of. */
	inline void MakeOwned()
	{
		if (!IsOwned())
		{
			FUniqueBuffer MutableBuffer = FUniqueBuffer::Alloc(ViewType::GetSize());
			ViewType::CopyTo(MutableBuffer);
			ViewType::operator=(ViewType(MutableBuffer.GetData()));
			Buffer = MoveTemp(MutableBuffer);
		}
	}

	/** Returns a buffer that exactly contains this value. */
	inline FSharedBuffer GetBuffer() const
	{
		const FMemoryView View = ViewType::GetView();
		const FSharedBuffer& OuterBuffer = GetOuterBuffer();
		return View == OuterBuffer.GetView() ? OuterBuffer : FSharedBuffer::MakeView(View, OuterBuffer);
	}

	/** Returns the outer buffer (if any) that contains this value. */
	inline const FSharedBuffer& GetOuterBuffer() const & { return Buffer; }
	inline FSharedBuffer GetOuterBuffer() && { return MoveTemp(Buffer); }

private:
	template <typename OtherType>
	friend class TCbBuffer;

	FSharedBuffer Buffer;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Factory functions for types derived from TCbBuffer.
 *
 * This uses the curiously recurring template pattern to construct the correct derived type, that
 * must inherit from TCbBuffer and this type to expose the factory functions.
 */
template <typename Type, typename ViewType>
class TCbBufferFactory
{
public:
	/** Construct a value from an owned clone of its memory. */
	static inline Type Clone(const void* const Data)
	{
		return Clone(ViewType(Data));
	}

	/** Construct a value from an owned clone of its memory. */
	static inline Type Clone(const ViewType& Value)
	{
		Type Owned = MakeView(Value);
		Owned.MakeOwned();
		return Owned;
	}

	/** Construct a value from a read-only view of its memory and its optional outer buffer. */
	static inline Type MakeView(const void* const Data, FSharedBuffer OuterBuffer = FSharedBuffer())
	{
		return MakeView(ViewType(Data), MoveTemp(OuterBuffer));
	}

	/** Construct a value from a read-only view of its memory and its optional outer buffer. */
	static inline Type MakeView(const ViewType& Value, FSharedBuffer OuterBuffer = FSharedBuffer())
	{
		return Type(Value, MoveTemp(OuterBuffer));
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCbArray;
class FCbObject;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A field that includes a shared buffer for the memory that contains it.
 *
 * @see FCbFieldView
 * @see TCbBuffer
 */
class FCbField : public TCbBuffer<FCbFieldView>, public TCbBufferFactory<FCbField, FCbFieldView>
{
public:
	using TCbBuffer::TCbBuffer;

	/** Access the field as an object. Defaults to an empty object on error. */
	inline FCbObject AsObject() &;
	inline FCbObject AsObject() &&;

	/** Access the field as an array. Defaults to an empty array on error. */
	inline FCbArray AsArray() &;
	inline FCbArray AsArray() &&;

	/** Access the field as binary. Returns the provided default on error. */
	inline FSharedBuffer AsBinary(const FSharedBuffer& Default = FSharedBuffer()) &;
	inline FSharedBuffer AsBinary(const FSharedBuffer& Default = FSharedBuffer()) &&;
};

/**
 * Iterator for FCbField.
 *
 * @see TCbFieldIterator
 */
class FCbFieldIterator : public TCbFieldIterator<FCbField>
{
public:
	/** Construct a field range from an owned clone of a range. */
	CORE_API static FCbFieldIterator CloneRange(const FCbFieldViewIterator& It);

	/** Construct a field range from an owned clone of a range. */
	static inline FCbFieldIterator CloneRange(const FCbFieldIterator& It)
	{
		return CloneRange(FCbFieldViewIterator(It));
	}

	/** Construct a field range that contains exactly one field. */
	static inline FCbFieldIterator MakeSingle(FCbField Field)
	{
		return FCbFieldIterator(MoveTemp(Field));
	}

	/**
	 * Construct a field range from a buffer containing zero or more valid fields.
	 *
	 * @param Buffer A buffer containing zero or more valid fields.
	 * @param Type HasFieldType means that Buffer contains the type. Otherwise, use the given type.
	 */
	static inline FCbFieldIterator MakeRange(FSharedBuffer Buffer, ECbFieldType Type = ECbFieldType::HasFieldType)
	{
		if (Buffer.GetSize())
		{
			const void* const DataEnd = Buffer.GetView().GetDataEnd();
			return FCbFieldIterator(FCbField(MoveTemp(Buffer), Type), DataEnd);
		}
		return FCbFieldIterator();
	}

	/** Construct a field range from an iterator and its optional outer buffer. */
	static inline FCbFieldIterator MakeRangeView(const FCbFieldViewIterator& It, FSharedBuffer OuterBuffer = FSharedBuffer())
	{
		return FCbFieldIterator(FCbField(It, MoveTemp(OuterBuffer)), GetFieldsEnd(It));
	}

	/** Construct an empty field range. */
	constexpr FCbFieldIterator() = default;

	/** Clone the range, if necessary, to a buffer that this has ownership of. */
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
 * An array that includes a shared buffer for the memory that contains it.
 *
 * @see FCbArrayView
 * @see TCbBuffer
 */
class FCbArray : public TCbBuffer<FCbArrayView>, public TCbBufferFactory<FCbArray, FCbArrayView>
{
public:
	using TCbBuffer::TCbBuffer;

	/** Create an iterator for the fields of this array. */
	inline FCbFieldIterator CreateIterator() const
	{
		return FCbFieldIterator::MakeRangeView(CreateViewIterator(), GetOuterBuffer());
	}

	/** Access the array as an array field. */
	inline FCbField AsField() const &
	{
		return FCbField(FCbArrayView::AsFieldView(), *this);
	}

	/** Access the array as an array field. */
	inline FCbField AsField() &&
	{
		return FCbField(FCbArrayView::AsFieldView(), MoveTemp(*this));
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * An object that includes a shared buffer for the memory that contains it.
 *
 * @see FCbObjectView
 * @see TCbBuffer
 */
class FCbObject : public TCbBuffer<FCbObjectView>, public TCbBufferFactory<FCbObject, FCbObjectView>
{
public:
	using TCbBuffer::TCbBuffer;

	/** Create an iterator for the fields of this object. */
	inline FCbFieldIterator CreateIterator() const
	{
		return FCbFieldIterator::MakeRangeView(CreateViewIterator(), GetOuterBuffer());
	}

	/** Find a field by case-sensitive name comparison. */
	inline FCbField Find(FAnsiStringView Name) const
	{
		if (::FCbFieldView Field = FindView(Name))
		{
			return FCbField(Field, *this);
		}
		return FCbField();
	}

	/** Find a field by case-insensitive name comparison. */
	inline FCbField FindIgnoreCase(FAnsiStringView Name) const
	{
		if (::FCbFieldView Field = FindViewIgnoreCase(Name))
		{
			return FCbField(Field, *this);
		}
		return FCbField();
	}

	/** Access the object as an object field. */
	inline FCbField AsField() const &
	{
		return FCbField(FCbObjectView::AsFieldView(), *this);
	}

	/** Access the object as an object field. */
	inline FCbField AsField() &&
	{
		return FCbField(FCbObjectView::AsFieldView(), MoveTemp(*this));
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline FCbObject FCbField::AsObject() &
{
	return IsObject() ? FCbObject(AsObjectView(), *this) : FCbObject();
}

inline FCbObject FCbField::AsObject() &&
{
	return IsObject() ? FCbObject(AsObjectView(), MoveTemp(*this)) : FCbObject();
}

inline FCbArray FCbField::AsArray() &
{
	return IsArray() ? FCbArray(AsArrayView(), *this) : FCbArray();
}

inline FCbArray FCbField::AsArray() &&
{
	return IsArray() ? FCbArray(AsArrayView(), MoveTemp(*this)) : FCbArray();
}

inline FSharedBuffer FCbField::AsBinary(const FSharedBuffer& Default) &
{
	const FMemoryView View = AsBinaryView();
	return !HasError() ? FSharedBuffer::MakeView(View, GetOuterBuffer()) : Default;
}

inline FSharedBuffer FCbField::AsBinary(const FSharedBuffer& Default) &&
{
	const FMemoryView View = AsBinaryView();
	return !HasError() ? FSharedBuffer::MakeView(View, MoveTemp(*this).GetOuterBuffer()) : Default;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
