// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/IoContainerId.h"
#include "Serialization/StructuredArchive.h"

FArchive& operator<<(FArchive& Ar, FIoContainerId& ContainerId)
{
	Ar << ContainerId.Id;

	return Ar;
}

void operator<<(FStructuredArchiveSlot Slot, FIoContainerId& Value)
{
	Slot << Value.Id;
}
