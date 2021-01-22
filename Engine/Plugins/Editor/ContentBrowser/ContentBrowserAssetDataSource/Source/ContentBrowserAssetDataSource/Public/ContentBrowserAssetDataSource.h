// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ContentBrowserDataSource.h"
#include "IAssetRegistry.h"
#include "UObject/GCObject.h"
#include "Misc/BlacklistNames.h"
#include "ContentBrowserDataMenuContexts.h"
#include "ContentBrowserAssetDataPayload.h"
#include "ContentBrowserAssetDataSource.generated.h"

class IAssetTools;
class IAssetTypeActions;
class ICollectionManager;
class UFactory;
class UToolMenu;
class FAssetFolderContextMenu;
class FAssetFileContextMenu;
struct FCollectionNameType;

USTRUCT()
struct CONTENTBROWSERASSETDATASOURCE_API FContentBrowserCompiledAssetDataFilter
{
	GENERATED_BODY()

public:
	// Folder filtering
	bool bRunFolderQueryOnDemand = false;
	// On-demand filtering (always recursive on PathToScanOnDemand)
	bool bRecursivePackagePathsToInclude = false;
	bool bRecursivePackagePathsToExclude = false;
	FBlacklistPaths PackagePathsToInclude;
	FBlacklistPaths PackagePathsToExclude;
	FBlacklistPaths PathBlacklist;
	TSet<FName> ExcludedPackagePaths;
	TSet<FString> PathsToScanOnDemand;
	// Cached filtering
	TSet<FName> CachedSubPaths;

	// Asset filtering
	bool bFilterExcludesAllAssets = false;
	FARCompiledFilter InclusiveFilter;
	FARCompiledFilter ExclusiveFilter;

	// Legacy custom assets
	TArray<FAssetData> CustomSourceAssets;
};

UCLASS()
class CONTENTBROWSERASSETDATASOURCE_API UContentBrowserAssetDataSource : public UContentBrowserDataSource
{
	GENERATED_BODY()

public:
	void Initialize(const FName InMountRoot, const bool InAutoRegister = true);

	virtual void Shutdown() override;

	virtual void CompileFilter(const FName InPath, const FContentBrowserDataFilter& InFilter, FContentBrowserDataCompiledFilter& OutCompiledFilter) override;

	virtual void EnumerateItemsMatchingFilter(const FContentBrowserDataCompiledFilter& InFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback) override;

	virtual void EnumerateItemsAtPath(const FName InPath, const EContentBrowserItemTypeFilter InItemTypeFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback) override;

	virtual bool IsDiscoveringItems(FText* OutStatus = nullptr) override;

	virtual bool PrioritizeSearchPath(const FName InPath) override;

	virtual bool IsFolderVisibleIfHidingEmpty(const FName InPath) override;

	virtual bool CanCreateFolder(const FName InPath, FText* OutErrorMsg) override;

	virtual bool CreateFolder(const FName InPath, FContentBrowserItemDataTemporaryContext& OutPendingItem) override;

	virtual bool DoesItemPassFilter(const FContentBrowserItemData& InItem, const FContentBrowserDataCompiledFilter& InFilter) override;

	virtual bool GetItemAttribute(const FContentBrowserItemData& InItem, const bool InIncludeMetaData, const FName InAttributeKey, FContentBrowserItemDataAttributeValue& OutAttributeValue) override;

	virtual bool GetItemAttributes(const FContentBrowserItemData& InItem, const bool InIncludeMetaData, FContentBrowserItemDataAttributeValues& OutAttributeValues) override;

	virtual bool GetItemPhysicalPath(const FContentBrowserItemData& InItem, FString& OutDiskPath) override;

	virtual bool IsItemDirty(const FContentBrowserItemData& InItem) override;

