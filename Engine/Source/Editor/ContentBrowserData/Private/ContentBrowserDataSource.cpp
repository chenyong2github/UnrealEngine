// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserDataSource.h"
#include "Features/IModularFeatures.h"

FName UContentBrowserDataSource::GetModularFeatureTypeName()
{
	static const FName ModularFeatureTypeName = "ContentBrowserDataSource";
	return ModularFeatureTypeName;
}

void UContentBrowserDataSource::RegisterDataSource()
{
	IModularFeatures::Get().RegisterModularFeature(GetModularFeatureTypeName(), this);
}

void UContentBrowserDataSource::UnregisterDataSource()
{
	IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureTypeName(), this);
}

void UContentBrowserDataSource::Initialize(const FName InMountRoot, const bool InAutoRegister)
{
	MountRoot = InMountRoot;

	// Explode the mount root into its hierarchy (eg, "/One/Two" becomes ["/", "/One", "/One/Two"])
	{
		static const FName Slash = "/";

		TCHAR MountRootStr[FName::StringBufferSize];
		const int32 MountRootLen = MountRoot.ToString(MountRootStr);
		checkf(MountRootLen > 0 && MountRootStr[0] == TEXT('/'), TEXT("Mount roots must not be empty and must start with a slash!"));

		MountRootHierarchy.Add(Slash);
		if (MountRootLen > 1)
		{
			for (TCHAR* PathEndPtr = FCString::Strchr(MountRootStr + 1, TEXT('/'));
				PathEndPtr;
				PathEndPtr = FCString::Strchr(PathEndPtr + 1, TEXT('/'))
				)
			{
				*PathEndPtr = 0;
				MountRootHierarchy.Add(MountRootStr);
				*PathEndPtr = TEXT('/');
			}
			MountRootHierarchy.Add(MountRoot);
		}
	}

	bIsInitialized = true;

	if (InAutoRegister)
	{
		RegisterDataSource();
	}
}

void UContentBrowserDataSource::Shutdown()
{
	UnregisterDataSource();

	bIsInitialized = false;
}

void UContentBrowserDataSource::BeginDestroy()
{
	Shutdown();

	Super::BeginDestroy();
}

void UContentBrowserDataSource::SetDataSink(IContentBrowserItemDataSink* InDataSink)
{
	DataSink = InDataSink;
}

bool UContentBrowserDataSource::IsInitialized() const
{
	return bIsInitialized;
}

void UContentBrowserDataSource::Tick(const float InDeltaTime)
{
}

FName UContentBrowserDataSource::GetVirtualMountRoot() const
{
	return MountRoot;
}

TArrayView<const FName> UContentBrowserDataSource::GetVirtualMountRootHierarchy() const
{
	return MakeArrayView(MountRootHierarchy);
}

bool UContentBrowserDataSource::IsVirtualPathUnderMountRoot(const FName InPath) const
{
	static const FName RootPath = "/";
	if (MountRoot == RootPath)
	{
		// If we're mounted at the virtual root then everything is under us
		return true;
	}

	TCHAR PathStr[FName::StringBufferSize];
	const int32 PathLen = InPath.ToString(PathStr);

	TCHAR MountRootStr[FName::StringBufferSize];
	int32 MountRootLen = MountRoot.ToString(MountRootStr);

	// If the path length is shorter than the mount root, then the path cannot be under the mount root
	if (PathLen < MountRootLen)
	{
		return false;
	}

	if (PathLen == MountRootLen)
	{
		// "Equals" comparison on the local string buffers
		return FCString::Strnicmp(PathStr, MountRootStr, MountRootLen) == 0;
	}

	// Ensure the mount root ends with a / to avoid matching "/Root" against "/Root2/MyFile"
	if (MountRootLen > 0 && MountRootStr[MountRootLen - 1] != TEXT('/'))
	{
		MountRootStr[MountRootLen++] = TEXT('/');
		MountRootStr[MountRootLen] = 0;
	}

	// "StartsWith" comparison on the local string buffers
	return FCString::Strnicmp(PathStr, MountRootStr, MountRootLen) == 0;
}

