// Copyright Epic Games, Inc. All Rights Reserved.

#include "Virtualization/PayloadId.h"

#include "Hash/Blake3.h"
#include "Memory/SharedBuffer.h"
#include "Misc/Guid.h"
#include "Misc/StringBuilder.h"
#include "Serialization/Archive.h"

namespace UE
{
namespace Virtualization
{

FPayloadId::FPayloadId(const FIoHash& BlakeHash)
	: Identifier(BlakeHash)
{
	bIsHashSet = !Identifier.IsZero();
}

FPayloadId::FPayloadId(const FSharedBuffer& Payload)
{ 
	// We consider not having a valid payload and having an empty payload to be the same.
	if (Payload.GetSize() > 0)
	{
		Identifier = FIoHash::HashBuffer(Payload);
		bIsHashSet = true;
	}
}

// TODO: Currently only used for backwards compatibility, we might want to make this private and allow 
// FVirtualizedUntypedBulkData::CreateFromBulkData to access it by being a friend.
FPayloadId::FPayloadId(const FGuid& Guid)
{
	if (Guid.IsValid())
	{
		// Hash each element individually rather than making assumptions about
		// the internal layout of FGuid and treating it as a contiguous buffer.
		// Slightly slower, but safer.
		FBlake3 Hash;

		Hash.Update(&Guid[0], sizeof(uint32));
		Hash.Update(&Guid[1], sizeof(uint32));
		Hash.Update(&Guid[2], sizeof(uint32));
		Hash.Update(&Guid[3], sizeof(uint32));

		Identifier = FIoHash(Hash.Finalize());
		bIsHashSet = true;
	}
}

void FPayloadId::Reset()
{
	Identifier.Reset();
	bIsHashSet = false;
}

FGuid FPayloadId::ToGuid() const
{
	if (IsValid())
	{
		// We use the first 16 bytes of the FIoHash to create the guid, there is
		// no specific reason why these were chosen, we could take any pattern or combination
		// of bytes.
		uint32* HashBytes = (uint32*)Identifier.GetBytes();
		return FGuid(HashBytes[0], HashBytes[1], HashBytes[2], HashBytes[3]);
	}
	else
	{
		return FGuid();
	}
}

FString FPayloadId::ToString() const
{
	if (IsValid())
	{	
		TStringBuilder<65> HexStringBuilder;
		HexStringBuilder << Identifier;

		return HexStringBuilder.ToString();
	}
	else
	{
		return FString();
	}
}

bool FPayloadId::operator == (const FPayloadId& InPayloadId) const
{
	// If ::bIsHashSet is different between the two payloads we know they are different.
	if (bIsHashSet != InPayloadId.bIsHashSet)
	{
		return false;
	}

	// If ::bIsHashSet is false for both payload then we know they are the same.
	if (!bIsHashSet)
	{
		return true;
	}

	// Compare the two hashes
	return Identifier == InPayloadId.Identifier;
}

FArchive& operator<<(FArchive& Ar, FPayloadId& PayloadId)
{
	Ar << PayloadId.Identifier;

	if (Ar.IsLoading())
	{
		PayloadId.bIsHashSet = !PayloadId.Identifier.IsZero();
	}

	return Ar;
}

} // namespace Virtualization
} // namespace UE
