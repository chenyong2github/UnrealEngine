// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Build.h"

#if !UE_BUILD_SHIPPING

#include "Templates/UniquePtr.h"

class IIoCache;

CORE_API TUniquePtr<IIoCache> MakeMemoryIoCache(uint64 CacheSize);

#endif // !UE_BUILD_SHIPPING
