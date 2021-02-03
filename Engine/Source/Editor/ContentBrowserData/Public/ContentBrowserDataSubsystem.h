// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Containers/SortedMap.h"
#include "ContentBrowserItem.h"
#include "ContentBrowserDataFilter.h"
#include "ContentBrowserDataSubsystem.generated.h"

/** Called for incremental item data updates from data sources that can provide delta-updates */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnContentBrowserItemDataUpdated, TArrayView<const FContentBrowserItemDataUpdate>);

/** Called for wholesale item data updates from data sources that can't provide delta-updates, or when the set of active data sources is modified */
DECLARE_MULTICAST_DELEGATE(FOnContentBrowserItemDataRefreshed);

/** Called when all active data sources have completed their initial content discovery scan. May be called multiple times if new data sources are registered after the current set of active data sources have completed their initial scan */
DECLARE_MULTICAST_DELEGATE(FOnContentBrowserItemDataDiscoveryComplete);

/** Internal - Filter data used to inject dummy items for the path down to the mount root of each data source */
USTRUCT()
struct CONTENTBROWSERDATA_API FContentBrowserCompiledSubsystemFilter
{
	GENERATED_BODY()
	
public:
	TArray<FName> MountRootsToEnumerate;
};

/** Internal - Filter data used to inject dummy items */
USTRUCT()
struct CONTENTBROWSERDATA_API FContentBrowserCompiledVirtualFolderFilter
{
	GENERATED_BODY()

public:
	TMap<FName, FContentBrowserItemData> CachedSubPaths;
};

/**
 * Subsystem that provides access to Content Browser data.
 * This type deals with the composition of multiple data sources, which provide information about the folders and files available in the Content Browser.
 */
UCLASS(config=Editor)
class CONTENTBROWSERDATA_API UContentBrowserDataSubsystem : public UEditorSubsystem, public IContentBrowserItemDataSink
{
	GENERATED_BODY()

public:
	//~ UEditorSubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Attempt to activate the named data source.
	 * @return True if the data source was available and not already active, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="ContentBrowser")
	bool ActivateDataSource(const FName Name);

	/**
	 * Attempt to deactivate the named data source.
	 * @return True if the data source was available and active, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="ContentBrowser")
	bool DeactivateDataSource(const FName Name);

	/**
	 * Activate all available data sources.
	 */
	UFUNCTION(BlueprintCallable, Category="ContentBrowser")
	void ActivateAllDataSources();

	/**
	 * Deactivate all active data sources.
	 */
	UFUNCTION(BlueprintCallable, Category="ContentBrowser")
	void DeactivateAllDataSources();

	/**
	 * Get the list of current available data sources.
	 */
	UFUNCTION(BlueprintCallable, Category="ContentBrowser")
	TArray<FName> GetAvailableDataSources() const;

	/**
	 * Get the list of current active data sources.
	 */
	UFUNCTION(BlueprintCallable, Category="ContentBrowser")
	TArray<FName> GetActiveDataSources() const;

	/**
	 * Delegate called for incremental item data updates from data sources that can provide delta-updates.
	 */
	FOnContentBrowserItemDataUpdated& OnItemDataUpdated();

	/**
	 * Delegate called for wholesale item data updates from data sources that can't provide delta-updates, or when the set of active data sources is modified.
	 */
	FOnContentBrowserItemDataRefreshed& OnItemDataRefreshed();

	/**
	 * Delegate called when all active data sources have completed their initial content discovery scan.
	 * @note May be called multiple times if new data sources are registered after the current set of active data sources have completed their initial scan.
	 */
	FOnContentBrowserItemDataDiscoveryComplete& OnItemDataDiscoveryComplete();

	/**
	 * Take a raw data filter and convert it into a compiled version that could be re-used for multiple queries using the same data (typically this is only useful for post-filtering multiple items).
	 * @note The compiled filter is only valid until the data source changes, so only keep it for a short time (typically within a function call, or 1-frame).
	 */
	void CompileFilter(const FName InPath, const FContentBrowserDataFilter& InFilter, FContentBrowserDataCompiledFilter& OutCompiledFilter) const;

	/**
	 * Enumerate the items (folders and/or files) that match a previously compiled filter.
	 */
	void EnumerateItemsMatchingFilter(const FContentBrowserDataCompiledFilter& InFilter, TFunctionRef<bool(FContentBrowserItem&&)> InCallback) const;
	void EnumerateItemsMatchingFilter(const FContentBrowserDataCompiledFilter& InFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback) const;

	/**
	 * Enumerate the items (folders and/or files) that exist under the given virtual path.
	 */
	void EnumerateItemsUnderPath(const FName InPath, const FContentBrowserDataFilter& InFilter, TFunctionRef<bool(FContentBrowserItem&&)> InCallback) const;
	void EnumerateItemsUnderPath(const FName InPath, const FContentBrowserDataFilter& InFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback) const;

	/**
	 * Get the items (folders and/or files) that exist under the given virtual path.
	 */
	UFUNCTION(BlueprintCallable, Category="ContentBrowser")
	TArray<FContentBrowserItem> GetItemsUnderPath(const FName InPath, const FContentBrowserDataFilter& InFilter) const;

	/**
	 * Enumerate the items (folders and/or files) that exist at the given virtual path.
	 * @note Multiple items may have the same virtual path if they are different types, or come from different data sources.
	 */
	void EnumerateItemsAtPath(const FName InPath, const EContentBrowserItemTypeFilter InItemTypeFilter, TFunctionRef<bool(FContentBrowserItem&&)> InCallback) const;
	void EnumerateItemsAtPath(const FName InPath, const EContentBrowserItemTypeFilter InItemTypeFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback) const;

	/**
	 * Get the items (folders and/or files) that exist at the given virtual path.
	 * @note Multiple items may have the same virtual path if they are different types, or come from different data sources.
	 */
	UFUNCTION(BlueprintCallable, Category="ContentBrowser")
	TArray<FContentBrowserItem> GetItemsAtPath(const FName InPath, const EContentBrowserItemTypeFilter InItemTypeFilter) const;

	/**
	 * Get the first item (folder and/or file) that exists at the given virtual path.
	 */
	UFUNCTION(BlueprintCallable, Category="ContentBrowser")
	FContentBrowserItem GetItemAtPath(const FName InPath, const EContentBrowserItemTypeFilter InItemTypeFilter) const;

	/**
	 * Query whether any data sources are currently discovering content, and retrieve optional status messages that can be shown in the UI.
	 */
	bool IsDiscoveringItems(TArray<FText>* OutStatus = nullptr) const;

	/**
	 * If possible, attempt to prioritize content discovery for the given virtual path.
	 */
	bool PrioritizeSearchPath(const FName InPath);

	/**
	 * Query whether the given virtual folder should be visible if the UI is asking to hide empty content folders.
	 */
	bool IsFolderVisibleIfHidingEmpty(const FName InPath) const;

	/*
	 * Query whether a folder can be created at the given virtual path, optionally providing error information if it cannot.
	 *
	 * @param InPath The virtual path of the folder that is being queried.
	 * @param OutErrorMessage Optional error message to fill on failure.
	 *
	 * @return True if the folder can be created, false otherwise.
	 */
	bool CanCreateFolder(const FName InPath, FText* OutErrorMsg) const;

	/*
	 * Attempt to begin the process of asynchronously creating a folder at the given virtual path, returning a temporary item that can be finalized or canceled by the user.
	 *
	 * @param InPath The initial virtual path of the folder that is being created.
	 *
	 * @return The pending folder item to create (test for validity).
	 */
	FContentBrowserItemTemporaryContext CreateFolder(const FName InPath) const;

	/**
	 * Attempt to convert the given package path to virtual paths associated with the active data sources (callback will be called for each successful conversion).
	 * @note This exists to allow the Content Browser to interface with public APIs that only operate on package paths and should ideally be avoided for new code.
	 * @note This function only adjusts the path to something that could represent a virtualized item within this data source, but it doesn't guarantee that an item actually exists at that path.
	 */
	void Legacy_TryConvertPackagePathToVirtualPaths(const FName InPackagePath, TFunctionRef<bool(FName)> InCallback);

	/**
	 * Attempt to convert the given asset data to a virtual paths associated with the active data sources (callback will be called for each successful conversion).
	 * @note This exists to allow the Content Browser to interface with public APIs that only operate on asset data and should ideally be avoided for new code.
	 * @note This function only adjusts the path to something that could represent a virtualized item within this data source, but it doesn't guarantee that an item actually exists at that path.
	 */
	void Legacy_TryConvertAssetDataToVirtualPaths(const FAssetData& InAssetData, const bool InUseFolderPaths, TFunctionRef<bool(FName)> InCallback);

