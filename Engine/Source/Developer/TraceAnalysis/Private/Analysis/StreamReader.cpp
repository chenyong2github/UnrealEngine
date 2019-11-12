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
