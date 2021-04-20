// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserDataSubsystem.h"
#include "ContentBrowserDataSource.h"
#include "Containers/Ticker.h"
#include "Misc/PackageName.h"
#include "Features/IModularFeatures.h"
#include "Stats/Stats.h"
#include "UObject/UObjectThreadContext.h"
#include "Framework/Application/SlateApplication.h"

void UContentBrowserDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();

	{
		const FName DataSourceFeatureName = UContentBrowserDataSource::GetModularFeatureTypeName();

		const int32 AvailableDataSourcesCount = ModularFeatures.GetModularFeatureImplementationCount(DataSourceFeatureName);
		for (int32 AvailableDataSourcesIndex = 0; AvailableDataSourcesIndex < AvailableDataSourcesCount; ++AvailableDataSourcesIndex)
		{
			HandleDataSourceRegistered(DataSourceFeatureName, ModularFeatures.GetModularFeatureImplementation(DataSourceFeatureName, AvailableDataSourcesIndex));
		}
	}

	ModularFeatures.OnModularFeatureRegistered().AddUObject(this, &UContentBrowserDataSubsystem::HandleDataSourceRegistered);
	ModularFeatures.OnModularFeatureUnregistered().AddUObject(this, &UContentBrowserDataSubsystem::HandleDataSourceUnregistered);

	// Tick during normal operation
	TickHandle = FTicker::GetCoreTicker().AddTicker(TEXT("ContentBrowserData"), 0.1f, [this](const float InDeltaTime)
	{
		Tick(InDeltaTime);
		return true;
	});

	// Tick during modal dialog operation
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetOnModalLoopTickEvent().AddUObject(this, &UContentBrowserDataSubsystem::Tick);
	}
}

void UContentBrowserDataSubsystem::Deinitialize()
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	ModularFeatures.OnModularFeatureRegistered().RemoveAll(this);
	ModularFeatures.OnModularFeatureUnregistered().RemoveAll(this);

	ActiveDataSources.Reset();
	AvailableDataSources.Reset();
	ActiveDataSourcesDiscoveringContent.Reset();

	if (TickHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetOnModalLoopTickEvent().RemoveAll(this);
	}
}

bool UContentBrowserDataSubsystem::ActivateDataSource(const FName Name)
{
	EnabledDataSources.AddUnique(Name);

	if (!ActiveDataSources.Contains(Name))
	{
		if (UContentBrowserDataSource* DataSource = AvailableDataSources.FindRef(Name))
		{
			DataSource->SetDataSink(this);
			ActiveDataSources.Add(Name, DataSource);
			ActiveDataSourcesDiscoveringContent.Add(Name);
			NotifyItemDataRefreshed();
			return true;
		}
		else
		{
			// TODO: Log warning
		}
	}

	return false;
}

bool UContentBrowserDataSubsystem::DeactivateDataSource(const FName Name)
{
	EnabledDataSources.Remove(Name);

	if (UContentBrowserDataSource* DataSource = ActiveDataSources.FindRef(Name))
	{
		DataSource->SetDataSink(nullptr);
		ActiveDataSources.Remove(Name);
		ActiveDataSourcesDiscoveringContent.Remove(Name);
		NotifyItemDataRefreshed();
		return true;
	}

	return false;
}

void UContentBrowserDataSubsystem::ActivateAllDataSources()
{
	if (ActiveDataSources.Num() == AvailableDataSources.Num())
	{
		// Everything is already active - nothing to do
		return;
	}

	ActiveDataSources = AvailableDataSources;
	for (const auto& ActiveDataSourcePair : ActiveDataSources)
	{
		ActiveDataSourcePair.Value->SetDataSink(this);
		ActiveDataSourcesDiscoveringContent.Add(ActiveDataSourcePair.Key);

		// Merge this array as it may contain sources that we've not yet discovered, so can't activate yet
		EnabledDataSources.AddUnique(ActiveDataSourcePair.Key);
	}
	NotifyItemDataRefreshed();
}

