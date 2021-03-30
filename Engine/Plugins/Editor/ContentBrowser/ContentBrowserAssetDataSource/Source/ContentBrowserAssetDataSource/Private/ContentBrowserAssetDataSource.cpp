// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserAssetDataSource.h"
#include "ContentBrowserAssetDataCore.h"
#include "ContentBrowserDataLegacyBridge.h"
#include "Modules/ModuleManager.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "CollectionManagerModule.h"
#include "ICollectionManager.h"
#include "AssetViewUtils.h"
#include "ObjectTools.h"
#include "UObject/GCObjectScopeGuard.h"
#include "Factories/Factory.h"
#include "Misc/ScopeExit.h"
#include "Misc/StringBuilder.h"
#include "Misc/ScopedSlowTask.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "FileHelpers.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ToolMenus.h"
#include "NewAssetContextMenu.h"
#include "AssetFolderContextMenu.h"
#include "AssetFileContextMenu.h"
#include "ContentBrowserDataSubsystem.h"

#define LOCTEXT_NAMESPACE "ContentBrowserAssetDataSource"

namespace ContentBrowserAssetDataSource
{

bool PathPassesCompiledDataFilter(const FContentBrowserCompiledAssetDataFilter& InFilter, const FName InPath)
{
	auto PathPassesFilter = [&InPath](const FBlacklistPaths& InPathFilter, const bool InRecursive)
	{
		return !InPathFilter.HasFiltering() || (InRecursive ? InPathFilter.PassesStartsWithFilter(InPath, /*bAllowParentPaths*/true) : InPathFilter.PassesFilter(InPath));
	};

	const bool bPassesFilterBlacklist = PathPassesFilter(InFilter.PackagePathsToInclude, InFilter.bRecursivePackagePathsToInclude) && PathPassesFilter(InFilter.PackagePathsToExclude, InFilter.bRecursivePackagePathsToExclude);
	const bool bPassesPathFilter = PathPassesFilter(InFilter.PathBlacklist, /*bRecursive*/true);
	const bool bPassesExcludedPathsFilter = !InFilter.ExcludedPackagePaths.Contains(InPath);

	return bPassesFilterBlacklist && bPassesPathFilter && bPassesExcludedPathsFilter;
}

}

void UContentBrowserAssetDataSource::Initialize(const FName InMountRoot, const bool InAutoRegister)
{
	Super::Initialize(InMountRoot, InAutoRegister);

	AssetRegistry = &FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();
	AssetRegistry->OnFileLoadProgressUpdated().AddUObject(this, &UContentBrowserAssetDataSource::OnAssetRegistryFileLoadProgress);

	{
		static const FName NAME_AssetTools = "AssetTools";
		AssetTools = &FModuleManager::GetModuleChecked<FAssetToolsModule>(NAME_AssetTools).Get();
	}

	CollectionManager = &FCollectionManagerModule::GetModule().Get();

	// Listen for asset registry updates
	AssetRegistry->OnAssetAdded().AddUObject(this, &UContentBrowserAssetDataSource::OnAssetAdded);
	AssetRegistry->OnAssetRemoved().AddUObject(this, &UContentBrowserAssetDataSource::OnAssetRemoved);
	AssetRegistry->OnAssetRenamed().AddUObject(this, &UContentBrowserAssetDataSource::OnAssetRenamed);
	AssetRegistry->OnAssetUpdated().AddUObject(this, &UContentBrowserAssetDataSource::OnAssetUpdated);
	AssetRegistry->OnPathAdded().AddUObject(this, &UContentBrowserAssetDataSource::OnPathAdded);
	AssetRegistry->OnPathRemoved().AddUObject(this, &UContentBrowserAssetDataSource::OnPathRemoved);
	AssetRegistry->OnFilesLoaded().AddUObject(this, &UContentBrowserAssetDataSource::OnScanCompleted);

	// Listen for when assets are loaded or changed
	FCoreUObjectDelegates::OnAssetLoaded.AddUObject(this, &UContentBrowserAssetDataSource::OnAssetLoaded);
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UContentBrowserAssetDataSource::OnObjectPropertyChanged);

	// Listen for new mount roots
	FPackageName::OnContentPathMounted().AddUObject(this, &UContentBrowserAssetDataSource::OnContentPathMounted);
	FPackageName::OnContentPathDismounted().AddUObject(this, &UContentBrowserAssetDataSource::OnContentPathDismounted);

	// Listen for paths being forced visible
	AssetViewUtils::OnAlwaysShowPath().AddUObject(this, &UContentBrowserAssetDataSource::OnAlwaysShowPath);

	// Register our ability to create assets via the legacy Content Browser API
	ContentBrowserDataLegacyBridge::OnCreateNewAsset().BindUObject(this, &UContentBrowserAssetDataSource::OnBeginCreateAsset);

	// Create the asset menu instances
	AssetFolderContextMenu = MakeShared<FAssetFolderContextMenu>();
	AssetFileContextMenu = MakeShared<FAssetFileContextMenu>();

	// Bind the asset specific menu extensions
	{
		if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AddNewContextMenu"))
		{
			Menu->AddDynamicSection(*FString::Printf(TEXT("DynamicSection_DataSource_%s"), *GetName()), FNewToolMenuDelegate::CreateLambda([WeakThis = TWeakObjectPtr<UContentBrowserAssetDataSource>(this)](UToolMenu* InMenu)
			{
				if (UContentBrowserAssetDataSource* This = WeakThis.Get())
				{
					This->PopulateAddNewContextMenu(InMenu);
				}
			}));
		}

		if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.FolderContextMenu"))
		{
			Menu->AddDynamicSection(*FString::Printf(TEXT("DynamicSection_DataSource_%s"), *GetName()), FNewToolMenuDelegate::CreateLambda([WeakThis = TWeakObjectPtr<UContentBrowserAssetDataSource>(this)](UToolMenu* InMenu)
			{
				if (UContentBrowserAssetDataSource* This = WeakThis.Get())
				{
					This->PopulateAssetFolderContextMenu(InMenu);
				}
			}));
		}

		if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu"))
		{
			Menu->AddDynamicSection(*FString::Printf(TEXT("DynamicSection_DataSource_%s"), *GetName()), FNewToolMenuDelegate::CreateLambda([WeakThis = TWeakObjectPtr<UContentBrowserAssetDataSource>(this)](UToolMenu* InMenu)
			{
				if (UContentBrowserAssetDataSource* This = WeakThis.Get())
				{
					This->PopulateAssetFileContextMenu(InMenu);
				}
			}));
		}

		if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.DragDropContextMenu"))
		{
			Menu->AddDynamicSection(*FString::Printf(TEXT("DynamicSection_DataSource_%s"), *GetName()), FNewToolMenuDelegate::CreateLambda([WeakThis = TWeakObjectPtr<UContentBrowserAssetDataSource>(this)](UToolMenu* InMenu)
			{
				if (UContentBrowserAssetDataSource* This = WeakThis.Get())
				{
					This->PopulateDragDropContextMenu(InMenu);
				}
			}));
		}
	}

	DiscoveryStatusText = LOCTEXT("InitializingAssetDiscovery", "Initializing Asset Discovery...");

	// Populate the initial set of hidden empty folders
	// This will be updated as the scan finds more content
	AssetRegistry->EnumerateAllCachedPaths([this](FName InPath)
	{
		if (!AssetRegistry->HasAssets(InPath, /*bRecursive*/true))
		{
			EmptyAssetFolders.Add(InPath.ToString());
		}
		return true;
	});

	// Mount roots are always visible
	{
		FPackageName::QueryRootContentPaths(RootContentPaths);

		for (const FString& RootContentPath : RootContentPaths)
		{
			OnAlwaysShowPath(RootContentPath);
		}
	}
}

void UContentBrowserAssetDataSource::Shutdown()
{
	CollectionManager = nullptr;

	AssetTools = nullptr;

	if (!FModuleManager::Get().IsModuleLoaded(AssetRegistryConstants::ModuleName))
	{
		AssetRegistry = nullptr;
	}

	if (AssetRegistry)
	{
		AssetRegistry->OnFileLoadProgressUpdated().RemoveAll(this);

		AssetRegistry->OnAssetAdded().RemoveAll(this);
		AssetRegistry->OnAssetRemoved().RemoveAll(this);
		AssetRegistry->OnAssetRenamed().RemoveAll(this);
		AssetRegistry->OnAssetUpdated().RemoveAll(this);
		AssetRegistry->OnPathAdded().RemoveAll(this);
		AssetRegistry->OnPathRemoved().RemoveAll(this);
	}

	FCoreUObjectDelegates::OnAssetLoaded.RemoveAll(this);
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);

	AssetViewUtils::OnAlwaysShowPath().RemoveAll(this);

	ContentBrowserDataLegacyBridge::OnCreateNewAsset().Unbind();

	Super::Shutdown();
}

void UContentBrowserAssetDataSource::EnumerateRootPaths(const FContentBrowserDataFilter& InFilter, TFunctionRef<void(FName)> InCallback)
{
	for (const FString& RootContentPath : RootContentPaths)
	{
		if (RootContentPath.Len() > 1)
		{
			InCallback(FName(RootContentPath.Len() - 1, *RootContentPath));
		}
	}
}

