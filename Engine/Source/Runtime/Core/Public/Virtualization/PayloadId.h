// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "IO/IoHash.h"

class FArchive;
class FSharedBuffer;

namespace UE::Virtualization
{
	
/** An identifier for a payload in the virtualization system. */
class CORE_API FPayloadId
{
public:
	FPayloadId() = default;

	/** Create a FPayloadId from the given FIoHash */
	explicit FPayloadId(const FIoHash& BlakeHash);

	/** Creates a FPayloadId based on hashing the given FSharedBuffer */
	explicit FPayloadId(const FSharedBuffer& Payload);

	/** 
	 * Used when we already have a FGuid identifying the payload and do not want to 
	 * pay the cost of loading and rehashing the payload each time we load the editor
	 */
	explicit FPayloadId(const FGuid& Guid);

	/** Returns true if the id is valid and should reference a payload */
	bool IsValid() const { return bIsHashSet;  }

	/** Reset the id to the initial, invalid state so that it does not reference a payload */
	void Reset();

	// TODO:  Although accessors are provided, this is mostly to provide compatibility with 
	// existing systems. Ideally we should remove these before UE5 is released to prevent their
	// use by licensees and try to encourage the use of FPayloadId directly.

	/** 
	 * Returns the id as a FGuid (provides compatibility for systems that expect a FGuid, 
	 * such as many of the original DDC implementations, over time we should be able to
	 * phase its use and remove it.
	 */
	FGuid ToGuid() const;

	/** Returns a string representation of the id, useful for debug tools */
	FString ToString() const;

	bool operator == (const FPayloadId& InPayloadId) const;
	bool operator != (const FPayloadId& InPayloadId) const { return !(*this == InPayloadId); }

	/** Serialization specialization */
	CORE_API friend FArchive& operator<<(FArchive& Ar, FPayloadId& PayloadId);

	/** Hash specialization */
	inline friend uint32 GetTypeHash(const FPayloadId& Hash);

	/** String builder specialization */
	template <typename CharType>
	friend TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FPayloadId& Id);
private:

	/** The actual identifier */
	FIoHash Identifier;

	/** 
	 * Keeps track of if Identifier has been set as checking all elements of a FIoHash isn't 
	 * exactly fast. Things are further complicated by FIoHash::HashBuffer(Ptr, 0) != FIoHash()
	 * so we would have two 'hash is not set' states to check for.
	 */
	bool bIsHashSet = false;
};

inline uint32 GetTypeHash(const FPayloadId& Hash)
{
	return GetTypeHash(Hash.Identifier);
}

template <typename CharType>
TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FPayloadId& Id)
{
	if (Id.IsValid())
	{
		Builder << Id.Identifier;
	}

	return Builder;
}

} // namespace UE::Virtualization