void UContentBrowserDataSubsystem::DeactivateAllDataSources()
{
	if (ActiveDataSources.Num() == 0)
	{
		// Everything is already deactivated - nothing to do
		return;
	}

	for (const auto& ActiveDataSourcePair : ActiveDataSources)
	{
		ActiveDataSourcePair.Value->SetDataSink(nullptr);
	}
	ActiveDataSources.Reset();
	EnabledDataSources.Reset();
	ActiveDataSourcesDiscoveringContent.Reset();
	NotifyItemDataRefreshed();
}

TArray<FName> UContentBrowserDataSubsystem::GetAvailableDataSources() const
{
	TArray<FName> AvailableDataSourceNames;
	AvailableDataSources.GenerateKeyArray(AvailableDataSourceNames);
	return AvailableDataSourceNames;
}

TArray<FName> UContentBrowserDataSubsystem::GetActiveDataSources() const
{
	TArray<FName> ActiveDataSourceNames;
	ActiveDataSources.GenerateKeyArray(ActiveDataSourceNames);
	return ActiveDataSourceNames;
}

FOnContentBrowserItemDataUpdated& UContentBrowserDataSubsystem::OnItemDataUpdated()
{
	return ItemDataUpdatedDelegate;
}

FOnContentBrowserItemDataRefreshed& UContentBrowserDataSubsystem::OnItemDataRefreshed()
{
	return ItemDataRefreshedDelegate;
}

FOnContentBrowserItemDataDiscoveryComplete& UContentBrowserDataSubsystem::OnItemDataDiscoveryComplete()
{
	return ItemDataDiscoveryCompleteDelegate;
}

void UContentBrowserDataSubsystem::CompileFilter(const FName InPath, const FContentBrowserDataFilter& InFilter, FContentBrowserDataCompiledFilter& OutCompiledFilter) const
{
	OutCompiledFilter.ItemTypeFilter = InFilter.ItemTypeFilter;
	OutCompiledFilter.ItemCategoryFilter = InFilter.ItemCategoryFilter;
	OutCompiledFilter.ItemAttributeFilter = InFilter.ItemAttributeFilter;

	for (const auto& ActiveDataSourcePair : ActiveDataSources)
	{
		UContentBrowserDataSource* DataSource = ActiveDataSourcePair.Value;
		if (DataSource->IsVirtualPathUnderMountRoot(InPath))
		{
			// The requested path is managed by this data source, so compile the filter for it
			DataSource->CompileFilter(InPath, InFilter, OutCompiledFilter);
		}
		else
		{
			bool bEmitCallback = false;

			// The requested path is not managed by this data source, but we may still need to report part of its mount root as a sub-folder
			TArrayView<const FName> MountRootHierarchy = DataSource->GetVirtualMountRootHierarchy();
			for (const FName& MountRootPart : MountRootHierarchy)
			{
				if (MountRootPart == InPath)
				{
					// Emit the callback for the *next* part of the path
					bEmitCallback = true;
					continue;
				}

				if (bEmitCallback)
				{
					if (EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFolders))
					{
						FContentBrowserDataFilterList& FilterList = OutCompiledFilter.CompiledFilters.FindOrAdd(DataSource);
						FContentBrowserCompiledSubsystemFilter& SubsystemFilter = FilterList.FindOrAddFilter<FContentBrowserCompiledSubsystemFilter>();
						SubsystemFilter.MountRootsToEnumerate.Add(MountRootPart);
					}

					if (!InFilter.bRecursivePaths)
					{
						// Stop emitting and break if we're not doing a recursive search
						bEmitCallback = false;
						break;
					}
				}
			}

			if (bEmitCallback)
			{
				// This should only happen for recursive queries above the mount root, as queries at or below the mount root should have been caught by IsVirtualPathUnderMountRoot
				check(InFilter.bRecursivePaths);

				// If we were still emitting callbacks when the loop broke, then we need to query the data source at its mount root
				DataSource->CompileFilter(DataSource->GetVirtualMountRoot(), InFilter, OutCompiledFilter);
			}
		}
	}
}

