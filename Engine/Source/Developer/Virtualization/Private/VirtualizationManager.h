// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compression/CompressedBuffer.h"
#include "HAL/CriticalSection.h"
#include "Logging/LogMacros.h"
#include "Templates/UniquePtr.h"

#include "Virtualization/VirtualizationSystem.h"

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

/**
 * Filtering
 * 
 * When pushing a payload it can be filtered based on the path of the package it belongs to. The filtering options 
 * are set up via the config files. 
 * Note that this only affects pushing a payload, if the filtering for a project is changed to exclude a package that
 * is already virtualized it will still be able to pull it's payloads as needed but will store them locally in the 
 * package the next time that it is saved.
 * @see ShouldVirtualizePackage for implementation details.
 * 
 * Basic Setup:
 * 
 * [Core.ContentVirtualization]
 * FilterEngineContent=True/False			When true any payload from a package under Engine/Content/.. will be excluded from virtualization
 * FilterEnginePluginContent=True/False		When true any payload from a package under Engine/Plugins/../Content/.. will be excluded from virtualization
 * 
 * PackagePath Setup:
 * 
 * The path given can either be to a directory or a specific package. It can be added to the config files for
 * a GameFeature (commonly used to exclude all content in that game feature from being virtualized) in addition 
 * to the project's config files.
 * Note that these paths will be stored in the ini files under the Saved directory. To remove a path make sure to 
 * use the - syntax to remove the entry from the array, rather than removing the line itself. Otherwise it will
 * persist until the saved config file has been reset.
 *
 * [/Script/Virtualization.VirtualizationFilterSettings]
 * +ExcludePackagePaths="/MountPoint/PathToExclude/"				Excludes any package found under '/MountPoint/PathToExclude/'
 * +ExcludePackagePaths="/MountPoint/PathTo/ThePackageToExclude"	Excludes the specific package '/MountPoint/PathTo/ThePackageToExclude'
 */

namespace UE::Virtualization
{
class IVirtualizationBackend;
class IVirtualizationBackendFactory;

/** This is used as a wrapper around the various potential back end implementations. 
	The calling code shouldn't need to care about which back ends are actually in use. */
class FVirtualizationManager final : public IVirtualizationSystem
{
public:
	using FRegistedFactories = TMap<FName, IVirtualizationBackendFactory*>;
	using FBackendArray = TArray<IVirtualizationBackend*>;

	FVirtualizationManager();
	virtual ~FVirtualizationManager();

private:
	/* IVirtualizationSystem implementation */

	virtual bool IsEnabled() const override;
	virtual bool IsPushingEnabled(EStorageType StorageType) const override;
	
	virtual bool PushData(const FIoHash& Id, const FCompressedBuffer& Payload, EStorageType StorageType, const FString& Context) override;
	virtual bool PushData(TArrayView<FPushRequest> Requests, EStorageType StorageType) override;

	virtual FCompressedBuffer PullData(const FIoHash& Id) override;

	virtual bool DoPayloadsExist(TArrayView<const FIoHash> Ids, EStorageType StorageType, TArray<FPayloadStatus>& OutStatuses) override;

	virtual FPayloadActivityInfo GetAccumualtedPayloadActivityInfo() const override;

	virtual void GetPayloadActivityInfo( GetPayloadActivityInfoFuncRef ) const override;

	virtual FOnNotification& GetNotificationEvent() override
	{
		return NotificationEvent;
	}
	
private:

	void ApplySettingsFromConfigFiles(const FConfigFile& PlatformEngineIni);
	void ApplySettingsFromCmdline();
	
	void ApplyDebugSettingsFromConfigFiles(const FConfigFile& PlatformEngineIni);
	void ApplyDebugSettingsFromFromCmdline();

	void MountBackends();
	void ParseHierarchy(const TCHAR* GraphName, const TCHAR* HierarchyKey, const FRegistedFactories& FactoryLookupTable, FBackendArray& PushArray);
	bool CreateBackend(const TCHAR* GraphName, const FString& ConfigEntryName, const FRegistedFactories& FactoryLookupTable, FBackendArray& PushArray);

	void AddBackend(TUniquePtr<IVirtualizationBackend> Backend, FBackendArray& PushArray);

	void CachePayload(const FIoHash& Id, const FCompressedBuffer& Payload, const IVirtualizationBackend* BackendSource);

	bool TryCacheDataToBackend(IVirtualizationBackend& Backend, const FIoHash& Id, const FCompressedBuffer& Payload);
	bool TryPushDataToBackend(IVirtualizationBackend& Backend, TArrayView<FPushRequest> Requests);

	FCompressedBuffer PullDataFromBackend(IVirtualizationBackend& Backend, const FIoHash& Id);

	/** 
	 * Determines if a package should be virtualized or not based on it's package path and the current 
	 * filtering set up for the project.
	 * 
	 * @param PackagePath	The path of the package to check. This can be empty which would indicate that
	 *						a payload is not owned by a specific package.
	 * @return				True if the package should be virtualized and false if the package path is 
	 *						excluded by the projects current filter set up.
	 */
	bool ShouldVirtualizePackage(const FPackagePath& PackagePath) const;
	bool ShouldVirtualizePackage(const FString& Context) const;
	
	/** Are payloads allowed to be virtualized. Defaults to true. */
	bool bEnablePayloadPushing;

	/** Should payloads be cached locally after being pulled from persistent storage? Defaults to true. */
	bool bEnableCacheAfterPull;

	/** The minimum length for a payload to be considered for virtualization. Defaults to 0 bytes. */
	int64 MinPayloadLength;

	/** The name of the backend graph to load from the config ini file that will describe the backend hierarchy */
	FString BackendGraphName;

	/** Should payloads in engine content packages before filtered out and never virtualized */
	bool bFilterEngineContent;
	
	/** Should payloads in engine plugin content packages before filtered out and never virtualized */
	bool bFilterEnginePluginContent;

	/** Debugging option: When enabled all public operations will be performed as single threaded. This is intended to aid debugging and not for production use.*/
	bool bForceSingleThreaded;
	
	/**
	 * Debugging option: When enabled we will immediately 'pull' each payload after it has been 'pushed' and compare it to the original payload source to make 
	 * sure that it can be pulled correctly.
	 * This is intended to aid debugging and not for production use.
	 */
	bool bValidateAfterPushOperation;

	/** Array of backend names that should have their pull operation disabled */
	TArray<FString> BackendsToDisablePulls;

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

	/** Our notification Event */
	FOnNotification NotificationEvent;
};

} // namespace UE::Virtualization
