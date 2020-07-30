// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigLogicMemoryStream.h"

FRigLogicMemoryStream::FRigLogicMemoryStream(TArray<uint8>* Buffer)
{
	BitStreamBuffer = Buffer; //don't allocate new memory, just point to the existing one
}

void FRigLogicMemoryStream::seek(size_t Position)
{
	PositionInBuffer = Position;
}

size_t FRigLogicMemoryStream::tell()
{
	return PositionInBuffer;
}

void FRigLogicMemoryStream::open()
{
	PositionInBuffer = 0;
}

void FRigLogicMemoryStream::read(char* ReadToBuffer, size_t Size)
{
	FMemory::Memcpy(ReadToBuffer, BitStreamBuffer->GetData() + PositionInBuffer, Size);
	PositionInBuffer = PositionInBuffer + Size;
}

void FRigLogicMemoryStream::write(const char* WriteFromBuffer, size_t Size)
{
	FString writeStr;
	int32 BufferSize = BitStreamBuffer->Num();
	ensure(BufferSize >= 0);
	if (PositionInBuffer + Size > static_cast<size_t>(BufferSize))
	{
		int32 difference = PositionInBuffer + Size - BufferSize;
		BitStreamBuffer->AddUninitialized(difference);
	}
	FMemory::Memcpy(BitStreamBuffer->GetData() + PositionInBuffer, WriteFromBuffer, Size);
	PositionInBuffer = PositionInBuffer + Size;
}

size_t FRigLogicMemoryStream::size()
{
	return BitStreamBuffer->Num();
}