private:
	using FNameToDataSourceMap = TSortedMap<FName, UContentBrowserDataSource*, FDefaultAllocator, FNameFastLess>;

	/**
	 * Called to handle a data source modular feature being registered.
	 * @note Will activate the data source if it is in the EnabledDataSources array.
	 */
	void HandleDataSourceRegistered(const FName& Type, class IModularFeature* Feature);
	
	/**
	 * Called to handle a data source modular feature being unregistered.
	 * @note Will deactivate the data source if it is in the ActiveDataSources map.
	 */
	void HandleDataSourceUnregistered(const FName& Type, class IModularFeature* Feature);

	/**
	 * Tick this subsystem.
	 * @note Called once every 0.1 seconds.
	 */
	void Tick(const float InDeltaTime);

	//~ IContentBrowserItemDataSink interface
	virtual void QueueItemDataUpdate(FContentBrowserItemDataUpdate&& InUpdate) override;
	virtual void NotifyItemDataRefreshed() override;

	/**
	 * Handle for the Tick callback.
	 */
	FDelegateHandle TickHandle;

	/**
	 * Map of data sources that are currently active.
	 */
	FNameToDataSourceMap ActiveDataSources;

	/**
	 * Map of data sources that are currently available.
	 */
	FNameToDataSourceMap AvailableDataSources;

	/**
	 * Set of data sources that are currently running content discovery.
	 * ItemDataDiscoveryCompleteDelegate will be called each time this set becomes empty.
	 */
	TSet<FName> ActiveDataSourcesDiscoveringContent;

	/**
	 * Array of data source names that should be activated when available.
	 */
	UPROPERTY(config)
	TArray<FName> EnabledDataSources;

	/**
	 * Queue of incremental item data updates.
	 * These will be passed to ItemDataUpdatedDelegate on the end of Tick.
	 */
	TArray<FContentBrowserItemDataUpdate> PendingUpdates;

	/**
	 * True if an item data refresh notification is pending.
	 */
	bool bPendingItemDataRefreshedNotification = false;

	/**
	 * Delegate called for incremental item data updates from data sources that can provide delta-updates.
	 */
	FOnContentBrowserItemDataUpdated ItemDataUpdatedDelegate;

	/**
	 * Delegate called for wholesale item data updates from data sources that can't provide delta-updates, or when the set of active data sources is modified.
	 */
	FOnContentBrowserItemDataRefreshed ItemDataRefreshedDelegate;

	/**
	 * Delegate called when all active data sources have completed their initial content discovery scan.
	 * @note May be called multiple times if new data sources are registered after the current set of active data sources have completed their initial scan.
	 */
	FOnContentBrowserItemDataDiscoveryComplete ItemDataDiscoveryCompleteDelegate;
};