void UContentBrowserDataSubsystem::EnumerateItemsMatchingFilter(const FContentBrowserDataCompiledFilter& InFilter, TFunctionRef<bool(FContentBrowserItem&&)> InCallback) const
{
	EnumerateItemsMatchingFilter(InFilter, [&InCallback](FContentBrowserItemData&& InItemData)
	{
		checkf(InItemData.IsValid(), TEXT("Enumerated items must be valid!"));
		return InCallback(FContentBrowserItem(MoveTemp(InItemData)));
	});
}

void UContentBrowserDataSubsystem::EnumerateItemsMatchingFilter(const FContentBrowserDataCompiledFilter& InFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback) const
{
	for (const auto& ActiveDataSourcePair : ActiveDataSources)
	{
		UContentBrowserDataSource* DataSource = ActiveDataSourcePair.Value;

		if (const FContentBrowserDataFilterList* FilterList = InFilter.CompiledFilters.Find(DataSource))
		{
			// Does data source have dummy paths down to its mount root that we also have to emit callbacks for?
			if (const FContentBrowserCompiledSubsystemFilter* SubsystemFilter = FilterList->FindFilter<FContentBrowserCompiledSubsystemFilter>())
			{
				for (const FName& MountRootPart : SubsystemFilter->MountRootsToEnumerate)
				{
					check(EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFolders));

					const FString MountLeafName = FPackageName::GetShortName(MountRootPart);
					InCallback(FContentBrowserItemData(DataSource, EContentBrowserItemFlags::Type_Folder, MountRootPart, *MountLeafName, FText(), nullptr));
				}
			}

			// Fully virtual folders are ones used purely for display purposes such as /All or /All/Plugins
			if (const FContentBrowserCompiledVirtualFolderFilter* VirtualFolderFilter = FilterList->FindFilter<FContentBrowserCompiledVirtualFolderFilter>())
			{
				if (EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFolders))
				{
					for (const auto& It : VirtualFolderFilter->CachedSubPaths)
					{
						// how do we skip over this item if not included (Engine Content, Engine Plugins, C++ Classes, etc..)
						InCallback(FContentBrowserItemData(It.Value));
					}
				}
			}
		}

		DataSource->EnumerateItemsMatchingFilter(InFilter, InCallback);
	}
}

void UContentBrowserDataSubsystem::EnumerateItemsUnderPath(const FName InPath, const FContentBrowserDataFilter& InFilter, TFunctionRef<bool(FContentBrowserItem&&)> InCallback) const
{
	EnumerateItemsUnderPath(InPath, InFilter, [&InCallback](FContentBrowserItemData&& InItemData)
	{
		checkf(InItemData.IsValid(), TEXT("Enumerated items must be valid!"));
		return InCallback(FContentBrowserItem(MoveTemp(InItemData)));
	});
}

void UContentBrowserDataSubsystem::EnumerateItemsUnderPath(const FName InPath, const FContentBrowserDataFilter& InFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback) const
{
	FContentBrowserDataCompiledFilter CompiledFilter;
	CompileFilter(InPath, InFilter, CompiledFilter);

	EnumerateItemsMatchingFilter(CompiledFilter, [&InCallback](FContentBrowserItemData&& InItemData)
	{
		return InCallback(MoveTemp(InItemData));
	});
}

