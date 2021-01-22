// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserDataSource.h"
#include "Features/IModularFeatures.h"
#include "Interfaces/IPluginManager.h"
#include "ContentBrowserDataSubsystem.h"
#include "ContentBrowserItemData.h"
#include "IContentBrowserDataModule.h"
#include "Settings/ContentBrowserSettings.h"
#include "Misc/PackageName.h"

struct FVirtualPathConverterBase
{
	FString ClassesPrefix;
	FString AllFolderPrefix;

	TArray<FString> MountsToIgnore;
	TMap<FName, FName> VirtualToInternal;

	FVirtualPathConverterBase()
	{
		AllFolderPrefix = TEXT("/All");
		ClassesPrefix = TEXT("Classes_");

		MountsToIgnore = {
			TEXT("Game"),
			TEXT("Engine"),
			TEXT("Classes_Game"),
			TEXT("Classes_Engine")
		};
	}

	void ResetCache()
	{
		VirtualToInternal.Reset();
	}

	bool EndConvertingToVirtualPath(const FStringView InPath, FName& OutPath)
	{
		const TCHAR* InPathData = InPath.GetData();

		TStringBuilder<FName::StringBufferSize> OutPathStr;

		const UContentBrowserSettings* ContentBrowserSettings = GetDefault<UContentBrowserSettings>();
		if (ContentBrowserSettings->ShowAllFolder)
		{
			OutPathStr.Append(AllFolderPrefix);
		}

		if (ContentBrowserSettings->OrganizeFolders && InPath.Len() > 0)
		{
			FStringView MountPointStringView;

			int32 SecondForwardSlash = INDEX_NONE;
			if (FStringView(InPath.GetData() + 1, InPath.Len() - 1).FindChar(TEXT('/'), SecondForwardSlash))
			{
				MountPointStringView = FStringView(InPathData + 1, SecondForwardSlash);
			}
			else
			{
				MountPointStringView = FStringView(InPathData + 1, InPath.Len() - 1);
			}

			const bool bHasClassesPrefix = MountPointStringView.StartsWith(ClassesPrefix);
			if (bHasClassesPrefix)
			{
				MountPointStringView.RightInline(MountPointStringView.Len() - ClassesPrefix.Len());
			}

			if (!MountsToIgnore.Contains(MountPointStringView))
			{
				FString MountPointName = FString(MountPointStringView.Len(), MountPointStringView.GetData());
				TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(MountPointName);
				if (Plugin.IsValid())
				{
					if (Plugin->GetLoadedFrom() == EPluginLoadedFrom::Engine)
					{
						OutPathStr.Append(TEXT("/Engine Plugins"));
					}
					else
					{
						OutPathStr.Append(TEXT("/Plugins"));
					}

					const FPluginDescriptor& PluginDescriptor = Plugin->GetDescriptor();
					if (!PluginDescriptor.EditorCustomVirtualPath.IsEmpty())
					{
						int32 NumChars = PluginDescriptor.EditorCustomVirtualPath.Len();
						if (PluginDescriptor.EditorCustomVirtualPath.EndsWith(TEXT("/")))
						{
							--NumChars;
						}

						if (NumChars > 0)
						{
							if (!PluginDescriptor.EditorCustomVirtualPath.StartsWith(TEXT("/")))
							{
								OutPathStr.Append(TEXT("/"));
							}

							OutPathStr.Append(*PluginDescriptor.EditorCustomVirtualPath, NumChars);
						}
					}
				}
				else
				{
					OutPathStr.Append(TEXT("/Other"));
				}
			}
		}

		OutPathStr.Append(InPath.GetData(), InPath.Len());
		OutPath = *OutPathStr;
		VirtualToInternal.Add(OutPath, FName(InPath));
		return true;
	}

	bool EndConvertingToVirtualPath(const FName InPath, FName& OutPath)
	{
		TStringBuilder<FName::StringBufferSize> PathStr;
		InPath.ToString(PathStr);
		return EndConvertingToVirtualPath(FStringView(PathStr.GetData(), PathStr.Len()), OutPath);
	}

	bool BeginConvertingFromVirtualPath(const FStringView InPath, FName& OutPath)
	{
		if (const FName* Found = VirtualToInternal.Find(FName(InPath)))
		{
			OutPath = *Found;
			return true;
		}
		else 
		{
			const UContentBrowserSettings* ContentBrowserSettings = GetDefault<UContentBrowserSettings>();
			if (ContentBrowserSettings->ShowAllFolder)
			{
				if (InPath.StartsWith(AllFolderPrefix))
				{
					return false;
				}
			}
			else if (ContentBrowserSettings->OrganizeFolders)
			{
				// Confirm it is a valid mount point
				FString MountPointName;
				const TCHAR* InPathData = InPath.GetData();
				if (const TCHAR* FoundChar = FCString::Strchr(InPathData + 1, TEXT('/')))
				{
					MountPointName = FString((int32)(FoundChar - InPathData), InPathData);
				}
				else
				{
					MountPointName = FString(InPath.Len(), InPathData);
				}

				if (MountPointName.StartsWith(TEXT("/Classes_")))
				{
					MountPointName.RemoveAt(1, MountPointName.Len() - 8, false);
				}

				MountPointName.Append(TEXT("/"));

				if (!FPackageName::MountPointExists(MountPointName))
				{
					return false;
				}
			}
		}

		OutPath = FName(InPath);
		return true;
	}