void UContentBrowserAssetDataSource::CompileFilter(const FName InPath, const FContentBrowserDataFilter& InFilter, FContentBrowserDataCompiledFilter& OutCompiledFilter)
{
	const FContentBrowserDataObjectFilter* ObjectFilter = InFilter.ExtraFilters.FindFilter<FContentBrowserDataObjectFilter>();
	const FContentBrowserDataPackageFilter* PackageFilter = InFilter.ExtraFilters.FindFilter<FContentBrowserDataPackageFilter>();
	const FContentBrowserDataClassFilter* ClassFilter = InFilter.ExtraFilters.FindFilter<FContentBrowserDataClassFilter>();
	const FContentBrowserDataCollectionFilter* CollectionFilter = InFilter.ExtraFilters.FindFilter<FContentBrowserDataCollectionFilter>();

	const FBlacklistPaths* PathBlacklist = PackageFilter && PackageFilter->PathBlacklist && PackageFilter->PathBlacklist->HasFiltering() ? PackageFilter->PathBlacklist.Get() : nullptr;
	const FBlacklistNames* ClassBlacklist = ClassFilter && ClassFilter->ClassBlacklist && ClassFilter->ClassBlacklist->HasFiltering() ? ClassFilter->ClassBlacklist.Get() : nullptr;

	const bool bIncludeFolders = EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFolders);
	const bool bIncludeFiles = EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFiles);

	const bool bIncludeAssets = EnumHasAnyFlags(InFilter.ItemCategoryFilter, EContentBrowserItemCategoryFilter::IncludeAssets);

	FContentBrowserDataFilterList& FilterList = OutCompiledFilter.CompiledFilters.FindOrAdd(this);
	FContentBrowserCompiledAssetDataFilter& AssetDataFilter = FilterList.FindOrAddFilter<FContentBrowserCompiledAssetDataFilter>();
	AssetDataFilter.bFilterExcludesAllAssets = true;

	// If we aren't including anything, then we can just bail now
	if (!bIncludeAssets || (!bIncludeFolders && !bIncludeFiles))
	{
		return;
	}

	// If we are filtering all paths, then we can bail now as we won't return any content
	if (PathBlacklist && PathBlacklist->IsBlacklistAll())
	{
		return;
	}

	TSet<FName> InternalPaths;
	TMap<FName, TArray<FName>> VirtualPaths;
	FName SingleInternalPath;
	ExpandVirtualPath(InPath, InFilter, SingleInternalPath, InternalPaths, VirtualPaths);

	auto GetExcludedPathsForItemAttributeFilter = [this](const EContentBrowserItemAttributeFilter& InItemAttributeFilter) -> TSet<FName>
	{
		const bool bIncludeProjectContent = EnumHasAnyFlags(InItemAttributeFilter, EContentBrowserItemAttributeFilter::IncludeProject);
		const bool bIncludeEngineContent = EnumHasAnyFlags(InItemAttributeFilter, EContentBrowserItemAttributeFilter::IncludeEngine);
		const bool bIncludePluginContent = EnumHasAnyFlags(InItemAttributeFilter, EContentBrowserItemAttributeFilter::IncludePlugins);
		const bool bIncludeDeveloperContent = EnumHasAnyFlags(InItemAttributeFilter, EContentBrowserItemAttributeFilter::IncludeDeveloper);
		const bool bIncludeLocalizedContent = EnumHasAnyFlags(InItemAttributeFilter, EContentBrowserItemAttributeFilter::IncludeLocalized);
		if (!bIncludeProjectContent || !bIncludeEngineContent || !bIncludePluginContent || !bIncludeDeveloperContent || !bIncludeLocalizedContent)
		{
			FARCompiledFilter CompiledBlacklistAttributePathFilter;
			{
				FARFilter BlacklistAttributePathFilter;
				if (!bIncludeProjectContent)
				{
					static const FName ProjectContentPath = "/Game";
					BlacklistAttributePathFilter.PackagePaths.Add(ProjectContentPath);
				}
				if (!bIncludeEngineContent)
				{
					static const FName EngineContentPath = "/Engine";
					BlacklistAttributePathFilter.PackagePaths.Add(EngineContentPath);
				}
				if (!bIncludePluginContent || !bIncludeProjectContent || !bIncludeEngineContent)
				{
					const TArray<TSharedRef<IPlugin>> Plugins = IPluginManager::Get().GetEnabledPluginsWithContent();
					for (const TSharedRef<IPlugin>& PluginRef : Plugins)
					{
						const bool bExcludePlugin = !bIncludePluginContent
							|| (!bIncludeProjectContent && PluginRef->GetLoadedFrom() == EPluginLoadedFrom::Project)
							|| (!bIncludeEngineContent && PluginRef->GetLoadedFrom() == EPluginLoadedFrom::Engine);

						if (bExcludePlugin)
						{
							FString PluginContentPath = PluginRef->GetMountedAssetPath();
							if (PluginContentPath.Len() > 1)
							{
								if (PluginContentPath[PluginContentPath.Len() - 1] == TEXT('/'))
								{
									PluginContentPath.RemoveAt(PluginContentPath.Len() - 1, 1, /*bAllowShrinking*/false);
								}
								BlacklistAttributePathFilter.PackagePaths.Add(*PluginContentPath);
							}
						}
					}
				}
				if (!bIncludeDeveloperContent)
				{
					static const FName ProjectDeveloperPath = "/Game/Developers";
					BlacklistAttributePathFilter.PackagePaths.Add(ProjectDeveloperPath);
				}
				if (!bIncludeLocalizedContent)
				{
					for (const FString& RootContentPath : RootContentPaths)
					{
						BlacklistAttributePathFilter.PackagePaths.Add(*(RootContentPath / TEXT("L10N")));
					}
				}
				BlacklistAttributePathFilter.bRecursivePaths = true;
				AssetRegistry->CompileFilter(BlacklistAttributePathFilter, CompiledBlacklistAttributePathFilter);
			}

			return CompiledBlacklistAttributePathFilter.PackagePaths;
		}

		return TSet<FName>();
	};

	// If we're including folders, but not doing a recursive search then we need to handle that here as the asset code below can't deal with that correctly
	// We also go through this path if we're not including files, as then we don't run the asset code below
	if (bIncludeFolders && (!InFilter.bRecursivePaths || !bIncludeFiles))
	{
		// Build the basic paths blacklist from the given data
		if (PackageFilter)
		{
			AssetDataFilter.bRecursivePackagePathsToInclude = PackageFilter->bRecursivePackagePathsToInclude;
			for (const FName& PackagePathToInclude : PackageFilter->PackagePathsToInclude)
			{
				AssetDataFilter.PackagePathsToInclude.AddWhitelistItem(NAME_None, PackagePathToInclude);
			}

			AssetDataFilter.bRecursivePackagePathsToExclude = PackageFilter->bRecursivePackagePathsToExclude;
			for (const FName& PackagePathToExclude : PackageFilter->PackagePathsToExclude)
			{
				AssetDataFilter.PackagePathsToExclude.AddBlacklistItem(NAME_None, PackagePathToExclude);
			}
		}
		if (PathBlacklist)
		{
			AssetDataFilter.PathBlacklist = *PathBlacklist;
		}

		// Add any exclusive paths from attribute filters
		AssetDataFilter.ExcludedPackagePaths = GetExcludedPathsForItemAttributeFilter(InFilter.ItemAttributeFilter);

		// Recursive caching of folders is at least as slow as running the query on-demand
		// and significantly slower when only querying the status of a few updated items
		// To this end, we only attempt to pre-cache non-recursive queries
		if (InFilter.bRecursivePaths)
		{
			AssetDataFilter.bRunFolderQueryOnDemand = true;
			for (const FName It : InternalPaths)
			{
				AssetDataFilter.PathsToScanOnDemand.Add(It.ToString());
			}
		}
		else
		{
			for (const FName InternalPath : InternalPaths)
			{
				AssetRegistry->EnumerateSubPaths(InternalPath, [&AssetDataFilter](FName SubPath)
				{
					const bool bPassesCompiledFilter = ContentBrowserAssetDataSource::PathPassesCompiledDataFilter(AssetDataFilter, SubPath);

					if (bPassesCompiledFilter)
					{
						AssetDataFilter.CachedSubPaths.Add(SubPath);
					}
			
					return true;
				}, false);
			}
		}

		FContentBrowserCompiledVirtualFolderFilter* VirtualFolderFilter = nullptr;
		for (const auto& It : VirtualPaths)
		{
			const FName VirtualSubPath = It.Key;
			const TArray<FName>& InternalRootPaths = It.Value;

			// Determine if any of the internal paths under this virtual path will be shown
			bool bPassesCompiledFilter = false;
			for (const FName InternalRootPath : InternalRootPaths)
			{
				if (ContentBrowserAssetDataSource::PathPassesCompiledDataFilter(AssetDataFilter, InternalRootPath))
				{
					bPassesCompiledFilter = true;
					break;
				}
			}

			if (bPassesCompiledFilter)
			{
				if (!VirtualFolderFilter)
				{
					VirtualFolderFilter = &FilterList.FindOrAddFilter<FContentBrowserCompiledVirtualFolderFilter>();
				}

				if (!VirtualFolderFilter->CachedSubPaths.Contains(VirtualSubPath))
				{
					FName InternalPath;
					if (TryConvertVirtualPathToInternal(VirtualSubPath, InternalPath))
					{
						VirtualFolderFilter->CachedSubPaths.Add(VirtualSubPath, CreateAssetFolderItem(InternalPath));
					}
					else
					{
						const FString MountLeafName = FPackageName::GetShortName(VirtualSubPath);
						VirtualFolderFilter->CachedSubPaths.Add(VirtualSubPath, FContentBrowserItemData(this, EContentBrowserItemFlags::Type_Folder, VirtualSubPath, *MountLeafName, FText(), nullptr));
					}
				}
			}
		}
	}

	// If we're not including files, then we can bail now as the rest of this function deals with assets
	if (!bIncludeFiles)
	{
		return;
	}

	// If we are filtering all classes, then we can bail now as we won't return any content
	if (ClassBlacklist && ClassBlacklist->IsBlacklistAll())
	{
		return;
	}

	// If we are filtering out this path, then we can bail now as it won't return any content
	if (PathBlacklist && !InFilter.bRecursivePaths)
	{
		for (auto It = InternalPaths.CreateIterator(); It; ++It)
		{
			if (!PathBlacklist->PassesStartsWithFilter(*It))
			{
				It.RemoveCurrent();
			}
		}

		if (InternalPaths.Num() == 0)
		{
			return;
		}
	}

	const bool bWasTemporaryCachingModeEnabled = AssetRegistry->GetTemporaryCachingMode();
	AssetRegistry->SetTemporaryCachingMode(true);
	ON_SCOPE_EXIT
	{
		AssetRegistry->SetTemporaryCachingMode(bWasTemporaryCachingModeEnabled);
	};

	// Build inclusive asset filter
	FARCompiledFilter CompiledInclusiveFilter;
	{
		// Build the basic inclusive filter from the given data
		{
			FARFilter InclusiveFilter;
			if (ObjectFilter)
			{
				InclusiveFilter.ObjectPaths.Append(ObjectFilter->ObjectNamesToInclude);
				InclusiveFilter.TagsAndValues.Append(ObjectFilter->TagsAndValuesToInclude);
				InclusiveFilter.bIncludeOnlyOnDiskAssets |= ObjectFilter->bOnDiskObjectsOnly;
			}
			if (PackageFilter)
			{
				InclusiveFilter.PackageNames.Append(PackageFilter->PackageNamesToInclude);
				InclusiveFilter.PackagePaths.Append(PackageFilter->PackagePathsToInclude);
				InclusiveFilter.bRecursivePaths |= PackageFilter->bRecursivePackagePathsToInclude;
			}
			if (ClassFilter)
			{
				InclusiveFilter.ClassNames.Append(ClassFilter->ClassNamesToInclude);
				InclusiveFilter.bRecursiveClasses |= ClassFilter->bRecursiveClassNamesToInclude;
			}
			if (CollectionFilter)
			{
				TArray<FName> ObjectPathsForCollections;
				if (GetObjectPathsForCollections(CollectionFilter->SelectedCollections, CollectionFilter->bIncludeChildCollections, ObjectPathsForCollections) && ObjectPathsForCollections.Num() == 0)
				{
					// If we had collections but they contained no objects then we can bail as nothing will pass the filter
					return;
				}
				InclusiveFilter.ObjectPaths.Append(MoveTemp(ObjectPathsForCollections));
			}
			AssetRegistry->CompileFilter(InclusiveFilter, CompiledInclusiveFilter);
		}

		// Remove any inclusive paths that aren't under the set of internal paths that we want to enumerate
		{
			FARCompiledFilter CompiledInternalPathFilter;
			{
				FARFilter InternalPathFilter;
				for (const FName InternalPath : InternalPaths)
				{
					InternalPathFilter.PackagePaths.Add(InternalPath);
				}
				InternalPathFilter.bRecursivePaths = InFilter.bRecursivePaths;
				AssetRegistry->CompileFilter(InternalPathFilter, CompiledInternalPathFilter);
			}

			if (CompiledInclusiveFilter.PackagePaths.Num() > 0)
			{
				// Explicit paths given - remove anything not in the internal paths set
				// If the paths resolve as empty then the combined filter will return nothing and can be skipped
				CompiledInclusiveFilter.PackagePaths = CompiledInclusiveFilter.PackagePaths.Intersect(CompiledInternalPathFilter.PackagePaths);
				if (CompiledInclusiveFilter.PackagePaths.Num() == 0)
				{
					return;
				}
			}
			else
			{
				// No explicit paths given - just use the internal paths set
				CompiledInclusiveFilter.PackagePaths = MoveTemp(CompiledInternalPathFilter.PackagePaths);
			}
		}

		// Remove any inclusive paths that aren't in the explicit whitelist set
		if (PathBlacklist && PathBlacklist->GetWhitelist().Num() > 0)
		{
			FARCompiledFilter CompiledWhitelistPathFilter;
			{
				FARFilter WhitelistPathFilter;
				for (const auto& WhitelistPair : PathBlacklist->GetWhitelist())
				{
					WhitelistPathFilter.PackagePaths.Add(*WhitelistPair.Key);
				}
				WhitelistPathFilter.bRecursivePaths = true;
				AssetRegistry->CompileFilter(WhitelistPathFilter, CompiledWhitelistPathFilter);
			}

			if (CompiledInclusiveFilter.PackagePaths.Num() > 0)
			{
				// Explicit paths given - remove anything not in the whitelist paths set
				// If the paths resolve as empty then the combined filter will return nothing and can be skipped
				CompiledInclusiveFilter.PackagePaths = CompiledInclusiveFilter.PackagePaths.Intersect(CompiledWhitelistPathFilter.PackagePaths);
				if (CompiledInclusiveFilter.PackagePaths.Num() == 0)
				{
					return;
				}
			}
			else
			{
				// No explicit paths given - just use the whitelist paths set
				CompiledInclusiveFilter.PackagePaths = MoveTemp(CompiledWhitelistPathFilter.PackagePaths);
			}
		}

		// Remove any inclusive classes that aren't in the explicit whitelist set
		if (ClassBlacklist && ClassBlacklist->GetWhitelist().Num() > 0)
		{
			FARCompiledFilter CompiledWhitelistClassFilter;
			{
				FARFilter WhitelistClassFilter;
				for (const auto& WhitelistPair : ClassBlacklist->GetWhitelist())
				{
					WhitelistClassFilter.ClassNames.Add(WhitelistPair.Key);
				}
				WhitelistClassFilter.bRecursiveClasses = true;
				AssetRegistry->CompileFilter(WhitelistClassFilter, CompiledWhitelistClassFilter);
			}

			if (CompiledInclusiveFilter.ClassNames.Num() > 0)
			{
				// Explicit classes given - remove anything not in the whitelist class set
				// If the classes resolve as empty then the combined filter will return nothing and can be skipped
				CompiledInclusiveFilter.ClassNames = CompiledInclusiveFilter.ClassNames.Intersect(CompiledWhitelistClassFilter.ClassNames);
				if (CompiledInclusiveFilter.ClassNames.Num() == 0)
				{
					return;
				}
			}
			else
			{
				// No explicit classes given - just use the whitelist class set
				CompiledInclusiveFilter.ClassNames = MoveTemp(CompiledWhitelistClassFilter.ClassNames);
			}
		}
	}

	// Build exclusive asset filter
	FARCompiledFilter CompiledExclusiveFilter;
	{
		// Build the basic exclusive filter from the given data
		{
			FARFilter ExclusiveFilter;
			if (ObjectFilter)
			{
				ExclusiveFilter.ObjectPaths.Append(ObjectFilter->ObjectNamesToExclude);
				ExclusiveFilter.TagsAndValues.Append(ObjectFilter->TagsAndValuesToExclude);
				ExclusiveFilter.bIncludeOnlyOnDiskAssets |= ObjectFilter->bOnDiskObjectsOnly;
			}
			if (PackageFilter)
			{
				ExclusiveFilter.PackageNames.Append(PackageFilter->PackageNamesToExclude);
				ExclusiveFilter.PackagePaths.Append(PackageFilter->PackagePathsToExclude);
				ExclusiveFilter.bRecursivePaths |= PackageFilter->bRecursivePackagePathsToExclude;
			}
			if (ClassFilter)
			{
				ExclusiveFilter.ClassNames.Append(ClassFilter->ClassNamesToExclude);
				ExclusiveFilter.bRecursiveClasses |= ClassFilter->bRecursiveClassNamesToExclude;
			}
			AssetRegistry->CompileFilter(ExclusiveFilter, CompiledExclusiveFilter);
		}

		// Add any exclusive paths that are in the explicit blacklist set
		if (PathBlacklist && PathBlacklist->GetBlacklist().Num() > 0)
		{
			FARCompiledFilter CompiledBlacklistPathFilter;
			{
				FARFilter BlacklistPathFilter;
				for (const auto& BlacklistPair : PathBlacklist->GetBlacklist())
				{
					BlacklistPathFilter.PackagePaths.Add(*BlacklistPair.Key);
				}
				BlacklistPathFilter.bRecursivePaths = true;
				AssetRegistry->CompileFilter(BlacklistPathFilter, CompiledBlacklistPathFilter);
			}

			CompiledExclusiveFilter.PackagePaths.Append(CompiledBlacklistPathFilter.PackagePaths);
		}

		// Add any exclusive paths from attribute filters
		CompiledExclusiveFilter.PackagePaths.Append(GetExcludedPathsForItemAttributeFilter(InFilter.ItemAttributeFilter));

		// Add any exclusive classes that are in the explicit blacklist set
		if (ClassBlacklist && ClassBlacklist->GetBlacklist().Num() > 0)
		{
			FARCompiledFilter CompiledBlacklistClassFilter;
			{
				FARFilter BlacklistClassFilter;
				for (const auto& BlacklistPair : ClassBlacklist->GetBlacklist())
				{
					BlacklistClassFilter.ClassNames.Add(BlacklistPair.Key);
				}
				BlacklistClassFilter.bRecursiveClasses = true;
				AssetRegistry->CompileFilter(BlacklistClassFilter, CompiledBlacklistClassFilter);
			}

			CompiledExclusiveFilter.ClassNames.Append(CompiledBlacklistClassFilter.ClassNames);
		}
	}

	// Apply our exclusive filter to the inclusive one to resolve cases where the exclusive filter cancels out the inclusive filter
	// If any filter components resolve as empty then the combined filter will return nothing and can be skipped
	{
		if (CompiledInclusiveFilter.PackageNames.Num() > 0 && CompiledExclusiveFilter.PackageNames.Num() > 0)
		{
			CompiledInclusiveFilter.PackageNames = CompiledInclusiveFilter.PackageNames.Difference(CompiledExclusiveFilter.PackageNames);
			if (CompiledInclusiveFilter.PackageNames.Num() == 0)
			{
				return;
			}
			CompiledExclusiveFilter.PackageNames.Reset();
		}
		if (CompiledInclusiveFilter.PackagePaths.Num() > 0 && CompiledExclusiveFilter.PackagePaths.Num() > 0)
		{
			CompiledInclusiveFilter.PackagePaths = CompiledInclusiveFilter.PackagePaths.Difference(CompiledExclusiveFilter.PackagePaths);
			if (CompiledInclusiveFilter.PackagePaths.Num() == 0)
			{
				return;
			}
			CompiledExclusiveFilter.PackagePaths.Reset();
		}
		if (CompiledInclusiveFilter.ObjectPaths.Num() > 0 && CompiledExclusiveFilter.ObjectPaths.Num() > 0)
		{
			CompiledInclusiveFilter.ObjectPaths = CompiledInclusiveFilter.ObjectPaths.Difference(CompiledExclusiveFilter.ObjectPaths);
			if (CompiledInclusiveFilter.ObjectPaths.Num() == 0)
			{
				return;
			}
			CompiledExclusiveFilter.ObjectPaths.Reset();
		}
		if (CompiledInclusiveFilter.ClassNames.Num() > 0 && CompiledExclusiveFilter.ClassNames.Num() > 0)
		{
			CompiledInclusiveFilter.ClassNames = CompiledInclusiveFilter.ClassNames.Difference(CompiledExclusiveFilter.ClassNames);
			if (CompiledInclusiveFilter.ClassNames.Num() == 0)
			{
				return;
			}
			CompiledExclusiveFilter.ClassNames.Reset();
		}
	}

	checkf(CompiledInclusiveFilter.PackagePaths.Num() > 0, TEXT("A compiled asset filter is required to have at least 1 path!"));

	// If we are enumerating recursively then the inclusive path list will already be fully filtered so just use that
	if (bIncludeFolders && InFilter.bRecursivePaths)
	{
		AssetDataFilter.CachedSubPaths = CompiledInclusiveFilter.PackagePaths;
		for (const FName InternalPath : InternalPaths)
		{
			AssetDataFilter.CachedSubPaths.Remove(InternalPath); // Remove the root as it's not a sub-path
		}
		AssetDataFilter.CachedSubPaths.Sort(FNameLexicalLess()); // Sort as we enumerate these in parent->child order
	}

	// If we got this far then we have something in the filters and need to run the query
	AssetDataFilter.bFilterExcludesAllAssets = false;
	AssetDataFilter.InclusiveFilter = MoveTemp(CompiledInclusiveFilter);
	AssetDataFilter.ExclusiveFilter = MoveTemp(CompiledExclusiveFilter);

	// Resolve any custom assets
	if (const FContentBrowserDataLegacyFilter* LegacyFilter = InFilter.ExtraFilters.FindFilter<FContentBrowserDataLegacyFilter>())
	{
		if (LegacyFilter->OnGetCustomSourceAssets.IsBound())
		{
			FARFilter CustomSourceAssetsFilter;
			CustomSourceAssetsFilter.PackageNames = AssetDataFilter.InclusiveFilter.PackageNames.Array();
			CustomSourceAssetsFilter.PackagePaths = AssetDataFilter.InclusiveFilter.PackagePaths.Array();
			CustomSourceAssetsFilter.ObjectPaths = AssetDataFilter.InclusiveFilter.ObjectPaths.Array();
			CustomSourceAssetsFilter.ClassNames = AssetDataFilter.InclusiveFilter.ClassNames.Array();
			CustomSourceAssetsFilter.TagsAndValues = AssetDataFilter.InclusiveFilter.TagsAndValues;
			CustomSourceAssetsFilter.bIncludeOnlyOnDiskAssets = AssetDataFilter.InclusiveFilter.bIncludeOnlyOnDiskAssets;

			LegacyFilter->OnGetCustomSourceAssets.Execute(CustomSourceAssetsFilter, AssetDataFilter.CustomSourceAssets);
		}
	}
}

