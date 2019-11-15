// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StreamReader.h"
#include "HAL/UnrealMemory.h"
#include "Math/UnrealMath.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
FStreamReader::~FStreamReader()
{
	FMemory::Free(Buffer);
}

////////////////////////////////////////////////////////////////////////////////
const uint8* FStreamReader::GetPointer(uint32 Size)
{
	if (Cursor + Size > End)
	{
		DemandHint = FMath::Max(DemandHint, Size);
		return nullptr;
	}

	return Buffer + Cursor;
}

////////////////////////////////////////////////////////////////////////////////
void FStreamReader::Advance(uint32 Size)
{
	Cursor += Size;
}

////////////////////////////////////////////////////////////////////////////////
bool FStreamReader::IsEmpty() const
{
	return Cursor >= End;
}



////////////////////////////////////////////////////////////////////////////////
void FStreamBuffer::Consolidate()
{
	int32 Remaining = End - Cursor;
	DemandHint += Remaining;

	if (DemandHint >= BufferSize)
	{
		const uint32 GrowthSizeMask = (8 << 10) - 1;
		BufferSize = (DemandHint + GrowthSizeMask + 1) & ~GrowthSizeMask;
		Buffer = (uint8*)FMemory::Realloc(Buffer, BufferSize);
	}

	if (!Remaining)
	{
		Cursor = 0;
		End = 0;
	}
	else if (Cursor)
	{
		memmove(Buffer, Buffer + Cursor, Remaining);
		Cursor = 0;
		End = Remaining;
		check(End <= BufferSize);
	}

	DemandHint = 0;
}

} // namespace Trace
