// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringView.h"
#include "Memory/MemoryFwd.h"
#include "Serialization/CompactBinary.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FArchive;
struct FDateTime;
struct FGuid;
struct FIoHash;
struct FTimespan;

/**
 * A writer for compact binary object, arrays, and fields.
 *
 * The writer produces a sequence of fields that can be saved to a provided memory buffer or into
 * a new owned buffer. The typical use case is to write a single object, which can be accessed by
 * calling Save().AsObjectRef() or Save(Buffer).AsObject().
 *
 * The writer will assert on most incorrect usage and will always produce valid compact binary if
 * provided with valid input. The writer does not check for invalid UTF-8 string encoding, object
 * fields with duplicate names, or invalid compact binary being copied from another source.
 *
 * It is most convenient to use the streaming API for the writer, as demonstrated in the example.
 *
 * When writing a small amount of compact binary data, TCbWriter can be more efficient as it uses
 * a fixed-size stack buffer for storage before spilling onto the heap.
 *
 * @see TCbWriter
 *
 * Example:
 *
 * FCbObjectRef WriteObject()
 * {
 *     TCbWriter<256> Writer;
 *     Writer.BeginObject();
 * 
 *     Writer << "Resize" << true;
 *     Writer << "MaxWidth" << 1024;
 *     Writer << "MaxHeight" << 1024;
 * 
 *     Writer.BeginArray();
 *     Writer << "FormatA" << "FormatB" << "FormatC";
 *     Writer.EndArray();
 * 
 *     Writer.EndObject();
 *     return Writer.Save().AsObjectRef();
 * }
 */
class FCbWriter
{
public:
	CORE_API FCbWriter();
	CORE_API ~FCbWriter();

	FCbWriter(const FCbWriter&) = delete;
	FCbWriter& operator=(const FCbWriter&) = delete;

	/** Empty the writer without releasing any allocated memory. */
	CORE_API void Reset();

	/**
	 * Serialize the field(s) to an owned buffer and return it as an iterator.
	 *
	 * It is not valid to call this function in the middle of writing an object, array, or field.
	 * The writer remains valid for further use when this function returns.
	 */
	CORE_API FCbFieldRefIterator Save() const;

	/**
	 * Serialize the field(s) to memory.
	 *
	 * It is not valid to call this function in the middle of writing an object, array, or field.
	 * The writer remains valid for further use when this function returns.
	 *
	 * @param Buffer A mutable memory view to write to. Must be exactly GetSaveSize() bytes.
	 * @return An iterator for the field(s) written to the buffer.
	 */
	CORE_API FCbFieldIterator Save(FMutableMemoryView Buffer) const;

	/**
	 * Serialize the field(s) to an archive.
	 *
	 * It is not valid to call this function in the middle of writing an object, array, or field.
	 * The writer remains valid for further use when this function returns.
	 *
	 * @param Ar An archive to write to. Exactly GetSaveSize() bytes will be written.
	 */
	CORE_API void Save(FArchive& Ar) const;

	/**
	 * The size of buffer (in bytes) required to serialize the fields that have been written.
	 *
	 * It is not valid to call this function in the middle of writing an object, array, or field.
	 */
	CORE_API uint64 GetSaveSize() const;

	/**
	 * Write a field name.
	 *
	 * It is not valid to call this function when writing an array field.
	 * Names must be valid UTF-8 and must be unique within an object.
	 */
	CORE_API FCbWriter& Name(FAnsiStringView Name);

	/** Copy the value (not the name) of an existing field. */
	CORE_API void Field(const FCbField& Value);
	/** Copy the value (not the name) of an existing field. Holds a reference if owned. */
	CORE_API void Field(const FCbFieldRef& Value);

	/** Begin a new object. Must have a matching call to EndObject. */
	CORE_API void BeginObject();
	/** End an object after its fields have been written. */
	CORE_API void EndObject();

	/** Copy the value (not the name) of an existing object. */
	CORE_API void Object(const FCbObject& Value);
	/** Copy the value (not the name) of an existing object. Holds a reference if owned. */
	CORE_API void Object(const FCbObjectRef& Value);

