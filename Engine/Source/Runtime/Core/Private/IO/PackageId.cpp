// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/PackageId.h"
#include "Serialization/StructuredArchive.h"

FPackageId FPackageId::FromName(const FName& Name, bool bAsOptional)
{
	TCHAR NameStr[FName::StringBufferSize + 2];
	uint32 NameLen = Name.ToString(NameStr);

	if (bAsOptional)
	{
		NameStr[NameLen++] = '.';
		NameStr[NameLen++] = 'o';
		NameStr[NameLen] = '\0';
	}

	for (uint32 I = 0; I < NameLen; ++I)
	{
		NameStr[I] = TChar<TCHAR>::ToLower(NameStr[I]);
	}
	uint64 Hash = CityHash64(reinterpret_cast<const char*>(NameStr), NameLen * sizeof(TCHAR));
	checkf(Hash != InvalidId, TEXT("Package name hash collision \"%s\" and InvalidId"), NameStr);
	return FPackageId(Hash);
}

FArchive& operator<<(FArchive& Ar, FPackageId& Value)
{
	FStructuredArchiveFromArchive(Ar).GetSlot() << Value;
	return Ar;
}

void operator<<(FStructuredArchiveSlot Slot, FPackageId& Value)
{
	Slot << Value.Id;
}