TArray<FContentBrowserItem> UContentBrowserDataSubsystem::GetItemsUnderPath(const FName InPath, const FContentBrowserDataFilter& InFilter) const
{
	TMap<FContentBrowserItemKey, FContentBrowserItem> FoundItems;
	EnumerateItemsUnderPath(InPath, InFilter, [&FoundItems](FContentBrowserItemData&& InItemData)
	{
		checkf(InItemData.IsValid(), TEXT("Enumerated items must be valid!"));

		const FContentBrowserItemKey ItemKey(InItemData);
		if (FContentBrowserItem* FoundItem = FoundItems.Find(ItemKey))
		{
			FoundItem->Append(InItemData);
		}
		else
		{
			FoundItems.Add(ItemKey, FContentBrowserItem(MoveTemp(InItemData)));
		}

		return true;
	});

	TArray<FContentBrowserItem> FoundItemsArray;
	FoundItems.GenerateValueArray(FoundItemsArray);
	FoundItemsArray.Sort([](const FContentBrowserItem& ItemOne, const FContentBrowserItem& ItemTwo)
	{
		return ItemOne.GetPrimaryInternalItem()->GetVirtualPath().Compare(ItemTwo.GetPrimaryInternalItem()->GetVirtualPath()) < 0;
	});
	return FoundItemsArray;
}

void UContentBrowserDataSubsystem::EnumerateItemsAtPath(const FName InPath, const EContentBrowserItemTypeFilter InItemTypeFilter, TFunctionRef<bool(FContentBrowserItem&&)> InCallback) const
{
	EnumerateItemsAtPath(InPath, InItemTypeFilter, [&InCallback](FContentBrowserItemData&& InItemData)
	{
		checkf(InItemData.IsValid(), TEXT("Enumerated items must be valid!"));
		return InCallback(FContentBrowserItem(MoveTemp(InItemData)));
	});
}

void UContentBrowserDataSubsystem::EnumerateItemsAtPath(const FName InPath, const EContentBrowserItemTypeFilter InItemTypeFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback) const
{
	for (const auto& ActiveDataSourcePair : ActiveDataSources)
	{
		UContentBrowserDataSource* DataSource = ActiveDataSourcePair.Value;
		if (DataSource->IsVirtualPathUnderMountRoot(InPath))
		{
			// The requested path is managed by this data source, so query it for the items
			DataSource->EnumerateItemsAtPath(InPath, InItemTypeFilter, InCallback);
		}
		else if (EnumHasAnyFlags(InItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFolders))
		{
			bool bEmitCallback = false;

			// The requested path is not managed by this data source, but we may still need to report part of its mount root as a sub-folder
			TArrayView<const FName> MountRootHierarchy = DataSource->GetVirtualMountRootHierarchy();
			for (const FName& MountRootPart : MountRootHierarchy)
			{
				if (MountRootPart == InPath)
				{
					// Emit the callback for the *next* part of the path
					bEmitCallback = true;
					continue;
				}

				if (bEmitCallback)
				{
					const FString MountLeafName = FPackageName::GetShortName(MountRootPart);
					InCallback(FContentBrowserItemData(DataSource, EContentBrowserItemFlags::Type_Folder, MountRootPart, *MountLeafName, FText(), nullptr));
					break;
				}
			}
		}
	}
}

TArray<FContentBrowserItem> UContentBrowserDataSubsystem::GetItemsAtPath(const FName InPath, const EContentBrowserItemTypeFilter InItemTypeFilter) const
{
	TMap<FContentBrowserItemKey, FContentBrowserItem> FoundItems;
	EnumerateItemsAtPath(InPath, InItemTypeFilter, [&FoundItems](FContentBrowserItemData&& InItemData)
	{
		checkf(InItemData.IsValid(), TEXT("Enumerated items must be valid!"));

		FContentBrowserItem& FoundItem = FoundItems.FindOrAdd(FContentBrowserItemKey(InItemData));
		FoundItem.Append(InItemData);

		return true;
	});

	TArray<FContentBrowserItem> FoundItemsArray;
	FoundItems.GenerateValueArray(FoundItemsArray);
	return FoundItemsArray;
}

FContentBrowserItem UContentBrowserDataSubsystem::GetItemAtPath(const FName InPath, const EContentBrowserItemTypeFilter InItemTypeFilter) const
{
	FContentBrowserItem FoundItem;
	EnumerateItemsAtPath(InPath, InItemTypeFilter, [&FoundItem](FContentBrowserItemData&& InItemData)
	{
		checkf(InItemData.IsValid(), TEXT("Enumerated items must be valid!"));

		if (FoundItem.IsValid())
		{
			if (FContentBrowserItemKey(FoundItem) == FContentBrowserItemKey(InItemData))
			{
				FoundItem.Append(InItemData);
			}
		}
		else
		{
			FoundItem = FContentBrowserItem(MoveTemp(InItemData));
		}

		return true;
	});
	return FoundItem;
}

