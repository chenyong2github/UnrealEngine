// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/UniquePtr.h"

class IIoCache;

struct FFileIoCacheConfig
{
	uint64 DiskStorageSize;
	uint64 MemoryStorageSize;
};

CORE_API TUniquePtr<IIoCache> MakeFileIoCache(const FFileIoCacheConfig& Config);