bool UContentBrowserDataSource::TryConvertVirtualPathToInternal(const FName InPath, FName& OutInternalPath)
{
	static const FName RootPath = "/";
	if (MountRoot == RootPath)
	{
		// If we're mounted at the virtual root then no re-mapping needs to happen
		OutInternalPath = InPath;
		return true;
	}

	TCHAR PathStr[FName::StringBufferSize];
	const int32 PathLen = InPath.ToString(PathStr);

	TCHAR MountRootStr[FName::StringBufferSize];
	const int32 MountRootLen = MountRoot.ToString(MountRootStr);

	// If the path length is shorter than the mount root, then the path cannot be under the mount root
	if (PathLen < MountRootLen)
	{
		return false;
	}

	// "StartsWith" comparison on the local string buffers
	// This doesn't add the slash to mount root as IsVirtualPathUnderMountRoot 
	// does because we will check that the remaining path starts with a slash
	if (FCString::Strnicmp(PathStr, MountRootStr, MountRootLen) != 0)
	{
		return false;
	}

	// If the mount root ended in a slash then we need to consider this as part of the internal path, 
	// as we wouldn't have allowed the duplicate slash in TryConvertInternalPathToVirtual
	int32 InternalPathStartIndex = MountRootLen;
	if (InternalPathStartIndex > 0 && MountRootStr[InternalPathStartIndex - 1] == TEXT('/'))
	{
		--InternalPathStartIndex;
	}

	const TCHAR* InternalPathStr = PathStr + InternalPathStartIndex;
	if (InternalPathStr[0] == 0)
	{
		// If the given path was the mount root itself, then we just return a slash as the internal path
		static const FName Slash = "/";
		OutInternalPath = Slash;
		return true;
	}
	
	if (InternalPathStr[0] != TEXT('/'))
	{
		return false;
	}

	OutInternalPath = InternalPathStr;
	return true;
}

bool UContentBrowserDataSource::TryConvertInternalPathToVirtual(const FName InInternalPath, FName& OutPath)
{
	static const FName RootPath = "/";
	if (MountRoot == RootPath)
	{
		// If we're mounted at the virtual root then no re-mapping needs to happen
		OutPath = InInternalPath;
		return true;
	}

	int32 PathLen = 0;
	TCHAR PathStr[FName::StringBufferSize] = {0};

	// Append the mount root
	PathLen += MountRoot.ToString(PathStr + PathLen, FName::StringBufferSize - PathLen);

	// If the mount root ended in a slash then remove this as appending the internal path would cause a duplicate slash
	// Note: This assumes that the internal path starts with a slash, which is stated in the contract of this function
	if (PathLen > 0 && PathStr[PathLen - 1] == TEXT('/'))
	{
		PathStr[--PathLen] = 0;
	}

	// Append the internal path
	PathLen += InInternalPath.ToString(PathStr + PathLen, FName::StringBufferSize - PathLen);

	OutPath = PathStr;
	return true;
}

void UContentBrowserDataSource::CompileFilter(const FName InPath, const FContentBrowserDataFilter& InFilter, FContentBrowserDataCompiledFilter& OutCompiledFilter)
{
}

void UContentBrowserDataSource::EnumerateItemsMatchingFilter(const FContentBrowserDataCompiledFilter& InFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback)
{
}

void UContentBrowserDataSource::EnumerateItemsAtPath(const FName InPath, const EContentBrowserItemTypeFilter InItemTypeFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback)
{
}

bool UContentBrowserDataSource::IsDiscoveringItems(FText* OutStatus)
{
	return false;
}

bool UContentBrowserDataSource::PrioritizeSearchPath(const FName InPath)
{
	return false;
}

bool UContentBrowserDataSource::IsFolderVisibleIfHidingEmpty(const FName InPath)
{
	return true;
}

bool UContentBrowserDataSource::CanCreateFolder(const FName InPath, FText* OutErrorMsg)
{
	return false;
}

