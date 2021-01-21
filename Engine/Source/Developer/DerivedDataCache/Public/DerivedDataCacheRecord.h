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
 * A cache record is a key, metadata, and a value.
 *
 * The value may be one of binary, object, or package.
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
	/** Access the value as an object. Returns an empty object if not an object. */
	inline const FCbObjectRef& AsObject() const { return Object; }
	/** Access the value as a package. Returns a null package if not a package. */
	inline const FCbPackage& AsPackage() const { return Package; }

	/** Set the value as binary. Removes any existing value of another type. */
	UE_API void SetBinary(FSharedBuffer Value);
	/** Set the value as an object. Removes any existing value of another type. */
	UE_API void SetObject(FCbObjectRef Value);
	/** Set the value as a package. Removes any existing value of another type. */
	UE_API void SetPackage(FCbPackage Value);

private:
	FCacheKey Key;
	FCbObjectRef Meta;

	FSharedBuffer Binary;
	FCbObjectRef Object;
	FCbPackage Package;
	ECacheRecordType Type = ECacheRecordType::None;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE

#undef UE_API
