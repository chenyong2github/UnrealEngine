// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ContentBrowserDataSource.h"
#include "ContentBrowserDataMenuContexts.h"
#include "ContentBrowserClassDataPayload.h"
#include "ContentBrowserClassDataSource.generated.h"

class IAssetTypeActions;
class ICollectionManager;
class UToolMenu;
class FNativeClassHierarchy;
struct FCollectionNameType;

USTRUCT()
struct CONTENTBROWSERCLASSDATASOURCE_API FContentBrowserCompiledClassDataFilter
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TSet<UClass*> ValidClasses;

	UPROPERTY()
	TSet<FName> ValidFolders;
};

UCLASS()
class CONTENTBROWSERCLASSDATASOURCE_API UContentBrowserClassDataSource : public UContentBrowserDataSource
{
	GENERATED_BODY()

public:
	void Initialize(const FName InMountRoot, const bool InAutoRegister = true);

	virtual void Shutdown() override;

	virtual void CompileFilter(const FName InPath, const FContentBrowserDataFilter& InFilter, FContentBrowserDataCompiledFilter& OutCompiledFilter) override;

	virtual void EnumerateItemsMatchingFilter(const FContentBrowserDataCompiledFilter& InFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback) override;

	virtual void EnumerateItemsAtPath(const FName InPath, const EContentBrowserItemTypeFilter InItemTypeFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback) override;

	virtual bool IsFolderVisibleIfHidingEmpty(const FName InPath) override;

	virtual bool DoesItemPassFilter(const FContentBrowserItemData& InItem, const FContentBrowserDataCompiledFilter& InFilter) override;

	virtual bool GetItemAttribute(const FContentBrowserItemData& InItem, const bool InIncludeMetaData, const FName InAttributeKey, FContentBrowserItemDataAttributeValue& OutAttributeValue) override;

	virtual bool GetItemAttributes(const FContentBrowserItemData& InItem, const bool InIncludeMetaData, FContentBrowserItemDataAttributeValues& OutAttributeValues) override;

	virtual bool GetItemPhysicalPath(const FContentBrowserItemData& InItem, FString& OutDiskPath) override;

	virtual bool CanEditItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg) override;

	virtual bool EditItem(const FContentBrowserItemData& InItem) override;

	virtual bool BulkEditItems(TArrayView<const FContentBrowserItemData> InItems) override;

	virtual bool AppendItemReference(const FContentBrowserItemData& InItem, FString& InOutStr) override;

	virtual bool UpdateThumbnail(const FContentBrowserItemData& InItem, FAssetThumbnail& InThumbnail) override;

	virtual bool TryGetCollectionId(const FContentBrowserItemData& InItem, FName& OutCollectionId) override;

	virtual bool Legacy_TryGetPackagePath(const FContentBrowserItemData& InItem, FName& OutPackagePath) override;

	virtual bool Legacy_TryGetAssetData(const FContentBrowserItemData& InItem, FAssetData& OutAssetData) override;

	virtual bool Legacy_TryConvertPackagePathToVirtualPath(const FName InPackagePath, FName& OutPath) override;

	virtual bool Legacy_TryConvertAssetDataToVirtualPath(const FAssetData& InAssetData, const bool InUseFolderPaths, FName& OutPath) override;

protected:
	virtual void EnumerateRootPaths(const FContentBrowserDataFilter& InFilter, TFunctionRef<void(FName)> InCallback) override;

private:
	bool IsKnownClassPath(const FName InPackagePath) const;

	bool GetClassPathsForCollections(TArrayView<const FCollectionNameType> InCollections, const bool bIncludeChildCollections, TArray<FName>& OutClassPaths);

	FContentBrowserItemData CreateClassFolderItem(const FName InFolderPath);

	FContentBrowserItemData CreateClassFileItem(UClass* InClass);

	TSharedPtr<const FContentBrowserClassFolderItemDataPayload> GetClassFolderItemPayload(const FContentBrowserItemData& InItem) const;

	TSharedPtr<const FContentBrowserClassFileItemDataPayload> GetClassFileItemPayload(const FContentBrowserItemData& InItem) const;

	void OnNewClassRequested(const FName InSelectedPath);

	void PopulateAddNewContextMenu(UToolMenu* InMenu);

	void ConditionalCreateNativeClassHierarchy();

	TSharedPtr<FNativeClassHierarchy> NativeClassHierarchy;

	TSharedPtr<IAssetTypeActions> ClassTypeActions;

	ICollectionManager* CollectionManager;
};
