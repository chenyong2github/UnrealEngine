// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// todo: prune includes
#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "IO/IoDispatcherBackend.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeRWLock.h"
#include "Stats/Stats.h"
#include "Templates/UniquePtr.h"

#ifndef PLATFORM_USE_STREAMING_FILE_CACHE
#define PLATFORM_USE_STREAMING_FILE_CACHE 0
#endif

class IStreamingFileSystem : public IIoDispatcherBackend
{
public:
	static STREAMINGFILESYSTEM_API TSharedRef<IStreamingFileSystem> CreateStreamingFileSystem();
};