void UContentBrowserAssetDataSource::EnumerateItemsMatchingFilter(const FContentBrowserDataCompiledFilter& InFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback)
{
	const FContentBrowserDataFilterList* FilterList = InFilter.CompiledFilters.Find(this);
	if (!FilterList)
	{
		return;
	}
	
	const FContentBrowserCompiledAssetDataFilter* AssetDataFilter = FilterList->FindFilter<FContentBrowserCompiledAssetDataFilter>();
	if (!AssetDataFilter)
	{
		return;
	}

	if (EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFolders))
	{
		if (AssetDataFilter->bRunFolderQueryOnDemand)
		{
			// Handle recursion manually so that we can cull out entire sub-trees once we fail to match a folder
			TArray<FName, TInlineAllocator<16>> PathsToScan;

			for (const FString& It : AssetDataFilter->PathsToScanOnDemand)
			{
				PathsToScan.Reset();
				PathsToScan.Add(*It);
				while (PathsToScan.Num() > 0)
				{
					const FName PathToScan = PathsToScan.Pop(/*bAllowShrinking*/false);
					AssetRegistry->EnumerateSubPaths(PathToScan, [this, &InCallback, &AssetDataFilter, &PathsToScan](FName SubPath)
					{
						const bool bPassesCompiledFilter = ContentBrowserAssetDataSource::PathPassesCompiledDataFilter(*AssetDataFilter, SubPath);
			
						if (bPassesCompiledFilter)
						{
							if (!InCallback(CreateAssetFolderItem(SubPath)))
							{
								return false;
							}

							PathsToScan.Add(SubPath);
						}
			
						return true;
					}, false);
				}
			}
		}
		else
		{
			for (const FName& SubPath : AssetDataFilter->CachedSubPaths)
			{
				if (!InCallback(CreateAssetFolderItem(SubPath)))
				{
					return;
				}
			}
		}
	}

	if (EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFiles) && !AssetDataFilter->bFilterExcludesAllAssets)
	{
		for (const FAssetData& CustomSourceAsset : AssetDataFilter->CustomSourceAssets)
		{
			if (!InCallback(CreateAssetFileItem(CustomSourceAsset)))
			{
				return;
			}
		}

		AssetRegistry->EnumerateAssets(AssetDataFilter->InclusiveFilter, [this, &InCallback, &AssetDataFilter](const FAssetData& AssetData)
		{
			if (ContentBrowserAssetData::IsPrimaryAsset(AssetData))
			{
				const bool bPassesExclusiveFilter = AssetDataFilter->ExclusiveFilter.IsEmpty() || !AssetRegistry->IsAssetIncludedByFilter(AssetData, AssetDataFilter->ExclusiveFilter);
				if (bPassesExclusiveFilter)
				{
					return InCallback(CreateAssetFileItem(AssetData));
				}
			}
			return true;
		});
	}
}