bool UContentBrowserDataSource::CreateFolder(const FName InPath, FContentBrowserItemDataTemporaryContext& OutPendingItem)
{
	return false;
}

bool UContentBrowserDataSource::DoesItemPassFilter(const FContentBrowserItemData& InItem, const FContentBrowserDataCompiledFilter& InFilter)
{
	return false;
}

bool UContentBrowserDataSource::GetItemAttribute(const FContentBrowserItemData& InItem, const bool InIncludeMetaData, const FName InAttributeKey, FContentBrowserItemDataAttributeValue& OutAttributeValue)
{
	return false;
}

bool UContentBrowserDataSource::GetItemAttributes(const FContentBrowserItemData& InItem, const bool InIncludeMetaData, FContentBrowserItemDataAttributeValues& OutAttributeValues)
{
	return false;
}

bool UContentBrowserDataSource::GetItemPhysicalPath(const FContentBrowserItemData& InItem, FString& OutDiskPath)
{
	return false;
}

bool UContentBrowserDataSource::IsItemDirty(const FContentBrowserItemData& InItem)
{
	return false;
}

bool UContentBrowserDataSource::CanEditItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	return false;
}

bool UContentBrowserDataSource::EditItem(const FContentBrowserItemData& InItem)
{
	return false;
}

bool UContentBrowserDataSource::BulkEditItems(TArrayView<const FContentBrowserItemData> InItems)
{
	bool bSuccess = false;
	for (const FContentBrowserItemData& Item : InItems)
	{
		bSuccess |= EditItem(Item);
	}
	return bSuccess;
}

bool UContentBrowserDataSource::CanPreviewItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	return false;
}

bool UContentBrowserDataSource::PreviewItem(const FContentBrowserItemData& InItem)
{
	return false;
}

bool UContentBrowserDataSource::BulkPreviewItems(TArrayView<const FContentBrowserItemData> InItems)
{
	bool bSuccess = false;
	for (const FContentBrowserItemData& Item : InItems)
	{
		bSuccess |= PreviewItem(Item);
	}
	return bSuccess;
}

bool UContentBrowserDataSource::CanDuplicateItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	return false;
}

bool UContentBrowserDataSource::DuplicateItem(const FContentBrowserItemData& InItem, FContentBrowserItemDataTemporaryContext& OutPendingItem)
{
	return false;
}

bool UContentBrowserDataSource::BulkDuplicateItems(TArrayView<const FContentBrowserItemData> InItems, TArray<FContentBrowserItemData>& OutNewItems)
{
	return false;
}

bool UContentBrowserDataSource::CanSaveItem(const FContentBrowserItemData& InItem, const EContentBrowserItemSaveFlags InSaveFlags, FText* OutErrorMsg)
{
	return false;
}

bool UContentBrowserDataSource::SaveItem(const FContentBrowserItemData& InItem, const EContentBrowserItemSaveFlags InSaveFlags)
{
	return false;
}

bool UContentBrowserDataSource::BulkSaveItems(TArrayView<const FContentBrowserItemData> InItems, const EContentBrowserItemSaveFlags InSaveFlags)
{
	bool bSuccess = false;
	for (const FContentBrowserItemData& Item : InItems)
	{
		bSuccess |= SaveItem(Item, InSaveFlags);
	}
	return bSuccess;
}

bool UContentBrowserDataSource::CanDeleteItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	return false;
}

bool UContentBrowserDataSource::DeleteItem(const FContentBrowserItemData& InItem)
{
	return false;
}

bool UContentBrowserDataSource::BulkDeleteItems(TArrayView<const FContentBrowserItemData> InItems)
{
	bool bSuccess = false;
	for (const FContentBrowserItemData& Item : InItems)
	{
		bSuccess |= DeleteItem(Item);
	}
	return bSuccess;
}

bool UContentBrowserDataSource::CanRenameItem(const FContentBrowserItemData& InItem, const FString* InNewName, FText* OutErrorMsg)
{
	return false;
}

