// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserClassDataSource.h"
#include "ContentBrowserClassDataCore.h"
#include "NativeClassHierarchy.h"
#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"
#include "CollectionManagerModule.h"
#include "ICollectionManager.h"
#include "Misc/StringBuilder.h"
#include "Misc/BlacklistNames.h"
#include "UObject/UObjectHash.h"
#include "ToolMenus.h"
#include "NewClassContextMenu.h"
#include "GameProjectGenerationModule.h"
#include "Framework/Docking/TabManager.h"
#include "ContentBrowserDataSubsystem.h"

#define LOCTEXT_NAMESPACE "ContentBrowserClassDataSource"

void UContentBrowserClassDataSource::Initialize(const FName InMountRoot, const bool InAutoRegister)
{
	Super::Initialize(InMountRoot, InAutoRegister);

	{
		static const FName NAME_AssetTools = "AssetTools";
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(NAME_AssetTools);
		ClassTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(UClass::StaticClass()).Pin();
	}

	CollectionManager = &FCollectionManagerModule::GetModule().Get();

	// Bind the class specific menu extensions
	{
		if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AddNewContextMenu"))
		{
			Menu->AddDynamicSection(*FString::Printf(TEXT("DynamicSection_DataSource_%s"), *GetName()), FNewToolMenuDelegate::CreateLambda([WeakThis = TWeakObjectPtr<UContentBrowserClassDataSource>(this)](UToolMenu* InMenu)
			{
				if (UContentBrowserClassDataSource* This = WeakThis.Get())
				{
					This->PopulateAddNewContextMenu(InMenu);
				}
			}));
		}
	}
}

void UContentBrowserClassDataSource::Shutdown()
{
	CollectionManager = nullptr;

	NativeClassHierarchy.Reset();

	Super::Shutdown();
}

void UContentBrowserClassDataSource::EnumerateRootPaths(const FContentBrowserDataFilter& InFilter, TFunctionRef<void(FName)> InCallback)
{
	TArray<FName> InternalRoots;
	NativeClassHierarchy->GetClassRoots(InternalRoots, EnumHasAnyFlags(InFilter.ItemAttributeFilter, EContentBrowserItemAttributeFilter::IncludeEngine), EnumHasAnyFlags(InFilter.ItemAttributeFilter, EContentBrowserItemAttributeFilter::IncludePlugins));

	for (const FName RootContentPath : InternalRoots)
	{
		InCallback(RootContentPath);
	}
}