void UContentBrowserAssetDataSource::EnumerateItemsAtPath(const FName InPath, const EContentBrowserItemTypeFilter InItemTypeFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback)
{
	FName InternalPath;
	if (!TryConvertVirtualPathToInternal(InPath, InternalPath))
	{
		return;
	}
	
	if (EnumHasAnyFlags(InItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFolders))
	{
		if (AssetRegistry->PathExists(InternalPath))
		{
			InCallback(CreateAssetFolderItem(InternalPath));
		}
	}

	if (EnumHasAnyFlags(InItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFiles))
	{
		FARFilter ARFilter;
		ARFilter.ObjectPaths.Add(InternalPath);
		AssetRegistry->EnumerateAssets(ARFilter, [this, &InCallback](const FAssetData& AssetData)
		{
			if (ContentBrowserAssetData::IsPrimaryAsset(AssetData))
			{
				return InCallback(CreateAssetFileItem(AssetData));
			}
			return true;
		});
	}
}

bool UContentBrowserAssetDataSource::IsDiscoveringItems(FText* OutStatus)
{
	if (AssetRegistry->IsLoadingAssets())
	{
		ContentBrowserAssetData::SetOptionalErrorMessage(OutStatus, DiscoveryStatusText);
		return true;
	}

	return false;
}

bool UContentBrowserAssetDataSource::PrioritizeSearchPath(const FName InPath)
{
	FName InternalPath;
	if (!TryConvertVirtualPathToInternal(InPath, InternalPath))
	{
		return false;
	}

	AssetRegistry->PrioritizeSearchPath(InternalPath.ToString() / FString());
	return true;
}

bool UContentBrowserAssetDataSource::IsFolderVisibleIfHidingEmpty(const FName InPath)
{
	FName InternalPath;
	if (!TryConvertVirtualPathToInternal(InPath, InternalPath))
	{
		return false;
	}

	if (!IsKnownContentPath(InternalPath))
	{
		return false;
	}

	TStringBuilder<FName::StringBufferSize> InternalPathStr;
	InternalPath.ToString(InternalPathStr);

	const FStringView InternalPathStrView = InternalPathStr;
	const uint32 InternalPathHash = GetTypeHash(InternalPathStrView);

	return AlwaysVisibleAssetFolders.ContainsByHash(InternalPathHash, InternalPathStrView) 
		|| !EmptyAssetFolders.ContainsByHash(InternalPathHash, InternalPathStrView);
}

bool UContentBrowserAssetDataSource::CanCreateFolder(const FName InPath, FText* OutErrorMsg)
{
	FName InternalPath;
	if (!TryConvertVirtualPathToInternal(InPath, InternalPath))
	{
		return false;
	}

	if (!IsKnownContentPath(InternalPath))
	{
		return false;
	}

	return ContentBrowserAssetData::CanModifyPath(AssetTools, InternalPath, OutErrorMsg);
}

bool UContentBrowserAssetDataSource::CreateFolder(const FName InPath, FContentBrowserItemDataTemporaryContext& OutPendingItem)
{
	const FString ParentPath = FPackageName::GetLongPackagePath(InPath.ToString());
	FName InternalParentPath;
	if (!TryConvertVirtualPathToInternal(*ParentPath, InternalParentPath))
	{
		return false;
	}

	const FString FolderItemName = FPackageName::GetShortName(InPath);
	FString InternalPathString = InternalParentPath.ToString() + TEXT("/") + FolderItemName;

	FContentBrowserItemData NewItemData(
		this,
		EContentBrowserItemFlags::Type_Folder | EContentBrowserItemFlags::Category_Asset | EContentBrowserItemFlags::Temporary_Creation,
		InPath,
		*FolderItemName,
		FText::AsCultureInvariant(FolderItemName),
		MakeShared<FContentBrowserAssetFolderItemDataPayload>(*InternalPathString)
		);

	OutPendingItem = FContentBrowserItemDataTemporaryContext(
		MoveTemp(NewItemData),
		FContentBrowserItemDataTemporaryContext::FOnValidateItem::CreateUObject(this, &UContentBrowserAssetDataSource::OnValidateItemName),
		FContentBrowserItemDataTemporaryContext::FOnFinalizeItem::CreateUObject(this, &UContentBrowserAssetDataSource::OnFinalizeCreateFolder)
		);

	return true;
}

bool UContentBrowserAssetDataSource::DoesItemPassFilter(const FContentBrowserItemData& InItem, const FContentBrowserDataCompiledFilter& InFilter)
{
	const FContentBrowserDataFilterList* FilterList = InFilter.CompiledFilters.Find(this);
	if (!FilterList)
	{
		return false;
	}

	const FContentBrowserCompiledAssetDataFilter* AssetDataFilter = FilterList->FindFilter<FContentBrowserCompiledAssetDataFilter>();
	if (!AssetDataFilter)
	{
		return false;
	}

	switch (InItem.GetItemType())
	{
	case EContentBrowserItemFlags::Type_Folder:
		if (EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFolders))
		{
			if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(InItem))
			{
				if (AssetDataFilter->bRunFolderQueryOnDemand)
				{
					TStringBuilder<FName::StringBufferSize> FolderInternalPathStr;
					FolderPayload->GetInternalPath().ToString(FolderInternalPathStr);
					FStringView FolderInternalPathStrView = FolderInternalPathStr;

					bool bIsUnderSearchPath = false;
					for (const FString& It : AssetDataFilter->PathsToScanOnDemand)
					{
						if (It == TEXT("/"))
						{
							bIsUnderSearchPath = true;
							break;
						}

						if (FolderInternalPathStrView.StartsWith(It))
						{
							if ((FolderInternalPathStrView.Len() <= It.Len()) || (FolderInternalPathStrView[It.Len()] == TEXT('/')))
							{
								bIsUnderSearchPath = true;
								break;
							}
						}
					}

					const bool bPassesCompiledFilter = ContentBrowserAssetDataSource::PathPassesCompiledDataFilter(*AssetDataFilter, FolderPayload->GetInternalPath());

					return bIsUnderSearchPath && bPassesCompiledFilter;
				}
				else
				{
					return AssetDataFilter->CachedSubPaths.Contains(FolderPayload->GetInternalPath());
				}
			}
		}
		break;

	case EContentBrowserItemFlags::Type_File:
		if (EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFiles) && !AssetDataFilter->bFilterExcludesAllAssets)
		{
			if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InItem))
			{
				const bool bPassesInclusiveFilter = AssetDataFilter->InclusiveFilter.IsEmpty() ||  AssetRegistry->IsAssetIncludedByFilter(AssetPayload->GetAssetData(), AssetDataFilter->InclusiveFilter);
				const bool bPassesExclusiveFilter = AssetDataFilter->ExclusiveFilter.IsEmpty() || !AssetRegistry->IsAssetIncludedByFilter(AssetPayload->GetAssetData(), AssetDataFilter->ExclusiveFilter);
				const bool bIsCustomAsset = AssetDataFilter->CustomSourceAssets.Contains(AssetPayload->GetAssetData());
				return (bPassesInclusiveFilter && bPassesExclusiveFilter) || bIsCustomAsset;
			}
		}
		break;

	default:
		break;
	}
	
	return false;
}

