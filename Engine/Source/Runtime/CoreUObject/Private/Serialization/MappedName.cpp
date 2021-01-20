// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/MappedName.h"
#include "Serialization/Archive.h"
#include "UObject/NameBatchSerialization.h"

FArchive& operator<<(FArchive& Ar, FMappedName& MappedName)
{
	Ar << MappedName.Index << MappedName.Number;

	return Ar;
}

void FNameMap::Load(TArrayView<const uint8> NameBuffer, TArrayView<const uint8> HashBuffer, FMappedName::EType InNameMapType)
{
	LoadNameBatch(NameEntries, NameBuffer, HashBuffer);
	NameMapType = InNameMapType;
}
