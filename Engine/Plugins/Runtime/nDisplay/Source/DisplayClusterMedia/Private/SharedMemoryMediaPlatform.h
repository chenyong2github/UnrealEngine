// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"

#include "SharedMemoryMediaTypes.h"


/** This is the interface to the platform specific functions needed by shared memory media. 
 *  It is mostly about the platform specific implementation of cross gpu textures.
 */
class FSharedMemoryMediaPlatform
{
public:

	/** This is the type definition of the rhi platform factory function that each rhi platfom implementation can register with.  */
	typedef TSharedPtr<FSharedMemoryMediaPlatform>(*CreateSharedMemoryMediaPlatform)();

public:

	/** TMap of the collection of registered rhi platform factory functions */
	static TMap<ERHIInterfaceType, CreateSharedMemoryMediaPlatform> PlatformCreators;

public:

	/** Helper function to get a stringified ERHIInterfaceType */
	static FString GetRhiTypeString(const ERHIInterfaceType RhiType);

	/** Rhi platform implementations call this function to register their factory creation function */
	static bool RegisterPlatformForRhi(ERHIInterfaceType RhiType, CreateSharedMemoryMediaPlatform PlatformCreator);

	/** This factory function will create an instance of the rhi platform specific implementation, if it has been registered. */
	static TSharedPtr<FSharedMemoryMediaPlatform, ESPMode::ThreadSafe> CreateInstanceForRhi(ERHIInterfaceType RhiType);

	/** Creates a cross gpu texture */
	virtual FTextureRHIRef CreateSharedCrossGpuTexture(EPixelFormat Format, int32 Width, int32 Height, const FGuid& Guid, uint32 BufferIdx)
	{
		return nullptr;
	}

	/** Opens a cross gpu texture specified by a Guid */
	virtual FTextureRHIRef OpenSharedCrossGpuTextureByGuid(const FGuid& Guid, FSharedMemoryMediaTextureDescription& OutTextureDescription)
	{
		return nullptr;
	}

	/** Release any platform specific resources related to the indexed texture */
	virtual void ReleaseSharedCrossGpuTexture(uint32 BufferIdx) {};

};