bool UContentBrowserAssetDataSource::GetItemAttribute(const FContentBrowserItemData& InItem, const bool InIncludeMetaData, const FName InAttributeKey, FContentBrowserItemDataAttributeValue& OutAttributeValue)
{
	return ContentBrowserAssetData::GetItemAttribute(this, InItem, InIncludeMetaData, InAttributeKey, OutAttributeValue);
}

bool UContentBrowserAssetDataSource::GetItemAttributes(const FContentBrowserItemData& InItem, const bool InIncludeMetaData, FContentBrowserItemDataAttributeValues& OutAttributeValues)
{
	return ContentBrowserAssetData::GetItemAttributes(this, InItem, InIncludeMetaData, OutAttributeValues);
}

bool UContentBrowserAssetDataSource::GetItemPhysicalPath(const FContentBrowserItemData& InItem, FString& OutDiskPath)
{
	return ContentBrowserAssetData::GetItemPhysicalPath(this, InItem, OutDiskPath);
}

bool UContentBrowserAssetDataSource::IsItemDirty(const FContentBrowserItemData& InItem)
{
	return ContentBrowserAssetData::IsItemDirty(this, InItem);
}

bool UContentBrowserAssetDataSource::CanEditItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	return ContentBrowserAssetData::CanEditItem(AssetTools, this, InItem, OutErrorMsg);
}

bool UContentBrowserAssetDataSource::EditItem(const FContentBrowserItemData& InItem)
{
	return ContentBrowserAssetData::EditItems(AssetTools, this, MakeArrayView(&InItem, 1));
}

bool UContentBrowserAssetDataSource::BulkEditItems(TArrayView<const FContentBrowserItemData> InItems)
{
	return ContentBrowserAssetData::EditItems(AssetTools, this, InItems);
}

bool UContentBrowserAssetDataSource::CanPreviewItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	return ContentBrowserAssetData::CanPreviewItem(AssetTools, this, InItem, OutErrorMsg);
}

bool UContentBrowserAssetDataSource::PreviewItem(const FContentBrowserItemData& InItem)
{
	return ContentBrowserAssetData::PreviewItems(AssetTools, this, MakeArrayView(&InItem, 1));
}

bool UContentBrowserAssetDataSource::BulkPreviewItems(TArrayView<const FContentBrowserItemData> InItems)
{
	return ContentBrowserAssetData::PreviewItems(AssetTools, this, InItems);
}

bool UContentBrowserAssetDataSource::CanDuplicateItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	return ContentBrowserAssetData::CanDuplicateItem(AssetTools, this, InItem, OutErrorMsg);
}

bool UContentBrowserAssetDataSource::DuplicateItem(const FContentBrowserItemData& InItem, FContentBrowserItemDataTemporaryContext& OutPendingItem)
{
	UObject* SourceAsset = nullptr;
	FAssetData NewAssetData;
	if (ContentBrowserAssetData::DuplicateItem(AssetTools, this, InItem, SourceAsset, NewAssetData))
	{
		FName VirtualizedPath;
		TryConvertInternalPathToVirtual(NewAssetData.ObjectPath, VirtualizedPath);

		FContentBrowserItemData NewItemData(
			this,
			EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Asset | EContentBrowserItemFlags::Temporary_Duplication,
			VirtualizedPath,
			NewAssetData.AssetName,
			FText::AsCultureInvariant(NewAssetData.AssetName.ToString()),
			MakeShared<FContentBrowserAssetFileItemDataPayload_Duplication>(MoveTemp(NewAssetData), SourceAsset)
			);

		OutPendingItem = FContentBrowserItemDataTemporaryContext(
			MoveTemp(NewItemData),
			FContentBrowserItemDataTemporaryContext::FOnValidateItem::CreateUObject(this, &UContentBrowserAssetDataSource::OnValidateItemName),
			FContentBrowserItemDataTemporaryContext::FOnFinalizeItem::CreateUObject(this, &UContentBrowserAssetDataSource::OnFinalizeDuplicateAsset)
			);

		return true;
	}

	return false;
}

bool UContentBrowserAssetDataSource::BulkDuplicateItems(TArrayView<const FContentBrowserItemData> InItems, TArray<FContentBrowserItemData>& OutNewItems)
{
	TArray<FAssetData> NewAssets;
	if (ContentBrowserAssetData::DuplicateItems(AssetTools, this, InItems, NewAssets))
	{
		for (const FAssetData& NewAsset : NewAssets)
		{
			OutNewItems.Emplace(CreateAssetFileItem(NewAsset));
		}

		return true;
	}

	return false;
}

bool UContentBrowserAssetDataSource::CanSaveItem(const FContentBrowserItemData& InItem, const EContentBrowserItemSaveFlags InSaveFlags, FText* OutErrorMsg)
{
	return ContentBrowserAssetData::CanSaveItem(AssetTools, this, InItem, InSaveFlags, OutErrorMsg);
}

bool UContentBrowserAssetDataSource::SaveItem(const FContentBrowserItemData& InItem, const EContentBrowserItemSaveFlags InSaveFlags)
{
	return ContentBrowserAssetData::SaveItems(AssetTools, this, MakeArrayView(&InItem, 1), InSaveFlags);
}

bool UContentBrowserAssetDataSource::BulkSaveItems(TArrayView<const FContentBrowserItemData> InItems, const EContentBrowserItemSaveFlags InSaveFlags)
{
	return ContentBrowserAssetData::SaveItems(AssetTools, this, InItems, InSaveFlags);
}

bool UContentBrowserAssetDataSource::CanDeleteItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	return ContentBrowserAssetData::CanDeleteItem(AssetTools, AssetRegistry, this, InItem, OutErrorMsg);
}

bool UContentBrowserAssetDataSource::DeleteItem(const FContentBrowserItemData& InItem)
{
	return ContentBrowserAssetData::DeleteItems(AssetTools, AssetRegistry, this, MakeArrayView(&InItem, 1));
}

bool UContentBrowserAssetDataSource::BulkDeleteItems(TArrayView<const FContentBrowserItemData> InItems)
{
	return ContentBrowserAssetData::DeleteItems(AssetTools, AssetRegistry, this, InItems);
}

bool UContentBrowserAssetDataSource::CanRenameItem(const FContentBrowserItemData& InItem, const FString* InNewName, FText* OutErrorMsg)
{
	return ContentBrowserAssetData::CanRenameItem(AssetTools, this, InItem, InNewName, OutErrorMsg);
}

bool UContentBrowserAssetDataSource::RenameItem(const FContentBrowserItemData& InItem, const FString& InNewName, FContentBrowserItemData& OutNewItem)
{
	if (ContentBrowserAssetData::RenameItem(AssetTools, AssetRegistry, this, InItem, InNewName))
	{
		switch (InItem.GetItemType())
		{
		case EContentBrowserItemFlags::Type_Folder:
			if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(InItem))
			{
				const FName NewFolderPath = *(FPaths::GetPath(FolderPayload->GetInternalPath().ToString()) / InNewName);
				OutNewItem = CreateAssetFolderItem(NewFolderPath);
			}
			break;

		case EContentBrowserItemFlags::Type_File:
			if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InItem))
			{
				// The asset should already be loaded from preforming the rename
				// We can use the renamed object instance to create the new asset data for the renamed item
				if (UObject* Asset = AssetPayload->GetAsset())
				{
					OutNewItem = CreateAssetFileItem(FAssetData(Asset));
				}
			}
			break;

		default:
			break;
		}

		return true;
	}

	return false;
}

bool UContentBrowserAssetDataSource::CanCopyItem(const FContentBrowserItemData& InItem, const FName InDestPath, FText* OutErrorMsg)
{
	// Cannot copy an item outside the paths known to this data source
	FName InternalDestPath;
	if (!TryConvertVirtualPathToInternal(InDestPath, InternalDestPath))
	{
		ContentBrowserAssetData::SetOptionalErrorMessage(OutErrorMsg, FText::Format(LOCTEXT("Error_FolderIsUnknown", "Folder '{0}' is outside the mount root of this data source ({1})"), FText::FromName(InDestPath), FText::FromName(GetVirtualMountRoot())));
		return false;
	}

	// The destination path must be a content folder
	if (!IsKnownContentPath(InternalDestPath))
	{
		ContentBrowserAssetData::SetOptionalErrorMessage(OutErrorMsg, FText::Format(LOCTEXT("Error_FolderIsNotContent", "Folder '{0}' is not a known content path"), FText::FromName(InDestPath)));
		return false;
	}

	// The destination path must be writable
	if (!ContentBrowserAssetData::CanModifyPath(AssetTools, InternalDestPath, OutErrorMsg))
	{
		return false;
	}

	return true;
}

bool UContentBrowserAssetDataSource::CopyItem(const FContentBrowserItemData& InItem, const FName InDestPath)
{
	FName InternalDestPath;
	if (!TryConvertVirtualPathToInternal(InDestPath, InternalDestPath))
	{
		return false;
	}

	if (!IsKnownContentPath(InternalDestPath))
	{
		return false;
	}

	return ContentBrowserAssetData::CopyItems(AssetTools, this, MakeArrayView(&InItem, 1), InternalDestPath);
}

bool UContentBrowserAssetDataSource::BulkCopyItems(TArrayView<const FContentBrowserItemData> InItems, const FName InDestPath)
{
	FName InternalDestPath;
	if (!TryConvertVirtualPathToInternal(InDestPath, InternalDestPath))
	{
		return false;
	}

	if (!IsKnownContentPath(InternalDestPath))
	{
		return false;
	}

	return ContentBrowserAssetData::CopyItems(AssetTools, this, InItems, InternalDestPath);
}