void UContentBrowserClassDataSource::CompileFilter(const FName InPath, const FContentBrowserDataFilter& InFilter, FContentBrowserDataCompiledFilter& OutCompiledFilter)
{
	const FContentBrowserDataClassFilter* ClassFilter = InFilter.ExtraFilters.FindFilter<FContentBrowserDataClassFilter>();
	const FContentBrowserDataCollectionFilter* CollectionFilter = InFilter.ExtraFilters.FindFilter<FContentBrowserDataCollectionFilter>();

	const FBlacklistNames* ClassBlacklist = ClassFilter && ClassFilter->ClassBlacklist && ClassFilter->ClassBlacklist->HasFiltering() ? ClassFilter->ClassBlacklist.Get() : nullptr;

	const bool bIncludeFolders = EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFolders);
	const bool bIncludeFiles = EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFiles);

	const bool bIncludeClasses = EnumHasAnyFlags(InFilter.ItemCategoryFilter, EContentBrowserItemCategoryFilter::IncludeClasses);

	FContentBrowserDataFilterList& FilterList = OutCompiledFilter.CompiledFilters.FindOrAdd(this);
	FContentBrowserCompiledClassDataFilter& ClassDataFilter = FilterList.FindOrAddFilter<FContentBrowserCompiledClassDataFilter>();

	// If we aren't including anything, then we can just bail now
	if (!bIncludeClasses || (!bIncludeFolders && !bIncludeFiles))
	{
		return;
	}

	ConditionalCreateNativeClassHierarchy();

	// Convert the virtual path - if it doesn't exist in this data source then the filter won't include anything
	TSet<FName> InternalPaths;
	TMap<FName, TArray<FName>> VirtualPaths;
	FName SingleInternalPath;
	ExpandVirtualPath(InPath, InFilter, SingleInternalPath, InternalPaths, VirtualPaths);

	{
		FContentBrowserCompiledVirtualFolderFilter* VirtualFolderFilter = nullptr;
		for (const auto& It : VirtualPaths)
		{
			const FName VirtualSubPath = It.Key;
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
						VirtualFolderFilter->CachedSubPaths.Add(VirtualSubPath, CreateClassFolderItem(InternalPath));
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

	if (InternalPaths.Num() == 0)
	{
		return;
	}

	FNativeClassHierarchyFilter ClassHierarchyFilter;
	ClassHierarchyFilter.ClassPaths.Reserve(InternalPaths.Num());
	for (const FName InternalPath : InternalPaths)
	{
		ClassHierarchyFilter.ClassPaths.Add(InternalPath);
	}
	ClassHierarchyFilter.bRecursivePaths = InFilter.bRecursivePaths;
	
	// Roots need some special path handling
	static const FName RootPath = "/";
	if (InPath == RootPath)
	{
		TArray<FName> ClassRootFolders;
		NativeClassHierarchy->GetClassRoots(ClassRootFolders, EnumHasAnyFlags(InFilter.ItemAttributeFilter, EContentBrowserItemAttributeFilter::IncludeEngine), EnumHasAnyFlags(InFilter.ItemAttributeFilter, EContentBrowserItemAttributeFilter::IncludePlugins));

		ClassHierarchyFilter.ClassPaths.Reset();
		ClassHierarchyFilter.ClassPaths.Append(ClassRootFolders);

		if (bIncludeFolders)
		{
			ClassDataFilter.ValidFolders.Append(ClassRootFolders);
		}

		// Root paths never contain files, and we've already filled the initial folder list,
		// so we can stop now unless recursing into the root paths we just configured
		if (!ClassHierarchyFilter.bRecursivePaths)
		{
			return;
		}
	}

	// Find the child class folders
	if (bIncludeFolders && !ClassHierarchyFilter.IsEmpty())
	{
		TArray<FString> ChildClassFolders;
		NativeClassHierarchy->GetMatchingFolders(ClassHierarchyFilter, ChildClassFolders);

		for (const FString& ChildClassFolder : ChildClassFolders)
		{
			ClassDataFilter.ValidFolders.Add(*ChildClassFolder);
		}
	}

	// If we are filtering all classes, then we can bail now as we won't return any file items
	if ((ClassFilter && (ClassFilter->ClassNamesToInclude.Num() > 0 && !ClassFilter->ClassNamesToInclude.Contains(NAME_Class))) ||
		(ClassFilter && (ClassFilter->ClassNamesToExclude.Num() > 0 &&  ClassFilter->ClassNamesToExclude.Contains(NAME_Class))) ||
		(ClassBlacklist && (ClassBlacklist->IsBlacklistAll() || !ClassBlacklist->PassesFilter(NAME_Class)))
		)
	{
		return;
	}

	// Find the child class files
	if (bIncludeFiles && !ClassHierarchyFilter.IsEmpty())
	{
		TArray<UClass*> ChildClassObjects;
		NativeClassHierarchy->GetMatchingClasses(ClassHierarchyFilter, ChildClassObjects);

		if (ChildClassObjects.Num() > 0)
		{
			TSet<FName> ClassPathsToInclude;
			if (CollectionFilter)
			{
				TArray<FName> ClassPathsForCollections;
				if (GetClassPathsForCollections(CollectionFilter->SelectedCollections, CollectionFilter->bIncludeChildCollections, ClassPathsForCollections) && ClassPathsForCollections.Num() == 0)
				{
					// If we had collections but they contained no classes then we can bail as nothing will pass the filter
					return;
				}

				ClassPathsToInclude.Append(ClassPathsForCollections);
			}

			for (UClass* ChildClassObject : ChildClassObjects)
			{
				const bool bPassesInclusiveFilter = ClassPathsToInclude.Num() == 0 || ClassPathsToInclude.Contains(*ChildClassObject->GetPathName());
				const bool bPassesBlacklistFilter = !ClassBlacklist || ClassBlacklist->PassesFilter(ChildClassObject->GetFName());

				if (bPassesInclusiveFilter && bPassesBlacklistFilter)
				{
					ClassDataFilter.ValidClasses.Add(ChildClassObject);
				}
			}
		}
	}
}

void UContentBrowserClassDataSource::EnumerateItemsMatchingFilter(const FContentBrowserDataCompiledFilter& InFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback)
{
	const FContentBrowserDataFilterList* FilterList = InFilter.CompiledFilters.Find(this);
	if (!FilterList)
	{
		return;
	}
	
	const FContentBrowserCompiledClassDataFilter* ClassDataFilter = FilterList->FindFilter<FContentBrowserCompiledClassDataFilter>();
	if (!ClassDataFilter)
	{
		return;
	}

	if (EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFolders))
	{
		for (const FName& ValidFolder : ClassDataFilter->ValidFolders)
		{
			if (!InCallback(CreateClassFolderItem(ValidFolder)))
			{
				return;
			}
		}
	}

	if (EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFiles))
	{
		for (UClass* ValidClass : ClassDataFilter->ValidClasses)
		{
			if (!InCallback(CreateClassFileItem(ValidClass)))
			{
				return;
			}
		}
	}
}

