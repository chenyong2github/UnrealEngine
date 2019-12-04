// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "UObject/PackageId.h"
#include "Serialization/StructuredArchive.h"

FArchive& operator<<(FArchive& Ar, FPackageId& Value)
{
	FStructuredArchiveFromArchive(Ar).GetSlot() << Value;
	return Ar;
}

void operator<<(FStructuredArchiveSlot Slot, FPackageId& Value)
{
	Slot << Value.Id;
}