bool UContentBrowserAssetDataSource::CanMoveItem(const FContentBrowserItemData& InItem, const FName InDestPath, FText* OutErrorMsg)
{
	// Cannot move an item outside the paths known to this data source
	FName InternalDestPath;
	if (!TryConvertVirtualPathToInternal(InDestPath, InternalDestPath))
	{
		ContentBrowserAssetData::SetOptionalErrorMessage(OutErrorMsg, FText::Format(LOCTEXT("Error_FolderIsUnknown", "Folder '{0}' is outside the mount root of this data source ({1})"), FText::FromName(InDestPath), FText::FromName(GetVirtualMountRoot())));
		return false;
	}

	// The destination path must be a content folder
	if (!IsKnownContentPath(InternalDestPath))
	{
		ContentBrowserAssetData::SetOptionalErrorMessage(OutErrorMsg, FText::Format(LOCTEXT("Error_FolderIsNotContent", "Folder '{0}' is not a known content path"), FText::FromName(InDestPath)));
		return false;
	}

	// The destination path must be writable
	if (!ContentBrowserAssetData::CanModifyPath(AssetTools, InternalDestPath, OutErrorMsg))
	{
		return false;
	}

	// Moving has to be able to delete the original item
	if (!ContentBrowserAssetData::CanModifyItem(AssetTools, this, InItem, OutErrorMsg))
	{
		return false;
	}

	return true;
}

bool UContentBrowserAssetDataSource::MoveItem(const FContentBrowserItemData& InItem, const FName InDestPath)
{
	FName InternalDestPath;
	if (!TryConvertVirtualPathToInternal(InDestPath, InternalDestPath))
	{
		return false;
	}

	if (!IsKnownContentPath(InternalDestPath))
	{
		return false;
	}

	return ContentBrowserAssetData::MoveItems(AssetTools, this, MakeArrayView(&InItem, 1), InternalDestPath);
}

bool UContentBrowserAssetDataSource::BulkMoveItems(TArrayView<const FContentBrowserItemData> InItems, const FName InDestPath)
{
	FName InternalDestPath;
	if (!TryConvertVirtualPathToInternal(InDestPath, InternalDestPath))
	{
		return false;
	}

	if (!IsKnownContentPath(InternalDestPath))
	{
		return false;
	}

	return ContentBrowserAssetData::MoveItems(AssetTools, this, InItems, InternalDestPath);
}

bool UContentBrowserAssetDataSource::AppendItemReference(const FContentBrowserItemData& InItem, FString& InOutStr)
{
	return ContentBrowserAssetData::AppendItemReference(AssetRegistry, this, InItem, InOutStr);
}

bool UContentBrowserAssetDataSource::UpdateThumbnail(const FContentBrowserItemData& InItem, FAssetThumbnail& InThumbnail)
{
	return ContentBrowserAssetData::UpdateItemThumbnail(this, InItem, InThumbnail);
}

bool UContentBrowserAssetDataSource::CanHandleDragDropEvent(const FContentBrowserItemData& InItem, const FDragDropEvent& InDragDropEvent) const
{
	if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(InItem))
	{
		if (TSharedPtr<FExternalDragOperation> ExternalDragDropOp = InDragDropEvent.GetOperationAs<FExternalDragOperation>())
		{
			TOptional<EMouseCursor::Type> NewDragCursor;
			if (!ExternalDragDropOp->HasFiles() || !ContentBrowserAssetData::CanModifyPath(AssetTools, FolderPayload->GetInternalPath(), nullptr))
			{
				NewDragCursor = EMouseCursor::SlashedCircle;
			}
			ExternalDragDropOp->SetCursorOverride(NewDragCursor);

			return true; // We will handle this drop, even if the result is invalid (eg, read-only folder)
		}
	}

	return false;
}

bool UContentBrowserAssetDataSource::HandleDragEnterItem(const FContentBrowserItemData& InItem, const FDragDropEvent& InDragDropEvent)
{
	return CanHandleDragDropEvent(InItem, InDragDropEvent);
}

bool UContentBrowserAssetDataSource::HandleDragOverItem(const FContentBrowserItemData& InItem, const FDragDropEvent& InDragDropEvent)
{
	return CanHandleDragDropEvent(InItem, InDragDropEvent);
}

bool UContentBrowserAssetDataSource::HandleDragLeaveItem(const FContentBrowserItemData& InItem, const FDragDropEvent& InDragDropEvent)
{
	return CanHandleDragDropEvent(InItem, InDragDropEvent);
}

bool UContentBrowserAssetDataSource::HandleDragDropOnItem(const FContentBrowserItemData& InItem, const FDragDropEvent& InDragDropEvent)
{
	if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(InItem))
	{
		if (TSharedPtr<FExternalDragOperation> ExternalDragDropOp = InDragDropEvent.GetOperationAs<FExternalDragOperation>())
		{
			FText ErrorMsg;
			if (ExternalDragDropOp->HasFiles() && ContentBrowserAssetData::CanModifyPath(AssetTools, FolderPayload->GetInternalPath(), &ErrorMsg))
			{
				// Delay import until next tick to avoid blocking the process that files were dragged from
				GEditor->GetEditorSubsystem<UImportSubsystem>()->ImportNextTick(ExternalDragDropOp->GetFiles(), FolderPayload->GetInternalPath().ToString());
			}

			if (!ErrorMsg.IsEmpty())
			{
				AssetViewUtils::ShowErrorNotifcation(ErrorMsg);
			}

			return true; // We handled this drop, even if the result was invalid (eg, read-only folder)
		}
	}

	return false;
}

bool UContentBrowserAssetDataSource::TryGetCollectionId(const FContentBrowserItemData& InItem, FName& OutCollectionId)
{
	if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InItem))
	{
		OutCollectionId = AssetPayload->GetAssetData().ObjectPath;
		return true;
	}
	return false;
}

bool UContentBrowserAssetDataSource::Legacy_TryGetPackagePath(const FContentBrowserItemData& InItem, FName& OutPackagePath)
{
	if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(InItem))
	{
		OutPackagePath = FolderPayload->GetInternalPath();
		return true;
	}
	return false;
}

bool UContentBrowserAssetDataSource::Legacy_TryGetAssetData(const FContentBrowserItemData& InItem, FAssetData& OutAssetData)
{
	if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(InItem))
	{
		OutAssetData = AssetPayload->GetAssetData();
		return true;
	}
	return false;
}

bool UContentBrowserAssetDataSource::Legacy_TryConvertPackagePathToVirtualPath(const FName InPackagePath, FName& OutPath)
{
	return IsKnownContentPath(InPackagePath) // Ignore unknown content paths
		&& TryConvertInternalPathToVirtual(InPackagePath, OutPath);
}

bool UContentBrowserAssetDataSource::Legacy_TryConvertAssetDataToVirtualPath(const FAssetData& InAssetData, const bool InUseFolderPaths, FName& OutPath)
{
	return InAssetData.AssetClass != NAME_Class // Ignore legacy class items
		&& TryConvertInternalPathToVirtual(InUseFolderPaths ? InAssetData.PackagePath : InAssetData.ObjectPath, OutPath);
}