bool UContentBrowserDataSubsystem::IsDiscoveringItems(TArray<FText>* OutStatus) const
{
	bool bIsDiscoveringItems = false;
	for (const auto& ActiveDataSourcePair : ActiveDataSources)
	{
		FText DataSourceStatus;
		if (ActiveDataSourcePair.Value->IsDiscoveringItems(&DataSourceStatus))
		{
			bIsDiscoveringItems = true;
			if (OutStatus && !DataSourceStatus.IsEmpty())
			{
				OutStatus->Emplace(MoveTemp(DataSourceStatus));
			}
		}
	}
	return bIsDiscoveringItems;
}

bool UContentBrowserDataSubsystem::PrioritizeSearchPath(const FName InPath)
{
	bool bDidPrioritize = false;
	for (const auto& ActiveDataSourcePair : ActiveDataSources)
	{
		UContentBrowserDataSource* DataSource = ActiveDataSourcePair.Value;
		if (DataSource->IsVirtualPathUnderMountRoot(InPath))
		{
			bDidPrioritize |= DataSource->PrioritizeSearchPath(InPath);
		}
	}
	return bDidPrioritize;
}

bool UContentBrowserDataSubsystem::IsFolderVisibleIfHidingEmpty(const FName InPath) const
{
	bool bIsVisible = false;
	bool bIsKnownPath = false;
	for (const auto& ActiveDataSourcePair : ActiveDataSources)
	{
		UContentBrowserDataSource* DataSource = ActiveDataSourcePair.Value;
		if (DataSource->IsVirtualPathUnderMountRoot(InPath))
		{
			bIsKnownPath = true;
			bIsVisible |= DataSource->IsFolderVisibleIfHidingEmpty(InPath);
		}
	}
	return bIsVisible || !bIsKnownPath;
}

bool UContentBrowserDataSubsystem::CanCreateFolder(const FName InPath, FText* OutErrorMsg) const
{
	bool bCanCreateFolder = false;
	for (const auto& ActiveDataSourcePair : ActiveDataSources)
	{
		UContentBrowserDataSource* DataSource = ActiveDataSourcePair.Value;
		if (DataSource->IsVirtualPathUnderMountRoot(InPath))
		{
			bCanCreateFolder |= DataSource->CanCreateFolder(InPath, OutErrorMsg);
		}
	}
	return bCanCreateFolder;
}

FContentBrowserItemTemporaryContext UContentBrowserDataSubsystem::CreateFolder(const FName InPath) const
{
	FContentBrowserItemTemporaryContext NewItem;
	for (const auto& ActiveDataSourcePair : ActiveDataSources)
	{
		UContentBrowserDataSource* DataSource = ActiveDataSourcePair.Value;
		if (DataSource->IsVirtualPathUnderMountRoot(InPath))
		{
			FContentBrowserItemDataTemporaryContext NewItemData;
			if (DataSource->CreateFolder(InPath, NewItemData))
			{
				NewItem.AppendContext(MoveTemp(NewItemData));
			}
		}
	}
	return NewItem;
}

void UContentBrowserDataSubsystem::Legacy_TryConvertPackagePathToVirtualPaths(const FName InPackagePath, TFunctionRef<bool(FName)> InCallback)
{
	for (const auto& ActiveDataSourcePair : ActiveDataSources)
	{
		UContentBrowserDataSource* DataSource = ActiveDataSourcePair.Value;

		FName VirtualPath;
		if (DataSource->Legacy_TryConvertPackagePathToVirtualPath(InPackagePath, VirtualPath))
		{
			if (!InCallback(VirtualPath))
			{
				break;
			}
		}
	}
}

