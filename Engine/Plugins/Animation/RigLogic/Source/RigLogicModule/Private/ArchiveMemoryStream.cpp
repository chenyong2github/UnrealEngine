// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchiveMemoryStream.h"

FArchiveMemoryStream::FArchiveMemoryStream(FArchive* Archive) :
	Archive{Archive},
	Origin{}
{
	check(Archive != nullptr);
	Origin = Archive->Tell();
}

void FArchiveMemoryStream::seek(size_t Position)
{
	ensure(Position <= static_cast<size_t>(TNumericLimits<int64>::Max()));
	Archive->Seek(Origin + static_cast<int64>(Position));
}

size_t FArchiveMemoryStream::tell()
{
	const int64 RelPosition = (Archive->Tell() - Origin);
	ensure(RelPosition >= 0);
	return static_cast<size_t>(RelPosition);
}

void FArchiveMemoryStream::open()
{
}

void FArchiveMemoryStream::close()
{
}

void FArchiveMemoryStream::read(char* ReadToBuffer, size_t Size)
{
	Archive->Serialize(ReadToBuffer, Size);
}

void FArchiveMemoryStream::write(const char* WriteFromBuffer, size_t Size)
{
	Archive->Serialize(const_cast<char*>(WriteFromBuffer), Size);
}

size_t FArchiveMemoryStream::size()
{
	const int64 TotalSize = Archive->GetArchiveState().TotalSize();
	ensure(TotalSize >= 0);
	const int64 StreamSize = TotalSize - Origin;
	ensure(StreamSize >= 0);
	return static_cast<size_t>(StreamSize);
}