bool UContentBrowserAssetDataSource::IsKnownContentPath(const FName InPackagePath) const
{
	TStringBuilder<FName::StringBufferSize> PackagePathStr;
	InPackagePath.ToString(PackagePathStr);

	const FStringView PackagePathStrView = PackagePathStr;
	for (const FString& RootContentPath : RootContentPaths)
	{
		const FStringView RootContentPathNoSlash = FStringView(RootContentPath).LeftChop(1);
		if (PackagePathStrView.StartsWith(RootContentPath, ESearchCase::IgnoreCase) || PackagePathStrView.Equals(RootContentPathNoSlash, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

bool UContentBrowserAssetDataSource::IsRootContentPath(const FName InPackagePath) const
{
	TStringBuilder<FName::StringBufferSize> PackagePathStr;
	InPackagePath.ToString(PackagePathStr);
	PackagePathStr << TEXT('/'); // RootContentPaths have a trailing slash

	const FStringView PackagePathStrView = PackagePathStr;
	return RootContentPaths.ContainsByPredicate([&PackagePathStrView](const FString& InRootContentPath)
	{
		return PackagePathStrView == InRootContentPath;
	});
}

bool UContentBrowserAssetDataSource::GetObjectPathsForCollections(TArrayView<const FCollectionNameType> InCollections, const bool bIncludeChildCollections, TArray<FName>& OutObjectPaths) const
{
	if (InCollections.Num() > 0)
	{
		const ECollectionRecursionFlags::Flags CollectionRecursionMode = bIncludeChildCollections ? ECollectionRecursionFlags::SelfAndChildren : ECollectionRecursionFlags::Self;
		
		for (const FCollectionNameType& CollectionNameType : InCollections)
		{
			CollectionManager->GetObjectsInCollection(CollectionNameType.Name, CollectionNameType.Type, OutObjectPaths, CollectionRecursionMode);
		}

		return true;
	}

	return false;
}

FContentBrowserItemData UContentBrowserAssetDataSource::CreateAssetFolderItem(const FName InFolderPath)
{
	FName VirtualizedPath;
	TryConvertInternalPathToVirtual(InFolderPath, VirtualizedPath);

	return ContentBrowserAssetData::CreateAssetFolderItem(this, VirtualizedPath, InFolderPath);
}

FContentBrowserItemData UContentBrowserAssetDataSource::CreateAssetFileItem(const FAssetData& InAssetData)
{
	FName VirtualizedPath;
	TryConvertInternalPathToVirtual(InAssetData.ObjectPath, VirtualizedPath);

	return ContentBrowserAssetData::CreateAssetFileItem(this, VirtualizedPath, InAssetData);
}

TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> UContentBrowserAssetDataSource::GetAssetFolderItemPayload(const FContentBrowserItemData& InItem) const
{
	return ContentBrowserAssetData::GetAssetFolderItemPayload(this, InItem);
}

TSharedPtr<const FContentBrowserAssetFileItemDataPayload> UContentBrowserAssetDataSource::GetAssetFileItemPayload(const FContentBrowserItemData& InItem) const
{
	return ContentBrowserAssetData::GetAssetFileItemPayload(this, InItem);
}

void UContentBrowserAssetDataSource::OnAssetRegistryFileLoadProgress(const IAssetRegistry::FFileLoadProgressUpdateData& InProgressUpdateData)
{
	if (InProgressUpdateData.bIsDiscoveringAssetFiles)
	{
		DiscoveryStatusText = FText::Format(LOCTEXT("DiscoveringAssetFiles", "Discovering Asset Files: {0} files found."), InProgressUpdateData.NumTotalAssets);
	}
	else
	{
		float ProgressFraction = 0.0f;
		if (InProgressUpdateData.NumTotalAssets > 0)
		{
			ProgressFraction = InProgressUpdateData.NumAssetsProcessedByAssetRegistry / (float)InProgressUpdateData.NumTotalAssets;
		}

		if (InProgressUpdateData.NumAssetsPendingDataLoad > 0)
		{
			DiscoveryStatusText = FText::Format(LOCTEXT("DiscoveringAssetData", "Discovering Asset Data ({0}): {1} assets remaining."), FText::AsPercent(ProgressFraction), InProgressUpdateData.NumAssetsPendingDataLoad);
		}
		else
		{
			const int32 NumAssetsLeftToProcess = InProgressUpdateData.NumTotalAssets - InProgressUpdateData.NumAssetsProcessedByAssetRegistry;
			if (NumAssetsLeftToProcess == 0)
			{
				DiscoveryStatusText = FText();
			}
			else
			{
				DiscoveryStatusText = FText::Format(LOCTEXT("ProcessingAssetData", "Processing Asset Data ({0}): {1} assets remaining."), FText::AsPercent(ProgressFraction), NumAssetsLeftToProcess);
			}
		}
	}
}

void UContentBrowserAssetDataSource::OnAssetAdded(const FAssetData& InAssetData)
{
	if (ContentBrowserAssetData::IsPrimaryAsset(InAssetData))
	{
		// The owner folder of this asset is no longer considered empty
		OnPathPopulated(InAssetData.PackagePath);

		QueueItemDataUpdate(FContentBrowserItemDataUpdate::MakeItemAddedUpdate(CreateAssetFileItem(InAssetData)));
	}
}

void UContentBrowserAssetDataSource::OnAssetRemoved(const FAssetData& InAssetData)
{
	if (ContentBrowserAssetData::IsPrimaryAsset(InAssetData))
	{
		QueueItemDataUpdate(FContentBrowserItemDataUpdate::MakeItemRemovedUpdate(CreateAssetFileItem(InAssetData)));
	}
}

void UContentBrowserAssetDataSource::OnAssetRenamed(const FAssetData& InAssetData, const FString& InOldObjectPath)
{
	if (ContentBrowserAssetData::IsPrimaryAsset(InAssetData))
	{
		// The owner folder of this asset is no longer considered empty
		OnPathPopulated(InAssetData.PackagePath);

		FName VirtualizedPath;
		TryConvertInternalPathToVirtual(*InOldObjectPath, VirtualizedPath);

		QueueItemDataUpdate(FContentBrowserItemDataUpdate::MakeItemMovedUpdate(CreateAssetFileItem(InAssetData), VirtualizedPath));
	}
}

void UContentBrowserAssetDataSource::OnAssetUpdated(const FAssetData& InAssetData)
{
	if (ContentBrowserAssetData::IsPrimaryAsset(InAssetData))
	{
		QueueItemDataUpdate(FContentBrowserItemDataUpdate::MakeItemModifiedUpdate(CreateAssetFileItem(InAssetData)));
	}
}

void UContentBrowserAssetDataSource::OnAssetLoaded(UObject* InAsset)
{
	if (InAsset && !InAsset->GetOutermost()->HasAnyPackageFlags(PKG_ForDiffing) &&
		!UE::AssetRegistry::FFiltering::ShouldSkipAsset(InAsset))
	{
		FAssetData AssetData(InAsset);
		if (ContentBrowserAssetData::IsPrimaryAsset(AssetData))
		{
			QueueItemDataUpdate(FContentBrowserItemDataUpdate::MakeItemModifiedUpdate(CreateAssetFileItem(AssetData)));
		}
	}
}

void UContentBrowserAssetDataSource::OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InPropertyChangedEvent)
{
	if (InObject && InObject->IsAsset())
	{
		FAssetData AssetData(InObject);
		if (ContentBrowserAssetData::IsPrimaryAsset(AssetData))
		{
			QueueItemDataUpdate(FContentBrowserItemDataUpdate::MakeItemModifiedUpdate(CreateAssetFileItem(AssetData)));
		}
	}
}

void UContentBrowserAssetDataSource::OnPathAdded(const FString& InPath)
{
	// New paths are considered empty until assets are added inside them
	EmptyAssetFolders.Add(InPath);

	QueueItemDataUpdate(FContentBrowserItemDataUpdate::MakeItemAddedUpdate(CreateAssetFolderItem(*InPath)));
}

void UContentBrowserAssetDataSource::OnPathRemoved(const FString& InPath)
{
	// Deleted paths are no longer relevant for tracking
	AlwaysVisibleAssetFolders.Remove(InPath);
	EmptyAssetFolders.Remove(InPath);

	QueueItemDataUpdate(FContentBrowserItemDataUpdate::MakeItemRemovedUpdate(CreateAssetFolderItem(*InPath)));
}

void UContentBrowserAssetDataSource::OnPathPopulated(const FName InPath)
{
	TStringBuilder<FName::StringBufferSize> PathStr;
	InPath.ToString(PathStr);
	OnPathPopulated(FStringView(PathStr));
}

void UContentBrowserAssetDataSource::OnPathPopulated(const FStringView InPath)
{
	// Recursively un-hide this path, emitting update events for any paths that change state so that the view updates
	if (InPath.Len() > 1)
	{
		// Trim any trailing slash
		FStringView Path = InPath;
		if (Path[Path.Len() - 1] == TEXT('/'))
		{
			Path = Path.Left(Path.Len() - 1);
		}

		// Recurse first as we want parents to be updated before their children
		{
			int32 LastSlashIndex = INDEX_NONE;
			if (Path.FindLastChar(TEXT('/'), LastSlashIndex) && LastSlashIndex > 0)
			{
				OnPathPopulated(Path.Left(LastSlashIndex));
			}
		}

		// Unhide this folder and emit a notification if required
		const uint32 PathHash = GetTypeHash(Path);
		if (EmptyAssetFolders.RemoveByHash(PathHash, Path) > 0)
		{
			// Queue an update event for this path as it may have become visible in the view
			QueueItemDataUpdate(FContentBrowserItemDataUpdate::MakeItemModifiedUpdate(CreateAssetFolderItem(FName(Path))));
		}
	}
}

void UContentBrowserAssetDataSource::OnAlwaysShowPath(const FString& InPath)
{
	// Recursively force show this path, emitting update events for any paths that change state so that the view updates
	if (InPath.Len() > 1)
	{
		// Trim any trailing slash
		FString Path = InPath;
		if (Path[Path.Len() - 1] == TEXT('/'))
		{
			Path.LeftInline(Path.Len() - 1);
		}

		// Recurse first as we want parents to be updated before their children
		{
			int32 LastSlashIndex = INDEX_NONE;
			if (Path.FindLastChar(TEXT('/'), LastSlashIndex) && LastSlashIndex > 0)
			{
				OnAlwaysShowPath(Path.Left(LastSlashIndex));
			}
		}

		// Force show this folder and emit a notification if required
		if (!AlwaysVisibleAssetFolders.Contains(Path))
		{
			AlwaysVisibleAssetFolders.Add(Path);

			// Queue an update event for this path as it may have become visible in the view
			QueueItemDataUpdate(FContentBrowserItemDataUpdate::MakeItemModifiedUpdate(CreateAssetFolderItem(FName(*Path))));
		}
	}
}

void UContentBrowserAssetDataSource::OnScanCompleted()
{
	// Done finding content - compact this set as items would have been removed as assets were found
	EmptyAssetFolders.CompactStable();
}

void UContentBrowserAssetDataSource::OnContentPathMounted(const FString& InAssetPath, const FString& InFileSystemPath)
{
	RootContentPaths.AddUnique(InAssetPath);

	// Mount roots are always visible
	OnAlwaysShowPath(InAssetPath);
}

void UContentBrowserAssetDataSource::OnContentPathDismounted(const FString& InAssetPath, const FString& InFileSystemPath)
{
	RootContentPaths.Remove(InAssetPath);
}

void UContentBrowserAssetDataSource::PopulateAddNewContextMenu(UToolMenu* InMenu)
{
	const UContentBrowserDataMenuContext_AddNewMenu* ContextObject = InMenu->FindContext<UContentBrowserDataMenuContext_AddNewMenu>();
	checkf(ContextObject, TEXT("Required context UContentBrowserDataMenuContext_AddNewMenu was missing!"));

	// Extract the internal asset paths that belong to this data source from the full list of selected paths given in the context
	TArray<FName> SelectedAssetPaths;
	for (const FName& SelectedPath : ContextObject->SelectedPaths)
	{
		FName InternalPath;
		if (TryConvertVirtualPathToInternal(SelectedPath, InternalPath) && IsKnownContentPath(InternalPath))
		{
			SelectedAssetPaths.Add(InternalPath);
		}
	}

	// Only add the asset items if we have an asset path selected
	FNewAssetContextMenu::FOnNewAssetRequested OnNewAssetRequested;
	FNewAssetContextMenu::FOnImportAssetRequested OnImportAssetRequested;
	if (SelectedAssetPaths.Num() > 0)
	{
		OnImportAssetRequested = FNewAssetContextMenu::FOnImportAssetRequested::CreateUObject(this, &UContentBrowserAssetDataSource::OnImportAsset);
		if (ContextObject->OnBeginItemCreation.IsBound())
		{
			OnNewAssetRequested = FNewAssetContextMenu::FOnNewAssetRequested::CreateUObject(this, &UContentBrowserAssetDataSource::OnNewAssetRequested, ContextObject->OnBeginItemCreation);
		}
	}

	FNewAssetContextMenu::MakeContextMenu(
		InMenu,
		SelectedAssetPaths,
		OnImportAssetRequested,
		OnNewAssetRequested
		);
}

void UContentBrowserAssetDataSource::PopulateAssetFolderContextMenu(UToolMenu* InMenu)
{
	return ContentBrowserAssetData::PopulateAssetFolderContextMenu(this, InMenu, *AssetFolderContextMenu);
}

void UContentBrowserAssetDataSource::PopulateAssetFileContextMenu(UToolMenu* InMenu)
{
	return ContentBrowserAssetData::PopulateAssetFileContextMenu(this, InMenu, *AssetFileContextMenu);
}

void UContentBrowserAssetDataSource::PopulateDragDropContextMenu(UToolMenu* InMenu)
{
	const UContentBrowserDataMenuContext_DragDropMenu* ContextObject = InMenu->FindContext<UContentBrowserDataMenuContext_DragDropMenu>();
	checkf(ContextObject, TEXT("Required context UContentBrowserDataMenuContext_DragDropMenu was missing!"));

	FToolMenuSection& Section = InMenu->FindOrAddSection("MoveCopy");
	if (ContextObject->bCanCopy)
	{
		// Get the internal drop path
		FName DropAssetPath;
		{
			for (const FContentBrowserItemData& DropTargetItemData : ContextObject->DropTargetItem.GetInternalItems())
			{
				if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(DropTargetItemData))
				{
					DropAssetPath = FolderPayload->GetInternalPath();
					break;
				}
			}
		}

		// Extract the internal package paths that belong to this data source from the full list of selected items given in the context
		TArray<FName> AdvancedCopyInputs;
		for (const FContentBrowserItem& DraggedItem : ContextObject->DraggedItems)
		{
			for (const FContentBrowserItemData& DraggedItemData : DraggedItem.GetInternalItems())
			{
				if (TSharedPtr<const FContentBrowserAssetFileItemDataPayload> AssetPayload = GetAssetFileItemPayload(DraggedItemData))
				{
					AdvancedCopyInputs.Add(AssetPayload->GetAssetData().PackageName);
				}

				if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(DraggedItemData))
				{
					AdvancedCopyInputs.Add(FolderPayload->GetInternalPath());
				}
			}
		}

		if (!DropAssetPath.IsNone() && AdvancedCopyInputs.Num() > 0)
		{
			Section.AddMenuEntry(
				"DragDropAdvancedCopy",
				LOCTEXT("DragDropAdvancedCopy", "Advanced Copy Here"),
				LOCTEXT("DragDropAdvancedCopyTooltip", "Copy the dragged items and any specified dependencies to this folder, afterwards fixing up any dependencies on copied files to the new files."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, AdvancedCopyInputs, DestinationPath = DropAssetPath.ToString()]() { OnAdvancedCopyRequested(AdvancedCopyInputs, DestinationPath); }))
				);
		}
	}
}