	virtual bool CanEditItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg) override;

	virtual bool EditItem(const FContentBrowserItemData& InItem) override;

	virtual bool BulkEditItems(TArrayView<const FContentBrowserItemData> InItems) override;

	virtual bool CanPreviewItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg) override;

	virtual bool PreviewItem(const FContentBrowserItemData& InItem) override;

	virtual bool BulkPreviewItems(TArrayView<const FContentBrowserItemData> InItems) override;

	virtual bool CanDuplicateItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg) override;

	virtual bool DuplicateItem(const FContentBrowserItemData& InItem, FContentBrowserItemDataTemporaryContext& OutPendingItem) override;

	virtual bool BulkDuplicateItems(TArrayView<const FContentBrowserItemData> InItems, TArray<FContentBrowserItemData>& OutNewItems) override;

	virtual bool CanSaveItem(const FContentBrowserItemData& InItem, const EContentBrowserItemSaveFlags InSaveFlags, FText* OutErrorMsg) override;

	virtual bool SaveItem(const FContentBrowserItemData& InItem, const EContentBrowserItemSaveFlags InSaveFlags) override;

	virtual bool BulkSaveItems(TArrayView<const FContentBrowserItemData> InItems, const EContentBrowserItemSaveFlags InSaveFlags) override;

	virtual bool CanDeleteItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg) override;

	virtual bool DeleteItem(const FContentBrowserItemData& InItem) override;

	virtual bool BulkDeleteItems(TArrayView<const FContentBrowserItemData> InItems) override;

	virtual bool CanRenameItem(const FContentBrowserItemData& InItem, const FString* InNewName, FText* OutErrorMsg) override;

	virtual bool RenameItem(const FContentBrowserItemData& InItem, const FString& InNewName, FContentBrowserItemData& OutNewItem) override;

	virtual bool CanCopyItem(const FContentBrowserItemData& InItem, const FName InDestPath, FText* OutErrorMsg) override;

	virtual bool CopyItem(const FContentBrowserItemData& InItem, const FName InDestPath) override;

	virtual bool BulkCopyItems(TArrayView<const FContentBrowserItemData> InItems, const FName InDestPath) override;

	virtual bool CanMoveItem(const FContentBrowserItemData& InItem, const FName InDestPath, FText* OutErrorMsg) override;

	virtual bool MoveItem(const FContentBrowserItemData& InItem, const FName InDestPath) override;

	virtual bool BulkMoveItems(TArrayView<const FContentBrowserItemData> InItems, const FName InDestPath) override;

	virtual bool AppendItemReference(const FContentBrowserItemData& InItem, FString& InOutStr) override;

	virtual bool UpdateThumbnail(const FContentBrowserItemData& InItem, FAssetThumbnail& InThumbnail) override;

	virtual bool HandleDragEnterItem(const FContentBrowserItemData& InItem, const FDragDropEvent& InDragDropEvent) override;

	virtual bool HandleDragOverItem(const FContentBrowserItemData& InItem, const FDragDropEvent& InDragDropEvent) override;

	virtual bool HandleDragLeaveItem(const FContentBrowserItemData& InItem, const FDragDropEvent& InDragDropEvent) override;

	virtual bool HandleDragDropOnItem(const FContentBrowserItemData& InItem, const FDragDropEvent& InDragDropEvent) override;

	virtual bool TryGetCollectionId(const FContentBrowserItemData& InItem, FName& OutCollectionId) override;

	virtual bool Legacy_TryGetPackagePath(const FContentBrowserItemData& InItem, FName& OutPackagePath) override;

	virtual bool Legacy_TryGetAssetData(const FContentBrowserItemData& InItem, FAssetData& OutAssetData) override;

	virtual bool Legacy_TryConvertPackagePathToVirtualPath(const FName InPackagePath, FName& OutPath) override;

	virtual bool Legacy_TryConvertAssetDataToVirtualPath(const FAssetData& InAssetData, const bool InUseFolderPaths, FName& OutPath) override;

protected:
	virtual void EnumerateRootPaths(const FContentBrowserDataFilter& InFilter, TFunctionRef<void(FName)> InCallback) override;