	/** Begin a new array. Must have a matching call to EndArray. */
	CORE_API void BeginArray();
	/** End an array after its fields have been written. */
	CORE_API void EndArray();

	/** Copy the value (not the name) of an existing array. */
	CORE_API void Array(const FCbArray& Value);
	/** Copy the value (not the name) of an existing array. Holds a reference if owned. */
	CORE_API void Array(const FCbArrayRef& Value);

	/** Write a null field. */
	CORE_API void Null();

	/** Write a binary field by copying Size bytes from Value. */
	CORE_API void Binary(const void* Value, uint64 Size);
	/** Write a binary field by copying the view. */
	inline void Binary(FMemoryView Value) { Binary(Value.GetData(), Value.GetSize()); }
	/** Write a binary field by copying the buffer. Holds a reference if owned. */
	CORE_API void Binary(const FSharedBuffer& Buffer);

	/** Write a string field by copying the UTF-8 value. */
	CORE_API void String(FAnsiStringView Value);
	/** Write a string field by converting the UTF-16 value to UTF-8. */
	CORE_API void String(FWideStringView Value);

	/** Write an integer field. */
	CORE_API void Integer(int32 Value);
	/** Write an integer field. */
	CORE_API void Integer(int64 Value);
	/** Write an integer field. */
	CORE_API void Integer(uint32 Value);
	/** Write an integer field. */
	CORE_API void Integer(uint64 Value);

	/** Write a float field from a 32-bit float value. */
	CORE_API void Float(float Value);
	/** Write a float field from a 64-bit float value. */
	CORE_API void Float(double Value);

	/** Write a bool field. */
	CORE_API void Bool(bool bValue);

	/** Write a field referencing compact binary data by its hash. */
	CORE_API void CompactBinaryAttachment(const FIoHash& Value);
	/** Write a field referencing binary data by its hash. */
	CORE_API void BinaryAttachment(const FIoHash& Value);

	/** Write a hash field. */
	CORE_API void Hash(const FIoHash& Value);
	/** Write a UUID field. */
	CORE_API void Uuid(const FGuid& Value);

	/** Write a date/time field with the specified count of 100ns ticks since the epoch. */
	CORE_API void DateTimeTicks(int64 Ticks);

	/** Write a date/time field. */
	CORE_API void DateTime(FDateTime Value);

	/** Write a time span field with the specified count of 100ns ticks. */
	CORE_API void TimeSpanTicks(int64 Ticks);

	/** Write a time span field. */
	CORE_API void TimeSpan(FTimespan Value);

	/** Private flags that are public to work with ENUM_CLASS_FLAGS. */
	enum class EStateFlags : uint8;

protected:
	/** Reserve the specified size up front until the format is optimized. */
	CORE_API explicit FCbWriter(int64 InitialSize);

private:
	friend FCbWriter& operator<<(FCbWriter& Writer, FAnsiStringView NameOrValue);

	/** Begin writing a field. Called twice for fields with names and once otherwise. */
	void BeginField();

	/** Finish writing a field by writing its type. */
	void EndField(ECbFieldType Type);

	/** Write the field name if valid in this state, otherwise write the string value. */
	CORE_API void NameOrString(FAnsiStringView NameOrValue);

	/** A view of the name of the current field (if any), otherwise the empty view. */
	FAnsiStringView CurrentName() const;

	/** Remove field types after the first to make the sequence uniform. */
	void MakeFieldsUniform(int64 FieldBeginOffset, int64 FieldEndOffset);

	/** State of the object, array, or top-level field being written. */
	struct FState
	{
		EStateFlags Flags{};
		/** The type of the fields in the sequence if uniform, otherwise None. */
		ECbFieldType UniformType{};
		/** The offset of the start of the current field. */
		int64 Offset{};
		/** The number of fields written in this state. */
		uint64 Count{};
	};

private:
	// This is a prototype-quality format for the writer. Using an array of bytes is inefficient,
	// and will lead to many unnecessary copies and moves of the data to resize the array, insert
	// object and array sizes, and remove field types for uniform objects and uniform arrays. The
	// optimized format will be a list of power-of-two blocks and an optional first block that is
	// provided externally, such as on the stack. That format will store the offsets that require
	// object or array sizes to be inserted and field types to be removed, and will perform those
	// operations only when saving to a buffer.
	TArray64<uint8> Data;
	TArray<FState> States;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A writer for compact binary object, arrays, and fields that uses a fixed-size stack buffer.
 *
 * @see FCbWriter
 */
template <uint32 InlineBufferSize>
class TCbWriter : public FCbWriter
{
public:
	inline TCbWriter()
		: FCbWriter(InlineBufferSize)
	{
	}