void UContentBrowserAssetDataSource::OnAdvancedCopyRequested(const TArray<FName>& InAdvancedCopyInputs, const FString& InDestinationPath)
{
	AssetTools->BeginAdvancedCopyPackages(InAdvancedCopyInputs, InDestinationPath / FString());
}

void UContentBrowserAssetDataSource::OnImportAsset(const FName InPath)
{
	if (ensure(!InPath.IsNone()))
	{
		AssetTools->ImportAssetsWithDialog(InPath.ToString());
	}
}

void UContentBrowserAssetDataSource::OnNewAssetRequested(const FName InPath, TWeakObjectPtr<UClass> InFactoryClass, UContentBrowserDataMenuContext_AddNewMenu::FOnBeginItemCreation InOnBeginItemCreation)
{
	UClass* FactoryClass = InFactoryClass.Get();
	if (ensure(!InPath.IsNone()) && ensure(FactoryClass) && ensure(InOnBeginItemCreation.IsBound()))
	{
		UFactory* NewFactory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

		// This factory may get gc'd as a side effect of various delegates potentially calling CollectGarbage so protect against it from being gc'd out from under us
		FGCObjectScopeGuard FactoryGCGuard(NewFactory);

		FEditorDelegates::OnConfigureNewAssetProperties.Broadcast(NewFactory);
		if (NewFactory->ConfigureProperties())
		{
			FEditorDelegates::OnNewAssetCreated.Broadcast(NewFactory);

			FString DefaultAssetName;
			FString PackageNameToUse;
			AssetTools->CreateUniqueAssetName(InPath.ToString() / NewFactory->GetDefaultNewAssetName(), FString(), PackageNameToUse, DefaultAssetName);

			OnBeginCreateAsset(*DefaultAssetName, InPath, NewFactory->GetSupportedClass(), NewFactory, InOnBeginItemCreation);
		}
	}
}

void UContentBrowserAssetDataSource::OnBeginCreateAsset(const FName InDefaultAssetName, const FName InPackagePath, UClass* InAssetClass, UFactory* InFactory, UContentBrowserDataMenuContext_AddNewMenu::FOnBeginItemCreation InOnBeginItemCreation)
{
	if (!ensure(InOnBeginItemCreation.IsBound()))
	{
		return;
	}

	if (!ensure(InAssetClass || InFactory))
	{
		return;
	}

	if (InAssetClass && InFactory && !ensure(InAssetClass->IsChildOf(InFactory->GetSupportedClass())))
	{
		return;
	}

	UClass* ClassToUse = InAssetClass ? InAssetClass : (InFactory ? InFactory->GetSupportedClass() : nullptr);
	if (!ensure(ClassToUse))
	{
		return;
	}

	FAssetData NewAssetData(*(InPackagePath.ToString() / InDefaultAssetName.ToString()), InPackagePath, InDefaultAssetName, ClassToUse->GetFName());

	FName VirtualizedPath;
	TryConvertInternalPathToVirtual(NewAssetData.ObjectPath, VirtualizedPath);

	FContentBrowserItemData NewItemData(
		this, 
		EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Category_Asset | EContentBrowserItemFlags::Temporary_Creation,
		VirtualizedPath, 
		NewAssetData.AssetName,
		FText::AsCultureInvariant(NewAssetData.AssetName.ToString()),
		MakeShared<FContentBrowserAssetFileItemDataPayload_Creation>(MoveTemp(NewAssetData), InAssetClass, InFactory)
		);

	InOnBeginItemCreation.Execute(FContentBrowserItemDataTemporaryContext(
		MoveTemp(NewItemData), 
		FContentBrowserItemDataTemporaryContext::FOnValidateItem::CreateUObject(this, &UContentBrowserAssetDataSource::OnValidateItemName),
		FContentBrowserItemDataTemporaryContext::FOnFinalizeItem::CreateUObject(this, &UContentBrowserAssetDataSource::OnFinalizeCreateAsset)
		));
}

bool UContentBrowserAssetDataSource::OnValidateItemName(const FContentBrowserItemData& InItem, const FString& InProposedName, FText* OutErrorMsg)
{
	return CanRenameItem(InItem, &InProposedName, OutErrorMsg);
}

FContentBrowserItemData UContentBrowserAssetDataSource::OnFinalizeCreateFolder(const FContentBrowserItemData& InItemData, const FString& InProposedName, FText* OutErrorMsg)
{
	checkf(InItemData.GetOwnerDataSource() == this, TEXT("OnFinalizeCreateFolder was bound to an instance from the wrong data source!"));
	checkf(EnumHasAllFlags(InItemData.GetItemFlags(), EContentBrowserItemFlags::Type_Folder | EContentBrowserItemFlags::Temporary_Creation), TEXT("OnFinalizeCreateFolder called for an instance with the incorrect type flags!"));

	// Committed creation
	if (TSharedPtr<const FContentBrowserAssetFolderItemDataPayload> FolderPayload = GetAssetFolderItemPayload(InItemData))
	{
		const FString FolderPath = FPaths::GetPath(FolderPayload->GetInternalPath().ToString()) / InProposedName;

		FString NewPathOnDisk;
		if (FPackageName::TryConvertLongPackageNameToFilename(FolderPath, NewPathOnDisk) && IFileManager::Get().MakeDirectory(*NewPathOnDisk, true))
		{
			AssetRegistry->AddPath(FolderPath);
			AssetViewUtils::OnAlwaysShowPath().Broadcast(FolderPath);
			return CreateAssetFolderItem(*FolderPath);
		}
	}

	ContentBrowserAssetData::SetOptionalErrorMessage(OutErrorMsg, LOCTEXT("Error_FailedToCreateFolder", "Failed to create folder"));
	return FContentBrowserItemData();
}

FContentBrowserItemData UContentBrowserAssetDataSource::OnFinalizeCreateAsset(const FContentBrowserItemData& InItemData, const FString& InProposedName, FText* OutErrorMsg)
{
	checkf(InItemData.GetOwnerDataSource() == this, TEXT("OnFinalizeCreateAsset was bound to an instance from the wrong data source!"));
	checkf(EnumHasAllFlags(InItemData.GetItemFlags(), EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Temporary_Creation), TEXT("OnFinalizeCreateAsset called for an instance with the incorrect type flags!"));

	// Committed creation
	UObject* Asset = nullptr;
	{
		TSharedPtr<const FContentBrowserAssetFileItemDataPayload_Creation> CreationContext = StaticCastSharedPtr<const FContentBrowserAssetFileItemDataPayload_Creation>(InItemData.GetPayload());
		
		UClass* AssetClass = CreationContext->GetAssetClass();
		UFactory* Factory = CreationContext->GetFactory();
		
		if (AssetClass || Factory)
		{
			Asset = AssetTools->CreateAsset(InProposedName, CreationContext->GetAssetData().PackagePath.ToString(), AssetClass, Factory, FName("ContentBrowserNewAsset"));
		}
	}

	if (!Asset)
	{
		ContentBrowserAssetData::SetOptionalErrorMessage(OutErrorMsg, LOCTEXT("Error_FailedToCreateAsset", "Failed to create asset"));
		return FContentBrowserItemData();
	}

	return CreateAssetFileItem(FAssetData(Asset));
}

FContentBrowserItemData UContentBrowserAssetDataSource::OnFinalizeDuplicateAsset(const FContentBrowserItemData& InItemData, const FString& InProposedName, FText* OutErrorMsg)
{
	checkf(InItemData.GetOwnerDataSource() == this, TEXT("OnFinalizeDuplicateAsset was bound to an instance from the wrong data source!"));
	checkf(EnumHasAllFlags(InItemData.GetItemFlags(), EContentBrowserItemFlags::Type_File | EContentBrowserItemFlags::Temporary_Duplication), TEXT("OnFinalizeDuplicateAsset called for an instance with the incorrect type flags!"));

	// Committed duplication
	UObject* Asset = nullptr;
	{
		TSharedPtr<const FContentBrowserAssetFileItemDataPayload_Duplication> DuplicationContext = StaticCastSharedPtr<const FContentBrowserAssetFileItemDataPayload_Duplication>(InItemData.GetPayload());

		if (UObject* SourceObject = DuplicationContext->GetSourceObject())
		{
			Asset = AssetTools->DuplicateAsset(InProposedName, DuplicationContext->GetAssetData().PackagePath.ToString(), SourceObject);
		}
	}

	if (!Asset)
	{
		ContentBrowserAssetData::SetOptionalErrorMessage(OutErrorMsg, LOCTEXT("Error_FailedToCreateAsset", "Failed to create asset"));
		return FContentBrowserItemData();
	}

	return CreateAssetFileItem(FAssetData(Asset));
}

#undef LOCTEXT_NAMESPACE