void UContentBrowserClassDataSource::EnumerateItemsAtPath(const FName InPath, const EContentBrowserItemTypeFilter InItemTypeFilter, TFunctionRef<bool(FContentBrowserItemData&&)> InCallback)
{
	FName InternalPath;
	if (!TryConvertVirtualPathToInternal(InPath, InternalPath))
	{
		return;
	}
	
	ConditionalCreateNativeClassHierarchy();

	if (EnumHasAnyFlags(InItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFolders))
	{
		if (TSharedPtr<const FNativeClassHierarchyNode> FolderNode = NativeClassHierarchy->FindNode(InternalPath, ENativeClassHierarchyNodeType::Folder))
		{
			InCallback(CreateClassFolderItem(InternalPath));
		}
	}

	if (EnumHasAnyFlags(InItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFiles))
	{
		if (TSharedPtr<const FNativeClassHierarchyNode> ClassNode = NativeClassHierarchy->FindNode(InternalPath, ENativeClassHierarchyNodeType::Class))
		{
			InCallback(CreateClassFileItem(ClassNode->Class));
		}
	}
}

bool UContentBrowserClassDataSource::IsFolderVisibleIfHidingEmpty(const FName InPath)
{
	FName InternalPath;
	if (!TryConvertVirtualPathToInternal(InPath, InternalPath))
	{
		return false;
	}

	if (!IsKnownClassPath(InternalPath))
	{
		return false;
	}

	ConditionalCreateNativeClassHierarchy();

	return ContentBrowserClassData::IsTopLevelFolder(InternalPath) 
		|| NativeClassHierarchy->HasClasses(InternalPath, /*bRecursive*/true);
}

bool UContentBrowserClassDataSource::DoesItemPassFilter(const FContentBrowserItemData& InItem, const FContentBrowserDataCompiledFilter& InFilter)
{
	const FContentBrowserDataFilterList* FilterList = InFilter.CompiledFilters.Find(this);
	if (!FilterList)
	{
		return false;
	}

	const FContentBrowserCompiledClassDataFilter* ClassDataFilter = FilterList->FindFilter<FContentBrowserCompiledClassDataFilter>();
	if (!ClassDataFilter)
	{
		return false;
	}

	switch (InItem.GetItemType())
	{
	case EContentBrowserItemFlags::Type_Folder:
		if (EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFolders) && ClassDataFilter->ValidFolders.Num() > 0)
		{
			if (TSharedPtr<const FContentBrowserClassFolderItemDataPayload> FolderPayload = GetClassFolderItemPayload(InItem))
			{
				return ClassDataFilter->ValidFolders.Contains(FolderPayload->GetInternalPath());
			}
		}
		break;

	case EContentBrowserItemFlags::Type_File:
		if (EnumHasAnyFlags(InFilter.ItemTypeFilter, EContentBrowserItemTypeFilter::IncludeFiles) && ClassDataFilter->ValidClasses.Num() > 0)
		{
			if (TSharedPtr<const FContentBrowserClassFileItemDataPayload> ClassPayload = GetClassFileItemPayload(InItem))
			{
				return ClassDataFilter->ValidClasses.Contains(ClassPayload->GetClass());
			}
		}
		break;

	default:
		break;
	}
	
	return false;
}

bool UContentBrowserClassDataSource::GetItemAttribute(const FContentBrowserItemData& InItem, const bool InIncludeMetaData, const FName InAttributeKey, FContentBrowserItemDataAttributeValue& OutAttributeValue)
{
	return ContentBrowserClassData::GetItemAttribute(ClassTypeActions.Get(), this, InItem, InIncludeMetaData, InAttributeKey, OutAttributeValue);
}

