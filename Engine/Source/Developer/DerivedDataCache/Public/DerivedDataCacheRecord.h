// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "DerivedDataCacheKey.h"
#include "Memory/SharedBuffer.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryPackage.h"

#define UE_API DERIVEDDATACACHE_API

namespace UE
{
namespace DerivedData
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** The type of value stored in a cache record. */
enum class ECacheRecordType : uint8
{
	None,
	Binary,
	Object,
	Package,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A cache record is a key, a value, attachments, and metadata.
 *
 * The key is expected to correspond uniquely to its value and attachments. The key should not be
 * used with any other value and attachments. The metadata does not have the same requirement and
 * may be used to persist details such as the machine that created the value or how much time was
 * spent creating the value.
 *
 * The value may be binary or an object. Only object values may have attachments, and attachments
 * are consumed by accessing the cache record as a package. It is valid to access an object as if
 * it is a package with no attachments, or to access a package as though it is an object. This is
 * not valid for binary values, which may not have attachments or be interpreted as another type.
 *
 * It is invalid for a package to include attachments that are not referenced by the object or by
 * one of its referenced attachments.
 */
class FCacheRecord
{
public:
	/** Construct an empty cache record. */
	UE_API FCacheRecord();

	/** Reset to an empty record. */
	UE_API void Reset();

	inline const FCacheKey& GetKey() const { return Key; }
	inline void SetKey(const FCacheKey& InKey) { Key = InKey; }

	inline const FCbObjectRef& GetMeta() const { return Meta; }
	inline void SetMeta(FCbObjectRef InMeta) { Meta = MoveTemp(InMeta); }

	inline ECacheRecordType GetType() const { return Type; }

	/** Access the value as binary. Returns a null buffer if not binary. */
	inline const FSharedBuffer& AsBinary() const { return Binary; }
	/** Access the value as an object. Returns an empty object if not an object or package. */
	inline const FCbObjectRef& AsObject() const { return Package.GetObject(); }
	/** Access the value as a package. Returns a null package if not an object or package. */
	inline const FCbPackage& AsPackage() const { return Package; }

	/** Returns the hash of the value, or the zero hash if there is no value. */
	inline const FIoHash& GetValueHash() const;

	/** Set the value as binary. Removes any existing value and attachments. */
	UE_API void SetBinary(FSharedBuffer Value);
	/** Set the value as binary with its hash. Removes any existing value and attachments. */
	UE_API void SetBinary(FSharedBuffer Value, const FIoHash& ValueHash);

	/** Set the value as an object. Removes any existing value and attachments. */
	UE_API void SetObject(FCbObjectRef Value);
	/** Set the value as an object with its hash. Removes any existing value and attachments. */
	UE_API void SetObject(FCbObjectRef Value, const FIoHash& ValueHash);

	/** Set the value as a package. Removes any existing value and attachments. */
	UE_API void SetPackage(FCbPackage Value);

private:
	FCacheKey Key;
	FCbObjectRef Meta;

	FCbPackage Package;
	FSharedBuffer Binary;
	FIoHash BinaryHash;
	ECacheRecordType Type = ECacheRecordType::None;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline const FIoHash& FCacheRecord::GetValueHash() const
{
	return Type == ECacheRecordType::Binary ? BinaryHash : Package.GetObjectHash();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE

#undef UE_API