private:
	bool IsKnownContentPath(const FName InPackagePath) const;

	bool IsRootContentPath(const FName InPackagePath) const;

	bool GetObjectPathsForCollections(TArrayView<const FCollectionNameType> InCollections, const bool bIncludeChildCollections, TArray<FName>& OutObjectPaths) const;

	FContentBrowserItemData CreateAssetFolderItem(const FName InFolderPath);

	FContentBrowserItemData CreateAssetFileItem(const FAssetData& InAssetData);

	TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> GetAssetFolderItemPayload(const FContentBrowserItemData& InItem) const;

	TSharedPtr<const FContentBrowserAssetFileItemDataPayload> GetAssetFileItemPayload(const FContentBrowserItemData& InItem) const;

	bool CanHandleDragDropEvent(const FContentBrowserItemData& InItem, const FDragDropEvent& InDragDropEvent) const;

	void OnAssetRegistryFileLoadProgress(const IAssetRegistry::FFileLoadProgressUpdateData& InProgressUpdateData);

	void OnAssetAdded(const FAssetData& InAssetData);

	void OnAssetRemoved(const FAssetData& InAssetData);

	void OnAssetRenamed(const FAssetData& InAssetData, const FString& InOldObjectPath);

	void OnAssetUpdated(const FAssetData& InAssetData);

	void OnAssetLoaded(UObject* InAsset);

	void OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InPropertyChangedEvent);

	void OnPathAdded(const FString& InPath);

	void OnPathRemoved(const FString& InPath);

	void OnPathPopulated(const FName InPath);

	void OnPathPopulated(const FStringView InPath);

	void OnAlwaysShowPath(const FString& InPath);

	void OnScanCompleted();

	void OnContentPathMounted(const FString& InAssetPath, const FString& InFileSystemPath);

	void OnContentPathDismounted(const FString& InAssetPath, const FString& InFileSystemPath);

	void PopulateAddNewContextMenu(UToolMenu* InMenu);

	void PopulateAssetFolderContextMenu(UToolMenu* InMenu);

	void PopulateAssetFileContextMenu(UToolMenu* InMenu);

	void PopulateDragDropContextMenu(UToolMenu* InMenu);

	void OnAdvancedCopyRequested(const TArray<FName>& InAdvancedCopyInputs, const FString& InDestinationPath);

	void OnImportAsset(const FName InPath);

	void OnNewAssetRequested(const FName InPath, TWeakObjectPtr<UClass> InFactoryClass, UContentBrowserDataMenuContext_AddNewMenu::FOnBeginItemCreation InOnBeginItemCreation);

	void OnBeginCreateAsset(const FName InDefaultAssetName, const FName InPackagePath, UClass* InAssetClass, UFactory* InFactory, UContentBrowserDataMenuContext_AddNewMenu::FOnBeginItemCreation InOnBeginItemCreation);

	bool OnValidateItemName(const FContentBrowserItemData& InItem, const FString& InProposedName, FText* OutErrorMsg);

	FContentBrowserItemData OnFinalizeCreateFolder(const FContentBrowserItemData& InItemData, const FString& InProposedName, FText* OutErrorMsg);

	FContentBrowserItemData OnFinalizeCreateAsset(const FContentBrowserItemData& InItemData, const FString& InProposedName, FText* OutErrorMsg);

	FContentBrowserItemData OnFinalizeDuplicateAsset(const FContentBrowserItemData& InItemData, const FString& InProposedName, FText* OutErrorMsg);

	IAssetRegistry* AssetRegistry;

	IAssetTools* AssetTools;

	ICollectionManager* CollectionManager;

	TSharedPtr<FAssetFolderContextMenu> AssetFolderContextMenu;

	TSharedPtr<FAssetFileContextMenu> AssetFileContextMenu;

	FText DiscoveryStatusText;

	/**
	 * The array of known root content paths that can hold assets.
	 * @note These paths include a trailing slash.
	 */
	TArray<FString> RootContentPaths;

	/**
	 * The set of folders that should always be visible, even if they contain no assets in the Content Browser view.
	 * This will include root content folders, and any folders that have been created directly (or indirectly) by a user action.
	 */
	TSet<FString> AlwaysVisibleAssetFolders;

	/**
	 * A cache of folders that contain no assets in the Content Browser view.
	 */
	TSet<FString> EmptyAssetFolders;
};
