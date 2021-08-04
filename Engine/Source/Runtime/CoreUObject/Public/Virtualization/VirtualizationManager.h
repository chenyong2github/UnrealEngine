// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compression/CompressedBuffer.h"
#include "HAL/CriticalSection.h"
#include "Logging/LogMacros.h"
#include "Templates/UniquePtr.h"
#include "Virtualization/PayloadId.h"

// TODO: Do we want to keep this header public for UE5 release? If the only interaction should be
//		 via FVirtualizedUntypedBulkData or other such classes we might not want to expose this 
//		 at all.

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

/** Profiling data containing all activity relating to payloads. */
struct FPayloadActivityInfo
{
	struct FActivity
	{
		/** The number of payloads that have been involved by the activity. */
		int64 PayloadCount = 0;
		/** The total size of all payloads involved in the activity, in bytes. */
		int64 TotalBytes = 0;
		/** The total number of cycles spent on the activity across all threads. */
		int64 CyclesSpent = 0;
	};

	FActivity Pull;
	FActivity Push;
};

/** Describes the type of storage to use for a given action */
enum EStorageType
{
	/** Store in the local cache backends, this can be called from any thread */
	Local = 0,
	/** Store in the persistent backends, this can only be called from the game thread due to limitations with ISourceControlModule. */
	Persistent
};

/** This is used as a wrapper around the various potential back end implementations. 
	The calling code shouldn't need to care about which back ends are actually in use. */
class COREUOBJECT_API FVirtualizationManager
{
public:
	using FRegistedFactories = TMap<FName, IVirtualizationBackendFactory*>;
	using FBackendArray = TArray<IVirtualizationBackend*>;

	/** Singleton access */
	static FVirtualizationManager& Get();

	FVirtualizationManager();
	~FVirtualizationManager();

	/** Poll to see if content virtualization is enabled or not. */
	bool IsEnabled() const;
	
	/** 
	 * Push a payload to the virtualization backends.
	 * 
	 * @param	Id			The identifier of the payload being pushed.
	 * @param	Payload		The payload itself in FCompressedBuffer form, it is 
	 *						assumed that if the buffer is to be compressed that
	 *						it will have been done by the caller.
	 * @param	StorageType	The type of storage to push the payload to, see EStorageType
	 *						for details.
	 * @return	True if at least one backend now contains the payload, otherwise false.
	 */
	bool PushData(const FPayloadId& Id, const FCompressedBuffer& Payload, EStorageType StorageType);

	/** 
	 * Pull a payload from the virtualization backends.
	 *
	 * @param	Id The identifier of the payload being pulled.
	 * @return	The payload in the form of a FCompressedBuffer. No decompression will
	 *			be applied to the payload, it is up to the caller if they want to 
	 *			retain the payload in compressed or uncompressed format.
	 *			If no backend contained the payload then an empty invalid FCompressedBuffer
	 *			will be returned.
	 */
	FCompressedBuffer PullData(const FPayloadId& Id);

	/** Access profiling info relating to payload activity. Stats will only be collected if ENABLE_COOK_STATS is enabled.*/
	FPayloadActivityInfo GetPayloadActivityInfo() const;

private:
	
	void ApplySettingsFromConfigFiles(const FConfigFile& PlatformEngineIni);
	void ApplySettingsFromCmdline();

	void ApplyDebugSettingsFromConfigFiles(const FConfigFile& PlatformEngineIni);

	void MountBackends();
	void ParseHierarchy(const TCHAR* GraphName, const TCHAR* HierarchyKey, const FRegistedFactories& FactoryLookupTable, FBackendArray& PushArray);
	bool CreateBackend(const TCHAR* GraphName, const FString& ConfigEntryName, const FRegistedFactories& FactoryLookupTable, FBackendArray& PushArray);

	void AddBackend(TUniquePtr<IVirtualizationBackend> Backend, FBackendArray& PushArray);

	void CachePayload(const FPayloadId& Id, const FCompressedBuffer& Payload, const IVirtualizationBackend* BackendSource);

	bool TryPushDataToBackend(IVirtualizationBackend& Backend, const FPayloadId& Id, const FCompressedBuffer& Payload);
	FCompressedBuffer PullDataFromBackend(IVirtualizationBackend& Backend, const FPayloadId& Id);

	/** Are payloads allowed to be virtualized. Defaults to true. */
	bool bEnablePayloadPushing;

	/** Should payloads be cached locally after being pulled from persistent storage? Defaults to true. */
	bool bEnableCacheAfterPull;

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

	/** All of the backends that were mounted during graph creation */
	TArray<TUniquePtr<IVirtualizationBackend>> AllBackends;

	/** Backends used for caching operations (must support push operations). */
	FBackendArray LocalCachableBackends; 

	/** Backends used for persistent storage operations (must support push operations). */
	FBackendArray PersistentStorageBackends; 

	/** 
	 * The hierarchy of backends to pull from, this is assumed to be ordered from fastest to slowest
	 * and can contain a mixture of local cacheable and persistent backends 
	 */
	FBackendArray PullEnabledBackends;
};

} // namespace UE::Virtualization
