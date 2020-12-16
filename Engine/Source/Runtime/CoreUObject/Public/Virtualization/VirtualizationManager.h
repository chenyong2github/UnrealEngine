// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"
#include "Misc/Guid.h"
#include "Memory/SharedBuffer.h"

// For UE_USE_VIRTUALBULKDATA
#include "UObject/UE5CookerObjectVersion.h"

COREUOBJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogVirtualization, Log, All);

// TODO: Do we want to keep this header public for UE5 release? If the only interaction should be
//		 via FVirtualizedUntypedBulkData or other such classes we might not want to expose this 
//		 at all.

// TODO: Remove when UE_USE_VIRTUALBULKDATA is removed  
/** Some methods can be turned to const when we are using VirtualBulkData, this macro will help with the upgrade path */
#if UE_USE_VIRTUALBULKDATA
	#define UE_VBD_CONST const
#else
	#define UE_VBD_CONST
#endif //UE_USE_VIRTUALBULKDATA

/** This is used as a wrapper around the various potential back end implementations. 
	The calling code shouldn't need to care about which back ends are actually in use. */
class COREUOBJECT_API FVirtualizationManager
{
public:
	/** Singleton access */
	static FVirtualizationManager& Get();

	FVirtualizationManager();
	~FVirtualizationManager() = default;

	/** Push a payload to the backends*/
	bool PushData(const FSharedBuffer& Payload, const FGuid& Guid);

	/** Pull a payload from the backends*/
	FSharedBuffer PullData(const FGuid& Guid);

private:

	void LoadSettingsFromConfigFiles();
	FString CreateFilePath(const FGuid& Guid);

	/** Are payloads allowed to be virtualized. Defaults to true. */
	bool bEnablePayloadPushing;

	/** The minimum length for a payload to be considered for virtualization. Defaults to 0 bytes. */
	int64 MinPayloadLength;
};
