// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compression/CompressedBuffer.h"
#include "HAL/CriticalSection.h"
#include "Logging/LogMacros.h"
#include "Virtualization/PayloadId.h"

#define UE_USE_VIRTUALBULKDATA 1

// Enables a code path that will allow VBD to be converted back to old style bulkdata if we need to revert for some reason
#if !UE_USE_VIRTUALBULKDATA
	#define UE_VBD_TO_OLD_BULKDATA_PATH 1

	// When reverting to old bulkdata we will need to add a new version to FUE5MainStreamObjectVersion, but since it is unlikely
	// that we will ever do this, I'd prefer not to submit it to FUE5MainStreamObjectVersion in a disabled form.
	// This static assert will remind anyone turning on this path that the version needs to be updated and hopefully we can just
	// delete this without ever enabling it once we take the plunge and strip out UE_USE_VIRTUALBULKDATA.
	static_assert(false, "Add the following two lines to FUE5MainStreamObjectVersion!");
	//	// Disabled content virtualization due to crippling problems, sadly StaticMesh and Textures saved with virtualization will need reverting.
	//	DisabledVirtualization,
#else
	#define UE_VBD_TO_OLD_BULKDATA_PATH 0
#endif //UE_VBD_TO_OLD_BULKDATA_PATH

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

/**
 * Configuring the backend hierarchy
 * 
 * The [Core.ContentVirtualization] section can contain a string 'BackendGraph' which will set with the name of  
 * the backend graph, if not set then the default 'ContentVirtualizationBackendGraph_None' will be used instead. 
 * This value can also be overridden from the command line by using 'BackendGraph=FooBar' where FooBar is the 
 * name of the graph.
 * 
 * The first entry in the graph to be parsed will be the 'Hierarchy' which describes which backends should be
 * mounted and in which order. For example 'Hierarchy=(Entry=Foo, Entry=Bar)' which should mount two backends
 * 'Foo' and 'Bar' in that order. 
 * 
 * Each referenced backend in the hierarchy will then require it's own entry in the graph where the key will be
 * it's name in the hierarchy and the value a string describing how to set it up. 
 * The value must contain 'Type=X' where X is the name used to find the correct IVirtualizationBackendFactory 
 * to create the backend with. 
 * Once the backend is created then reset of the string will be passed to it, so that additional customization
 * can be extracted. Depending on the backend implementation these values may or may not be required.
 * 
 * Example graph:
 * [ContentVirtualizationBackendGraph_Example] 
 * Hierarchy=(Entry=MemoryCache, Entry=NetworkShare)
 * MemoryCache=(Type=InMemory)
 * NetworkShare=(Type=FileSystem, Path="\\path\to\somewhere")
 * 
 * The graph is named 'ContentVirtualizationBackendGraph_Example'.
 * The hierarchy contains two entries 'InMemory' and 'NetworkShare' to be mounted in that order
 * MemoryCache creates a backend of type 'InMemory' and has no additional customization
 * NetworkShare creates a backend of type 'FileSystem' and provides an additional path, the filesystem backend would 
 * fatal error without this value.
 */

namespace UE::Virtualization
{

class IVirtualizationBackend;
class IVirtualizationBackendFactory;


/** This is used as a wrapper around the various potential back end implementations. 
	The calling code shouldn't need to care about which back ends are actually in use. */
class COREUOBJECT_API FVirtualizationManager
{
public:
	/** Singleton access */
	static FVirtualizationManager& Get();

	FVirtualizationManager();
	~FVirtualizationManager();

	/** 
	 * Push a payload to the virtualization backends.
	 * 
	 * @param Id
	 * @param Payload
	 * @returns
	 */
	bool PushData(const FPayloadId& Id, const FCompressedBuffer& Payload);

	/** 
	 * Pull a payload from the virtualization backends.
	 *
	 * @param Id
	 * @returns
	 */
	FCompressedBuffer PullData(const FPayloadId& Id);

private:

	void ApplySettingsFromConfigFiles(const FConfigFile& PlatformEngineIni);
	void ApplySettingsFromCmdline();

	void ApplyDebugSettingsFromConfigFiles(const FConfigFile& PlatformEngineIni);

	void MountBackends();
	bool CreateBackend(const TCHAR* GraphName, const FString& ConfigEntryName, TMap<FName, IVirtualizationBackendFactory*>& FactoryLookupTable);

	void AddBackend(class IVirtualizationBackend* Backend);

	/** Are payloads allowed to be virtualized. Defaults to true. */
	bool bEnablePayloadPushing;

	/** The minimum length for a payload to be considered for virtualization. Defaults to 0 bytes. */
	int64 MinPayloadLength;

	/** The name of the backend graph to load from the config ini file that will describe the backend hierarchy */
	FString BackendGraphName;

	/** Debugging option: When enabled all public operations will be performed as single threaded. This is intended to aid debugging and not for production use.*/
	bool bForceSingleThreaded;
	
	/** 
	 * Debugging option: When enabled all pull operations will fail so we can see which systems cannot survive ::PullData failing to find the virtualization 
	 * data at all. If ::PullData failures become fatal errors at some point then this option will cease to be useful.
	 * This is intended to aid debugging and not for production use.
	 */
	bool bFailPayloadPullOperations;

	/**
	 * Debugging option: When enabled we will immediately 'pull' each payload after it has been 'pushed' and compare it to the original payload source to make 
	 * sure that it can be pulled correctly.
	 * This is intended to aid debugging and not for production use.
	 */
	bool bValidateAfterPushOperation;

	/** The critical section used to force single threaded access if bForceSingleThreaded is true */
	FCriticalSection ForceSingleThreadedCS;

	/** Collection of all the instantiated backends */
	TArray<IVirtualizationBackend*> AllBackendsArray;

	/** Collection of all the instantiated backends that support pull operations*/
	TArray<IVirtualizationBackend*> PullEnabledBackendsArray;

	/** Collection of all the instantiated backends that support push operations*/
	TArray<IVirtualizationBackend*> PushEnabledBackendsArray;
};

} // namespace UE::Virtualization