	TCbWriter(const TCbWriter&) = delete;
	TCbWriter& operator=(const TCbWriter&) = delete;

private:
	// Reserve the inline buffer now even though we are unable to use it. This will avoid causing
	// new stack overflows when this functionality is properly implemented in the future.
	uint8 Buffer[InlineBufferSize];
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Write the field name if valid in this state, otherwise write the string value. */
inline FCbWriter& operator<<(FCbWriter& Writer, FAnsiStringView NameOrValue)
{
	Writer.NameOrString(NameOrValue);
	return Writer;
}

/** Write the field name if valid in this state, otherwise write the string value. */
inline FCbWriter& operator<<(FCbWriter& Writer, const ANSICHAR* NameOrValue)
{
	return Writer << FAnsiStringView(NameOrValue);
}

inline FCbWriter& operator<<(FCbWriter& Writer, const FCbField& Value)
{
	Writer.Field(Value);
	return Writer;
}

inline FCbWriter& operator<<(FCbWriter& Writer, const FCbFieldRef& Value)
{
	Writer.Field(Value);
	return Writer;
}

inline FCbWriter& operator<<(FCbWriter& Writer, const FCbObject& Value)
{
	Writer.Object(Value);
	return Writer;
}

inline FCbWriter& operator<<(FCbWriter& Writer, const FCbObjectRef& Value)
{
	Writer.Object(Value);
	return Writer;
}

inline FCbWriter& operator<<(FCbWriter& Writer, const FCbArray& Value)
{
	Writer.Array(Value);
	return Writer;
}

inline FCbWriter& operator<<(FCbWriter& Writer, const FCbArrayRef& Value)
{
	Writer.Array(Value);
	return Writer;
}

inline FCbWriter& operator<<(FCbWriter& Writer, nullptr_t)
{
	Writer.Null();
	return Writer;
}

inline FCbWriter& operator<<(FCbWriter& Writer, FWideStringView Value)
{
	Writer.String(Value);
	return Writer;
}

inline FCbWriter& operator<<(FCbWriter& Writer, const WIDECHAR* Value)
{
	Writer.String(Value);
	return Writer;
}

inline FCbWriter& operator<<(FCbWriter& Writer, int32 Value)
{
	Writer.Integer(Value);
	return Writer;
}

inline FCbWriter& operator<<(FCbWriter& Writer, int64 Value)
{
	Writer.Integer(Value);
	return Writer;
}

inline FCbWriter& operator<<(FCbWriter& Writer, uint32 Value)
{
	Writer.Integer(Value);
	return Writer;
}

inline FCbWriter& operator<<(FCbWriter& Writer, uint64 Value)
{
	Writer.Integer(Value);
	return Writer;
}

inline FCbWriter& operator<<(FCbWriter& Writer, float Value)
{
	Writer.Float(Value);
	return Writer;
}

inline FCbWriter& operator<<(FCbWriter& Writer, double Value)
{
	Writer.Float(Value);
	return Writer;
}

inline FCbWriter& operator<<(FCbWriter& Writer, bool Value)
{
	Writer.Bool(Value);
	return Writer;
}

inline FCbWriter& operator<<(FCbWriter& Writer, const FIoHash& Value)
{
	Writer.Hash(Value);
	return Writer;
}

inline FCbWriter& operator<<(FCbWriter& Writer, const FGuid& Value)
{
	Writer.Uuid(Value);
	return Writer;
}

CORE_API FCbWriter& operator<<(FCbWriter& Writer, FDateTime Value);
CORE_API FCbWriter& operator<<(FCbWriter& Writer, FTimespan Value);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
