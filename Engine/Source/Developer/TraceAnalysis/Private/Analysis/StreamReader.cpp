// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StreamReader.h"
#include "DataStream.h"
#include "HAL/UnrealMemory.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
const uint8* FStreamReader::GetPointer(uint32 Size) const
{
	if (Cursor + Size > End)
	{
		LastRequestedSize = Size;
		return nullptr;
	}

	return Cursor;
}

////////////////////////////////////////////////////////////////////////////////
void FStreamReader::Advance(uint32 Size)
{
	Cursor += Size;
	check(Cursor <= End);
}



////////////////////////////////////////////////////////////////////////////////
FStreamReaderDetail::FStreamReaderDetail(IInDataStream& InDataStream)
: DataStream(InDataStream)
, BufferSize(1024)
{
	Buffer = (uint8*)FMemory::Malloc(BufferSize);
}

////////////////////////////////////////////////////////////////////////////////
FStreamReaderDetail::~FStreamReaderDetail()
{
	FMemory::Free(Buffer);
}

////////////////////////////////////////////////////////////////////////////////
bool FStreamReaderDetail::Read()
{
	int32 Remaining = int32(UPTRINT(End - Cursor));

	uint32 Demand = Remaining + LastRequestedSize;
	if (Demand > BufferSize)
	{
		const uint32 GrowthSizeMask = (8 << 10) - 1;
		BufferSize = (Demand + GrowthSizeMask) & ~GrowthSizeMask;
		uint8* NextBuffer = (uint8*)FMemory::Realloc(Buffer, BufferSize);

		SIZE_T Delta = NextBuffer - Buffer;
		Cursor += Delta;
		End += Delta;
		Buffer += Delta;
	}

	if (Remaining != 0)
	{
		memmove(Buffer, Cursor, Remaining);
	}

	uint8* Dest = Buffer + Remaining;
	int32 ReadSize = DataStream.Read(Dest, BufferSize - Remaining);
	if (ReadSize <= 0)
	{
		return false;
	}

	Cursor = Buffer;
	End = Dest + ReadSize;
	return true;
}

} // namespace Trace