bool UContentBrowserDataSource::RenameItem(const FContentBrowserItemData& InItem, const FString& InNewName, FContentBrowserItemData& OutNewItem)
{
	return false;
}

bool UContentBrowserDataSource::CanCopyItem(const FContentBrowserItemData& InItem, const FName InDestPath, FText* OutErrorMsg)
{
	return false;
}

bool UContentBrowserDataSource::CopyItem(const FContentBrowserItemData& InItem, const FName InDestPath)
{
	return false;
}

bool UContentBrowserDataSource::BulkCopyItems(TArrayView<const FContentBrowserItemData> InItems, const FName InDestPath)
{
	bool bSuccess = false;
	for (const FContentBrowserItemData& Item : InItems)
	{
		bSuccess |= CopyItem(Item, InDestPath);
	}
	return bSuccess;
}

bool UContentBrowserDataSource::CanMoveItem(const FContentBrowserItemData& InItem, const FName InDestPath, FText* OutErrorMsg)
{
	return false;
}

bool UContentBrowserDataSource::MoveItem(const FContentBrowserItemData& InItem, const FName InDestPath)
{
	return false;
}

bool UContentBrowserDataSource::BulkMoveItems(TArrayView<const FContentBrowserItemData> InItems, const FName InDestPath)
{
	bool bSuccess = false;
	for (const FContentBrowserItemData& Item : InItems)
	{
		bSuccess |= MoveItem(Item, InDestPath);
	}
	return bSuccess;
}

bool UContentBrowserDataSource::AppendItemReference(const FContentBrowserItemData& InItem, FString& InOutStr)
{
	return false;
}

bool UContentBrowserDataSource::UpdateThumbnail(const FContentBrowserItemData& InItem, FAssetThumbnail& InThumbnail)
{
	return false;
}

TSharedPtr<FDragDropOperation> UContentBrowserDataSource::CreateCustomDragOperation(TArrayView<const FContentBrowserItemData> InItems)
{
	return nullptr;
}

bool UContentBrowserDataSource::HandleDragEnterItem(const FContentBrowserItemData& InItem, const FDragDropEvent& InDragDropEvent)
{
	return false;
}

bool UContentBrowserDataSource::HandleDragOverItem(const FContentBrowserItemData& InItem, const FDragDropEvent& InDragDropEvent)
{
	return false;
}

bool UContentBrowserDataSource::HandleDragLeaveItem(const FContentBrowserItemData& InItem, const FDragDropEvent& InDragDropEvent)
{
	return false;
}

bool UContentBrowserDataSource::HandleDragDropOnItem(const FContentBrowserItemData& InItem, const FDragDropEvent& InDragDropEvent)
{
	return false;
}

bool UContentBrowserDataSource::TryGetCollectionId(const FContentBrowserItemData& InItem, FName& OutCollectionId)
{
	return false;
}

bool UContentBrowserDataSource::Legacy_TryGetPackagePath(const FContentBrowserItemData& InItem, FName& OutPackagePath)
{
	return false;
}

bool UContentBrowserDataSource::Legacy_TryGetAssetData(const FContentBrowserItemData& InItem, FAssetData& OutAssetData)
{
	return false;
}

bool UContentBrowserDataSource::Legacy_TryConvertPackagePathToVirtualPath(const FName InPackagePath, FName& OutPath)
{
	return false;
}

bool UContentBrowserDataSource::Legacy_TryConvertAssetDataToVirtualPath(const FAssetData& InAssetData, const bool InUseFolderPaths, FName& OutPath)
{
	return false;
}

void UContentBrowserDataSource::QueueItemDataUpdate(FContentBrowserItemDataUpdate&& InUpdate)
{
	if (DataSink)
	{
		DataSink->QueueItemDataUpdate(MoveTemp(InUpdate));
	}
}

void UContentBrowserDataSource::NotifyItemDataRefreshed()
{
	if (DataSink)
	{
		DataSink->NotifyItemDataRefreshed();
	}
}