bool UContentBrowserClassDataSource::GetItemAttributes(const FContentBrowserItemData& InItem, const bool InIncludeMetaData, FContentBrowserItemDataAttributeValues& OutAttributeValues)
{
	return ContentBrowserClassData::GetItemAttributes(this, InItem, InIncludeMetaData, OutAttributeValues);
}

bool UContentBrowserClassDataSource::GetItemPhysicalPath(const FContentBrowserItemData& InItem, FString& OutDiskPath)
{
	return ContentBrowserClassData::GetItemPhysicalPath(this, InItem, OutDiskPath);
}

bool UContentBrowserClassDataSource::CanEditItem(const FContentBrowserItemData& InItem, FText* OutErrorMsg)
{
	return ContentBrowserClassData::CanEditItem(this, InItem, OutErrorMsg);
}

bool UContentBrowserClassDataSource::EditItem(const FContentBrowserItemData& InItem)
{
	return ContentBrowserClassData::EditItems(ClassTypeActions.Get(), this, MakeArrayView(&InItem, 1));
}

bool UContentBrowserClassDataSource::BulkEditItems(TArrayView<const FContentBrowserItemData> InItems)
{
	return ContentBrowserClassData::EditItems(ClassTypeActions.Get(), this, InItems);
}

bool UContentBrowserClassDataSource::AppendItemReference(const FContentBrowserItemData& InItem, FString& InOutStr)
{
	return ContentBrowserClassData::AppendItemReference(this, InItem, InOutStr);
}

bool UContentBrowserClassDataSource::UpdateThumbnail(const FContentBrowserItemData& InItem, FAssetThumbnail& InThumbnail)
{
	return ContentBrowserClassData::UpdateItemThumbnail(this, InItem, InThumbnail);
}

bool UContentBrowserClassDataSource::TryGetCollectionId(const FContentBrowserItemData& InItem, FName& OutCollectionId)
{
	if (TSharedPtr<const FContentBrowserClassFileItemDataPayload> ClassPayload = GetClassFileItemPayload(InItem))
	{
		OutCollectionId = ClassPayload->GetAssetData().ObjectPath;
		return true;
	}
	return false;
}

bool UContentBrowserClassDataSource::Legacy_TryGetPackagePath(const FContentBrowserItemData& InItem, FName& OutPackagePath)
{
	if (TSharedPtr<const FContentBrowserClassFolderItemDataPayload> FolderPayload = GetClassFolderItemPayload(InItem))
	{
		OutPackagePath = FolderPayload->GetInternalPath();
		return true;
	}
	return false;
}

bool UContentBrowserClassDataSource::Legacy_TryGetAssetData(const FContentBrowserItemData& InItem, FAssetData& OutAssetData)
{
	if (TSharedPtr<const FContentBrowserClassFileItemDataPayload> ClassPayload = GetClassFileItemPayload(InItem))
	{
		OutAssetData = ClassPayload->GetAssetData();
		return true;
	}
	return false;
}

bool UContentBrowserClassDataSource::Legacy_TryConvertPackagePathToVirtualPath(const FName InPackagePath, FName& OutPath)
{
	return IsKnownClassPath(InPackagePath) // Ignore non-class paths
		&& TryConvertInternalPathToVirtual(InPackagePath, OutPath);
}

bool UContentBrowserClassDataSource::Legacy_TryConvertAssetDataToVirtualPath(const FAssetData& InAssetData, const bool InUseFolderPaths, FName& OutPath)
{
	return InAssetData.AssetClass == NAME_Class // Ignore non-class class items
		&& TryConvertInternalPathToVirtual(InUseFolderPaths ? InAssetData.PackagePath : InAssetData.ObjectPath, OutPath);
}

bool UContentBrowserClassDataSource::IsKnownClassPath(const FName InPackagePath) const
{
	TStringBuilder<FName::StringBufferSize> PackagePathStr;
	InPackagePath.ToString(PackagePathStr);

	return FStringView(PackagePathStr).StartsWith(TEXT("/Classes_"));
}

bool UContentBrowserClassDataSource::GetClassPathsForCollections(TArrayView<const FCollectionNameType> InCollections, const bool bIncludeChildCollections, TArray<FName>& OutClassPaths)
{
	if (InCollections.Num() > 0)
	{
		const ECollectionRecursionFlags::Flags CollectionRecursionMode = bIncludeChildCollections ? ECollectionRecursionFlags::SelfAndChildren : ECollectionRecursionFlags::Self;
		
		for (const FCollectionNameType& CollectionNameType : InCollections)
		{
			CollectionManager->GetClassesInCollection(CollectionNameType.Name, CollectionNameType.Type, OutClassPaths, CollectionRecursionMode);
		}

		return true;
	}

	return false;
}

