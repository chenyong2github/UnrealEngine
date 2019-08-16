// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Logging/LogMacros.h"
#include "Containers/UnrealString.h"
#include "Trace/DataStream.h"

#include <memory.h>

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FFileStream
	: public IInDataStream
{
public:
					FFileStream(const TCHAR* FilePath);
	virtual			~FFileStream();
	virtual int32	Read(void* Data, uint32 Size) override;
	void			UpdateFileSize();

private:
	void			OpenFileInternal();

	FString			FilePath;
	IFileHandle*	Inner = nullptr;
	uint64			Cursor = 0;
	uint64			End = 0;
};

////////////////////////////////////////////////////////////////////////////////
class FStreamReader
{
public:
	class FData
	{
		friend			FStreamReader;
		const uint8*	Cursor = nullptr;
		const uint8*	End = nullptr;

	public:
		const uint8*	GetPointer(uint32 Size) const;
		void			Advance(uint32 Size);
	};

						FStreamReader(IInDataStream& InDataStream);
						~FStreamReader();
	FData*				Read();

private:
	static const uint32	BufferSize = 1 << 18;
	FData				Data;
	IInDataStream&		DataStream;
	uint8*				Buffer;
};

////////////////////////////////////////////////////////////////////////////////
inline FStreamReader::FData* FStreamReader::Read()
{
	int32 Remaining = int32(UPTRINT(Data.End - Data.Cursor));
	if (Remaining != 0)
	{
		memcpy(Buffer, Data.Cursor, Remaining);
	}

	uint8* Dest = Buffer + Remaining;
	int32 ReadSize = DataStream.Read(Dest, BufferSize - Remaining);
	if (ReadSize <= 0)
	{
		return nullptr;
	}

	Data.Cursor = Buffer;
	Data.End = Dest + ReadSize;
	return &Data;
}

////////////////////////////////////////////////////////////////////////////////
inline const uint8* FStreamReader::FData::GetPointer(uint32 Size) const
{
	if (Cursor + Size > End)
	{
		return nullptr;
	}

	return Cursor;
}

////////////////////////////////////////////////////////////////////////////////
inline void FStreamReader::FData::Advance(uint32 Size)
{
	Cursor += Size;
	check(Cursor <= End);
}

} // namespace Trace
