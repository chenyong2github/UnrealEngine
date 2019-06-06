// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Common/StringStore.h"

namespace Trace
{

FStringStore::FStringStore(FSlabAllocator& InAllocator)
	: Allocator(InAllocator)
{

}

const TCHAR* FStringStore::Store(const TCHAR* String)
{
	uint32 Hash = GetTypeHash(String);
	const TCHAR** AlreadyStored = StoredStrings.Find(Hash);
	if (AlreadyStored && !FCString::Strcmp(String, *AlreadyStored))
	{
		return *AlreadyStored;
	}
	
	int32 StringLength = FCString::Strlen(String) + 1;
	if (BufferLeft < StringLength)
	{
		BufferPtr = reinterpret_cast<TCHAR*>(Allocator.Allocate(BlockSize * sizeof(TCHAR)));
		++BlockCount;
		BufferLeft = BlockSize;
	}
	const TCHAR* Stored = BufferPtr;
	memcpy(BufferPtr, String, StringLength * sizeof(TCHAR));
	BufferLeft -= StringLength;
	BufferPtr += StringLength;
	if (!AlreadyStored)
	{
		StoredStrings.Add(Hash, Stored);
	}
	return Stored;
}

}