	bool BeginConvertingFromVirtualPath(const FName InPath, FName& OutPath)
	{
		TStringBuilder<FName::StringBufferSize> PathStr;
		InPath.ToString(PathStr);
		return BeginConvertingFromVirtualPath(FStringView(PathStr.GetData(), PathStr.Len()), OutPath);
	}
};

FVirtualPathConverterBase VirtualPathConverterBase;

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

	FName AdjustedPath;
	VirtualPathConverterBase.BeginConvertingFromVirtualPath(InPath, AdjustedPath);

	TCHAR PathStr[FName::StringBufferSize];
	const int32 PathLen = AdjustedPath.ToString(PathStr);

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

	// Special case "/" cannot be converted or remapped
	if (InPath == RootPath)
	{
		OutInternalPath = InPath;
		return true;
	}

	if (MountRoot == RootPath)
	{
		// If we're mounted at the virtual root then no re-mapping needs to happen
		return VirtualPathConverterBase.BeginConvertingFromVirtualPath(InPath, OutInternalPath);
	}

	FName AdjustedPath;
	VirtualPathConverterBase.BeginConvertingFromVirtualPath(InPath, AdjustedPath);

	TCHAR PathStr[FName::StringBufferSize];
	const int32 PathLen = AdjustedPath.ToString(PathStr);

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

	// Special case "/" cannot be converted or remapped
	if (InInternalPath == RootPath)
	{
		OutPath = InInternalPath;
		return true;
	}

	if (MountRoot == RootPath)
	{
		VirtualPathConverterBase.EndConvertingToVirtualPath(InInternalPath, OutPath);
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

	VirtualPathConverterBase.EndConvertingToVirtualPath(FStringView(PathStr, PathLen), OutPath);
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

void UContentBrowserDataSource::EnumerateRootPaths(const FContentBrowserDataFilter& InFilter, TFunctionRef<void(FName)> InCallback)
{
	checkf(false, TEXT("Must implement EnumerateRootPaths"));
}

void UContentBrowserDataSource::ExpandVirtualPath(const FName InPath, const FContentBrowserDataFilter& InFilter, FName& OutInternalPath, TSet<FName>& OutInternalPaths, TMap<FName, TArray<FName>>& OutVirtualPaths)
{
	static const FName RootPath = "/";
	if (InPath == RootPath)
	{
		OutInternalPath = InPath;
		OutInternalPaths.Add(OutInternalPath);
		return;
	}

	if (TryConvertVirtualPathToInternal(InPath, OutInternalPath))
	{
		OutInternalPaths.Add(OutInternalPath);
		return;
	}

	TStringBuilder<FName::StringBufferSize> PathString;
	InPath.ToString(PathString);
	PathString.Append(TEXT('/'));

	EnumerateRootPaths(InFilter, [this, &InFilter, &PathString, &OutInternalPaths, &OutVirtualPaths](const FName InternalRootPath)
	{
		FName VirtualRootPath;
		if (!TryConvertInternalPathToVirtual(InternalRootPath, VirtualRootPath))
		{
			return;
		}

		TStringBuilder<FName::StringBufferSize> VirtualRootPathString;
		VirtualRootPath.ToString(VirtualRootPathString);

		const FStringView VirtualRootPathStringView = VirtualRootPathString;
		if (!VirtualRootPathStringView.StartsWith(PathString, ESearchCase::IgnoreCase))
		{
			return;
		}

		if (InFilter.bRecursivePaths)
		{
			OutInternalPaths.Add(InternalRootPath);
			return;
		}

		// Add immediate subfolder virtual or otherwise
		const FStringView RightSide(VirtualRootPathStringView.GetData() + PathString.Len(), VirtualRootPathStringView.Len() - PathString.Len());
		int32 FoundIndex;
		if (RightSide.FindChar(TEXT('/'), FoundIndex))
		{
			const FName VirtualSubPath = FName(PathString.Len() + FoundIndex, VirtualRootPathStringView.GetData());
			TArray<FName>& InternalSubPaths = OutVirtualPaths.FindOrAdd(VirtualSubPath);
			InternalSubPaths.Add(InternalRootPath);
		}
		else
		{
			TArray<FName>& InternalSubPaths = OutVirtualPaths.FindOrAdd(VirtualRootPath);
			InternalSubPaths.Add(InternalRootPath);
		}
	});
}