FContentBrowserItemData UContentBrowserClassDataSource::CreateClassFolderItem(const FName InFolderPath)
{
	FName VirtualizedPath;
	TryConvertInternalPathToVirtual(InFolderPath, VirtualizedPath);

	return ContentBrowserClassData::CreateClassFolderItem(this, VirtualizedPath, InFolderPath);
}

FContentBrowserItemData UContentBrowserClassDataSource::CreateClassFileItem(UClass* InClass)
{
	ConditionalCreateNativeClassHierarchy();

	FName ClassPath;
	{
		FString ClassPathStr;
		const bool bValidClassPath = NativeClassHierarchy->GetClassPath(InClass, ClassPathStr);
		checkf(bValidClassPath, TEXT("GetClassPath failed to return a result for '%s'"), *InClass->GetPathName());
		ClassPath = *ClassPathStr;
	}

	FName VirtualizedPath;
	TryConvertInternalPathToVirtual(ClassPath, VirtualizedPath);

	return ContentBrowserClassData::CreateClassFileItem(this, VirtualizedPath, ClassPath, InClass);
}

TSharedPtr<const FContentBrowserClassFolderItemDataPayload> UContentBrowserClassDataSource::GetClassFolderItemPayload(const FContentBrowserItemData& InItem) const
{
	return ContentBrowserClassData::GetClassFolderItemPayload(this, InItem);
}

TSharedPtr<const FContentBrowserClassFileItemDataPayload> UContentBrowserClassDataSource::GetClassFileItemPayload(const FContentBrowserItemData& InItem) const
{
	return ContentBrowserClassData::GetClassFileItemPayload(this, InItem);
}

void UContentBrowserClassDataSource::PopulateAddNewContextMenu(UToolMenu* InMenu)
{
	const UContentBrowserDataMenuContext_AddNewMenu* ContextObject = InMenu->FindContext<UContentBrowserDataMenuContext_AddNewMenu>();
	checkf(ContextObject, TEXT("Required context UContentBrowserDataMenuContext_AddNewMenu was missing!"));

	// Extract the internal class paths that belong to this data source from the full list of selected paths given in the context
	TArray<FName> SelectedClassPaths;
	for (const FName& SelectedPath : ContextObject->SelectedPaths)
	{
		FName InternalPath;
		if (TryConvertVirtualPathToInternal(SelectedPath, InternalPath) && IsKnownClassPath(InternalPath))
		{
			SelectedClassPaths.Add(InternalPath);
		}
	}

	// Only add the asset items if we have a class path selected
	FNewClassContextMenu::FOnNewClassRequested OnNewClassRequested;
	if (SelectedClassPaths.Num() > 0)
	{
		OnNewClassRequested = FNewClassContextMenu::FOnNewClassRequested::CreateUObject(this, &UContentBrowserClassDataSource::OnNewClassRequested);
	}

	FNewClassContextMenu::MakeContextMenu(
		InMenu,
		SelectedClassPaths,
		OnNewClassRequested
		);
}

void UContentBrowserClassDataSource::OnNewClassRequested(const FName InSelectedPath)
{
	ConditionalCreateNativeClassHierarchy();

	// Parse out the on disk location for the currently selected path, this will then be used as the default location for the new class (if a valid project module location)
	FString ExistingFolderPath;
	if (!InSelectedPath.IsNone())
	{
		NativeClassHierarchy->GetFileSystemPath(InSelectedPath.ToString(), ExistingFolderPath);
	}

	FGameProjectGenerationModule::Get().OpenAddCodeToProjectDialog(
		FAddToProjectConfig()
		.InitialPath(ExistingFolderPath)
		.ParentWindow(FGlobalTabmanager::Get()->GetRootWindow())
		);
}

void UContentBrowserClassDataSource::ConditionalCreateNativeClassHierarchy()
{
	if (!NativeClassHierarchy)
	{
		NativeClassHierarchy = MakeShared<FNativeClassHierarchy>();
		NativeClassHierarchy->OnClassHierarchyUpdated().AddUObject(this, &UContentBrowserClassDataSource::NotifyItemDataRefreshed);
	}
}

#undef LOCTEXT_NAMESPACE