void UContentBrowserDataSubsystem::Legacy_TryConvertAssetDataToVirtualPaths(const FAssetData& InAssetData, const bool InUseFolderPaths, TFunctionRef<bool(FName)> InCallback)
{
	for (const auto& ActiveDataSourcePair : ActiveDataSources)
	{
		UContentBrowserDataSource* DataSource = ActiveDataSourcePair.Value;

		FName VirtualPath;
		if (DataSource->Legacy_TryConvertAssetDataToVirtualPath(InAssetData, InUseFolderPaths, VirtualPath))
		{
			if (!InCallback(VirtualPath))
			{
				break;
			}
		}
	}
}

void UContentBrowserDataSubsystem::HandleDataSourceRegistered(const FName& Type, IModularFeature* Feature)
{
	if (Type == UContentBrowserDataSource::GetModularFeatureTypeName())
	{
		UContentBrowserDataSource* DataSource = static_cast<UContentBrowserDataSource*>(Feature);

		checkf(DataSource->IsInitialized(), TEXT("Data source '%s' was uninitialized! Did you forget to call Initialize?"), *DataSource->GetName());

		AvailableDataSources.Add(DataSource->GetFName(), DataSource);

		if (EnabledDataSources.Contains(DataSource->GetFName()))
		{
			ActivateDataSource(DataSource->GetFName());
		}
	}
}

void UContentBrowserDataSubsystem::HandleDataSourceUnregistered(const FName& Type, IModularFeature* Feature)
{
	if (Type == UContentBrowserDataSource::GetModularFeatureTypeName())
	{
		UContentBrowserDataSource* DataSource = static_cast<UContentBrowserDataSource*>(Feature);

		if (AvailableDataSources.Contains(DataSource->GetFName()))
		{
			DeactivateDataSource(DataSource->GetFName());
		}

		AvailableDataSources.Remove(DataSource->GetFName());
	}
}

void UContentBrowserDataSubsystem::Tick(const float InDeltaTime)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_UContentBrowserDataSubsystem_Tick);

	if (GIsSavingPackage || IsGarbageCollecting() || FUObjectThreadContext::Get().IsRoutingPostLoad)
	{
		// Not to safe to Tick right now, as the below code may try and find objects
		return;
	}

	for (const auto& AvailableDataSourcePair : AvailableDataSources)
	{
		AvailableDataSourcePair.Value->Tick(InDeltaTime);
	}

	if (bPendingItemDataRefreshedNotification)
	{
		bPendingItemDataRefreshedNotification = false;
		PendingUpdates.Empty();
		ItemDataRefreshedDelegate.Broadcast();
	}

	if (PendingUpdates.Num() > 0)
	{
		ItemDataUpdatedDelegate.Broadcast(MakeArrayView(PendingUpdates));
		PendingUpdates.Empty();
	}

	if (ActiveDataSourcesDiscoveringContent.Num() > 0)
	{
		for (auto It = ActiveDataSourcesDiscoveringContent.CreateIterator(); It; ++It)
		{
			if (UContentBrowserDataSource* DataSource = ActiveDataSources.FindRef(*It))
			{
				// Has this source finished its content discovery?
				if (!DataSource->IsDiscoveringItems())
				{
					It.RemoveCurrent();
					continue;
				}
			}
			else
			{
				// Source no longer active - just remove this entry
				It.RemoveCurrent();
				continue;
			}
		}

		if (ActiveDataSourcesDiscoveringContent.Num() == 0)
		{
			ItemDataDiscoveryCompleteDelegate.Broadcast();
		}
	}
}

void UContentBrowserDataSubsystem::QueueItemDataUpdate(FContentBrowserItemDataUpdate&& InUpdate)
{
	// TODO: Merge multiple Modified updates for a single item?
	PendingUpdates.Emplace(MoveTemp(InUpdate));
}

void UContentBrowserDataSubsystem::NotifyItemDataRefreshed()
{
	bPendingItemDataRefreshedNotification = true;
}
