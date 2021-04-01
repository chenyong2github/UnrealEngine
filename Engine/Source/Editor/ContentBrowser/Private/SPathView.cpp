// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPathView.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SSeparator.h"
#include "EditorStyleSet.h"
#include "Settings/ContentBrowserSettings.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "ContentBrowserSingleton.h"
#include "ContentBrowserUtils.h"
#include "ContentBrowserLog.h"
#include "HistoryManager.h"

#include "DragAndDrop/AssetDragDropOp.h"
#include "DragDropHandler.h"

#include "PathViewTypes.h"
#include "SourcesSearch.h"
#include "SourcesViewWidgets.h"
#include "Widgets/Input/SSearchBox.h"
#include "ContentBrowserModule.h"
#include "Misc/BlacklistNames.h"

#include "IContentBrowserDataModule.h"
#include "ContentBrowserDataSource.h"
#include "ContentBrowserDataSubsystem.h"

#include "Application/SlateApplicationBase.h"
#include "ToolMenus.h"
#include <Misc/PathViews.h>

#define LOCTEXT_NAMESPACE "ContentBrowser"

SPathView::FScopedSelectionChangedEvent::FScopedSelectionChangedEvent(const TSharedRef<SPathView>& InPathView, const bool InShouldEmitEvent)
	: PathView(InPathView)
	, bShouldEmitEvent(InShouldEmitEvent)
{
	PathView->PreventTreeItemChangedDelegateCount++;
	InitialSelectionSet = GetSelectionSet();
}

SPathView::FScopedSelectionChangedEvent::~FScopedSelectionChangedEvent()
{
	check(PathView->PreventTreeItemChangedDelegateCount > 0);
	PathView->PreventTreeItemChangedDelegateCount--;

	if (bShouldEmitEvent)
	{
		const TSet<FName> FinalSelectionSet = GetSelectionSet();
		const bool bHasSelectionChanges = InitialSelectionSet.Num() != FinalSelectionSet.Num() || InitialSelectionSet.Difference(FinalSelectionSet).Num() > 0;
		if (bHasSelectionChanges)
		{
			const TArray<TSharedPtr<FTreeItem>> SelectedItems = PathView->TreeViewPtr->GetSelectedItems();
			PathView->TreeSelectionChanged(SelectedItems.Num() > 0 ? SelectedItems[0] : nullptr, ESelectInfo::Direct);
		}
	}
}

TSet<FName> SPathView::FScopedSelectionChangedEvent::GetSelectionSet() const
{
	TSet<FName> SelectionSet;

	const TArray<TSharedPtr<FTreeItem>> SelectedItems = PathView->TreeViewPtr->GetSelectedItems();
	for (const TSharedPtr<FTreeItem>& Item : SelectedItems)
	{
		if (ensure(Item.IsValid()))
		{
			SelectionSet.Add(Item->GetItem().GetVirtualPath());
		}
	}

	return SelectionSet;
}

SPathView::~SPathView()
{
	if (IContentBrowserDataModule* ContentBrowserDataModule = IContentBrowserDataModule::GetPtr())
	{
		if (UContentBrowserDataSubsystem* ContentBrowserData = ContentBrowserDataModule->GetSubsystem())
		{
			ContentBrowserData->OnItemDataUpdated().RemoveAll(this);
			ContentBrowserData->OnItemDataRefreshed().RemoveAll(this);
			ContentBrowserData->OnItemDataDiscoveryComplete().RemoveAll(this);
		}
	}

	SearchBoxFolderFilter->OnChanged().RemoveAll( this );
}

void SPathView::Construct( const FArguments& InArgs )
{
	OnItemSelectionChanged = InArgs._OnItemSelectionChanged;
	bAllowContextMenu = InArgs._AllowContextMenu;
	OnGetItemContextMenu = InArgs._OnGetItemContextMenu;
	InitialCategoryFilter = InArgs._InitialCategoryFilter;
	bAllowClassesFolder = InArgs._AllowClassesFolder;
	bAllowReadOnlyFolders = InArgs._AllowReadOnlyFolders;
	PreventTreeItemChangedDelegateCount = 0;
	TreeTitle = LOCTEXT("AssetTreeTitle", "Asset Tree");
	if ( InArgs._FocusSearchBoxWhenOpened )
	{
		RegisterActiveTimer( 0.f, FWidgetActiveTimerDelegate::CreateSP( this, &SPathView::SetFocusPostConstruct ) );
	}

	UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
	ContentBrowserData->OnItemDataUpdated().AddSP(this, &SPathView::HandleItemDataUpdated);
	ContentBrowserData->OnItemDataRefreshed().AddSP(this, &SPathView::HandleItemDataRefreshed);
	ContentBrowserData->OnItemDataDiscoveryComplete().AddSP(this, &SPathView::HandleItemDataDiscoveryComplete);

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	FolderBlacklist = AssetToolsModule.Get().GetFolderBlacklist();
	WritableFolderBlacklist = AssetToolsModule.Get().GetWritableFolderBlacklist();

	// Listen for when view settings are changed
	UContentBrowserSettings::OnSettingChanged().AddSP(this, &SPathView::HandleSettingChanged);

	//Setup the SearchBox filter
	SearchBoxFolderFilter = MakeShareable( new FolderTextFilter( FolderTextFilter::FItemToStringArray::CreateSP( this, &SPathView::PopulateFolderSearchStrings ) ) );
	SearchBoxFolderFilter->OnChanged().AddSP( this, &SPathView::FilterUpdated );

	// Setup plugin filters
	PluginPathFilters = InArgs._PluginPathFilters;
	if (PluginPathFilters.IsValid())
	{
		// Add all built-in filters here
		AllPluginPathFilters.Add( MakeShareable(new FContentBrowserPluginFilter_ContentOnlyPlugins()) );

		// Add external filters
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		for (const FContentBrowserModule::FAddPathViewPluginFilters& Delegate : ContentBrowserModule.GetAddPathViewPluginFilters())
		{
			if (Delegate.IsBound())
			{
				Delegate.Execute(AllPluginPathFilters);
			}
		}

		for (const TSharedRef<FContentBrowserPluginFilter>& Filter : AllPluginPathFilters)
		{
			SetPluginPathFilterActive(Filter, false);
		}
	}

	if (!TreeViewPtr.IsValid())
	{
		SAssignNew(TreeViewPtr, STreeView< TSharedPtr<FTreeItem> >)
			.TreeItemsSource(&TreeRootItems)
			.OnGenerateRow(this, &SPathView::GenerateTreeRow)
			.OnItemScrolledIntoView(this, &SPathView::TreeItemScrolledIntoView)
			.ItemHeight(18)
			.SelectionMode(InArgs._SelectionMode)
			.OnSelectionChanged(this, &SPathView::TreeSelectionChanged)
			.OnExpansionChanged(this, &SPathView::TreeExpansionChanged)
			.OnGetChildren(this, &SPathView::GetChildrenForTree)
			.OnSetExpansionRecursive(this, &SPathView::SetTreeItemExpansionRecursive)
			.OnContextMenuOpening(this, &SPathView::MakePathViewContextMenu)
			.ClearSelectionOnClick(false)
			.HighlightParentNodesForSelection(true);
	}

	SearchPtr = InArgs._ExternalSearch;
	if (!SearchPtr)
	{
		SearchPtr = MakeShared<FSourcesSearch>();
		SearchPtr->Initialize();
		SearchPtr->SetHintText(LOCTEXT("AssetTreeSearchBoxHint", "Search Folders"));
	}
	SearchPtr->OnSearchChanged().AddSP(this, &SPathView::SetSearchFilterText);

	TSharedRef<SBox> SearchBox = SNew(SBox);
	if (!InArgs._ExternalSearch)
	{
		SearchBox->SetPadding(FMargin(0, 1, 0, 3));

		SearchBox->SetContent(
			SNew(SHorizontalBox)

			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				InArgs._SearchContent.Widget
			]

			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SBox)
				.Visibility(InArgs._SearchBarVisibility)
				[
					SearchPtr->GetWidget()
				]
			]
		);
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		// Search
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SearchBox
		]

		// Tree title
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Font( FEditorStyle::GetFontStyle("ContentBrowser.SourceTitleFont") )
			.Text(this, &SPathView::GetTreeTitle)
			.Visibility(InArgs._ShowTreeTitle ? EVisibility::Visible : EVisibility::Collapsed)
		]

		// Separator
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 1)
		[
			SNew(SSeparator)
			.Visibility( ( InArgs._ShowSeparator) ? EVisibility::Visible : EVisibility::Collapsed )
		]
			
		// Tree
		+SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			TreeViewPtr.ToSharedRef()
		]
	];

	// Add all paths currently gathered from the asset registry
	Populate();

	// Always expand the game root initially
	static const FName GameRootName = TEXT("Game");
	for ( auto RootIt = TreeRootItems.CreateConstIterator(); RootIt; ++RootIt )
	{
		if ( (*RootIt)->GetItem().GetItemName() == GameRootName )
		{
			TreeViewPtr->SetItemExpansion(*RootIt, true);
		}
	}
}

void SPathView::PopulatePathViewFiltersMenu(UToolMenu* Menu)
{
	{
		FToolMenuSection& Section = Menu->AddSection("Reset");
		Section.AddMenuEntry(
			"ResetPluginPathFilters",
			LOCTEXT("ResetPluginPathFilters_Label", "Reset Path View Filters"),
			LOCTEXT("ResetPluginPathFilters_Tooltip", "Reset current path view filters state"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SPathView::ResetPluginPathFilters))
		);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("Filters", LOCTEXT("PathViewFilters_Label", "Filters"));

		for (const TSharedRef<FContentBrowserPluginFilter>& Filter : AllPluginPathFilters)
		{
			Section.AddMenuEntry(
				NAME_None,
				Filter->GetDisplayName(),
				Filter->GetToolTipText(),
				FSlateIcon(FEditorStyle::GetStyleSetName(), Filter->GetIconName()),
				FUIAction(
					FExecuteAction::CreateSP(this, &SPathView::PluginPathFilterClicked, Filter),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &SPathView::IsPluginPathFilterInUse, Filter)
				),
				EUserInterfaceActionType::ToggleButton
			);
		}
	}
}

void SPathView::PluginPathFilterClicked(TSharedRef<FContentBrowserPluginFilter> Filter)
{
	SetPluginPathFilterActive(Filter, !IsPluginPathFilterInUse(Filter));
	Populate();
}

bool SPathView::IsPluginPathFilterInUse(TSharedRef<FContentBrowserPluginFilter> Filter) const
{
	for (int32 i=0; i < PluginPathFilters->Num(); ++i)
	{
		if (PluginPathFilters->GetFilterAtIndex(i) == Filter)
		{
			return true;
		}
	}

	return false;
}

void SPathView::ResetPluginPathFilters()
{
	for (const TSharedRef<FContentBrowserPluginFilter>& Filter : AllPluginPathFilters)
	{
		SetPluginPathFilterActive(Filter, false);
	}

	Populate();
}

void SPathView::SetPluginPathFilterActive(const TSharedRef<FContentBrowserPluginFilter>& Filter, bool bActive)
{
	if (Filter->IsInverseFilter())
	{
		//Inverse filters are active when they are "disabled"
		bActive = !bActive;
	}

	Filter->ActiveStateChanged(bActive);

	if (bActive)
	{
		PluginPathFilters->Add(Filter);
	}
	else
	{
		PluginPathFilters->Remove(Filter);
	}
}

void SPathView::SetSelectedPaths(const TArray<FString>& Paths)
{
	if ( !ensure(TreeViewPtr.IsValid()) )
	{
		return;
	}

	// Clear the search box if it potentially hides a path we want to select
	for (const FString& Path : Paths)
	{
		if (PathIsFilteredFromViewBySearch(Path))
		{
			SearchPtr->ClearSearch();
			break;
		}
	}

	// Prevent the selection changed delegate since the invoking code requested it
	FScopedPreventTreeItemChangedDelegate DelegatePrevention( SharedThis(this) );

	// If the selection was changed before all pending initial paths were found, stop attempting to select them
	PendingInitialPaths.Empty();

	// Clear the selection to start, then add the selected paths as they are found
	LastSelectedPaths.Empty();
	TreeViewPtr->ClearSelection();

	for (int32 PathIdx = 0; PathIdx < Paths.Num(); ++PathIdx)
	{
		const FString& Path = Paths[PathIdx];

		TArray<FName> PathItemList;
		{
			TArray<FString> PathItemListStr;
			Path.ParseIntoArray(PathItemListStr, TEXT("/"), /*InCullEmpty=*/true);

			PathItemList.Reserve(PathItemListStr.Num());
			for (const FString& PathItemName : PathItemListStr)
			{
				PathItemList.Add(*PathItemName);
			}
		}

		if ( PathItemList.Num() )
		{
			// There is at least one element in the path
			TArray<TSharedPtr<FTreeItem>> TreeItems;

			// Find the first item in the root items list
			for ( int32 RootItemIdx = 0; RootItemIdx < TreeRootItems.Num(); ++RootItemIdx )
			{
				if ( TreeRootItems[RootItemIdx]->GetItem().GetItemName() == PathItemList[0] )
				{
					// Found the first item in the path
					TreeItems.Add(TreeRootItems[RootItemIdx]);
					break;
				}
			}

			// If found in the root items list, try to find the childmost item matching the path
			if ( TreeItems.Num() > 0 )
			{
				for ( int32 PathItemIdx = 1; PathItemIdx < PathItemList.Num(); ++PathItemIdx )
				{
					const FName PathItemName = PathItemList[PathItemIdx];
					const TSharedPtr<FTreeItem> ChildItem = TreeItems.Last()->GetChild(PathItemName);

					if ( ChildItem.IsValid() )
					{
						// Update tree items list
						TreeItems.Add(ChildItem);
					}
					else
					{
						// Could not find the child item
						break;
					}
				}

				// Expand all the tree folders up to but not including the last one.
				for (int32 ItemIdx = 0; ItemIdx < TreeItems.Num() - 1; ++ItemIdx)
				{
					TreeViewPtr->SetItemExpansion(TreeItems[ItemIdx], true);
				}

				// Set the selection to the closest found folder and scroll it into view
				LastSelectedPaths.Add(TreeItems.Last()->GetItem().GetVirtualPath());
				TreeViewPtr->SetItemSelection(TreeItems.Last(), true);
				TreeViewPtr->RequestScrollIntoView(TreeItems.Last());
			}
			else
			{
				// Could not even find the root path... skip
			}
		}
		else
		{
			// No path items... skip
		}
	}
}

void SPathView::ClearSelection()
{
	// Prevent the selection changed delegate since the invoking code requested it
	FScopedPreventTreeItemChangedDelegate DelegatePrevention( SharedThis(this) );

	// If the selection was changed before all pending initial paths were found, stop attempting to select them
	PendingInitialPaths.Empty();

	// Clear the selection to start, then add the selected paths as they are found
	TreeViewPtr->ClearSelection();
}

FString SPathView::GetSelectedPath() const
{
	// TODO: Abstract away?
	TArray<TSharedPtr<FTreeItem>> Items = TreeViewPtr->GetSelectedItems();
	if ( Items.Num() > 0 )
	{
		return Items[0]->GetItem().GetVirtualPath().ToString();
	}

	return FString();
}

TArray<FString> SPathView::GetSelectedPaths() const
{
	TArray<FString> RetArray;

	// TODO: Abstract away?
	TArray<TSharedPtr<FTreeItem>> Items = TreeViewPtr->GetSelectedItems();
	for ( int32 ItemIdx = 0; ItemIdx < Items.Num(); ++ItemIdx )
	{
		RetArray.Add(Items[ItemIdx]->GetItem().GetVirtualPath().ToString());
	}

	return RetArray;
}

TArray<FContentBrowserItem> SPathView::GetSelectedFolderItems() const
{
	TArray<TSharedPtr<FTreeItem>> SelectedViewItems = TreeViewPtr->GetSelectedItems();

	TArray<FContentBrowserItem> SelectedFolders;
	for (const TSharedPtr<FTreeItem>& SelectedViewItem : SelectedViewItems)
	{
		if (!SelectedViewItem->GetItem().IsTemporary())
		{
			SelectedFolders.Emplace(SelectedViewItem->GetItem());
		}
	}
	return SelectedFolders;
}

TSharedPtr<FTreeItem> SPathView::AddFolderItem(FContentBrowserItemData&& InItem, const bool bUserNamed)
{
	if (!ensure(TreeViewPtr.IsValid()))
	{
		// No tree view for some reason
		return nullptr;
	}

	if (!InItem.IsFolder())
	{
		// Not a folder
		return nullptr;
	}

	// The path view will add a node for each level of the path tree
	TArray<FString> PathItemList;
	InItem.GetVirtualPath().ToString().ParseIntoArray(PathItemList, TEXT("/"), /*InCullEmpty=*/true);

	// Start at the root and work down until all required children have been added
	TSharedPtr<FTreeItem> ParentTreeItem;
	TArray<TSharedPtr<FTreeItem>>* CurrentTreeItems = &TreeRootItems;

	TStringBuilder<512> CurrentPathStr;
	CurrentPathStr.Append(TEXT("/"));
	for (int32 PathItemIndex = 0; PathItemIndex < PathItemList.Num(); ++PathItemIndex)
	{
		const bool bIsLeafmostItem = PathItemIndex == PathItemList.Num() - 1;

		const FString FolderNameStr = PathItemList[PathItemIndex];
		const FName FolderName = *FolderNameStr;
		FPathViews::Append(CurrentPathStr, FolderNameStr);

		// Try and find an existing tree item
		TSharedPtr<FTreeItem> CurrentTreeItem;
		for (const TSharedPtr<FTreeItem>& PotentialTreeItem : *CurrentTreeItems)
		{
			if (PotentialTreeItem->GetItem().GetItemName() == FolderName)
			{
				CurrentTreeItem = PotentialTreeItem;
				break;
			}
		}

		// Handle creating the leaf-most item that was given to us to create
		if (bIsLeafmostItem)
		{
			if (CurrentTreeItem)
			{
				// Found a match - merge the new item data
				CurrentTreeItem->AppendItemData(InItem);
			}
			else
			{
				// No match - create a new item
				CurrentTreeItem = MakeShared<FTreeItem>(MoveTemp(InItem));
				CurrentTreeItem->Parent = ParentTreeItem;
				CurrentTreeItems->Add(CurrentTreeItem);

				if (ParentTreeItem)
				{
					check(&ParentTreeItem->Children == CurrentTreeItems);
					ParentTreeItem->RequestSortChildren();
				}
				else
				{
					SortRootItems();
				}

				// If we have pending initial paths, and this path added the path, we should select it now
				if (PendingInitialPaths.Num() > 0 && PendingInitialPaths.Contains(CurrentTreeItem->GetItem().GetVirtualPath()))
				{
					RecursiveExpandParents(CurrentTreeItem);
					TreeViewPtr->SetItemSelection(CurrentTreeItem, true);
					TreeViewPtr->RequestScrollIntoView(CurrentTreeItem);
				}
			}

			// If we want to name this item, select it, scroll it into view, expand the parent
			if (bUserNamed)
			{
				RecursiveExpandParents(CurrentTreeItem);
				TreeViewPtr->SetSelection(CurrentTreeItem);
				CurrentTreeItem->SetNamingFolder(true);
				TreeViewPtr->RequestScrollIntoView(CurrentTreeItem);
			}

			TreeViewPtr->RequestTreeRefresh();
			return CurrentTreeItem;
		}

		// If we're missing an item on the way down to the leaf-most item then we'll add a placeholder
		// This shouldn't usually happen as Populate will create paths in the correct order, but 
		// the path picker may force add a path that hasn't been discovered (or doesn't exist) yet
		if (!CurrentTreeItem)
		{
			CurrentTreeItem = MakeShared<FTreeItem>(FContentBrowserItemData(InItem.GetOwnerDataSource(), EContentBrowserItemFlags::Type_Folder, *CurrentPathStr, FolderName, FText(), nullptr));
			CurrentTreeItem->Parent = ParentTreeItem;
			CurrentTreeItems->Add(CurrentTreeItem);

			if (ParentTreeItem)
			{
				check(&ParentTreeItem->Children == CurrentTreeItems);
				ParentTreeItem->RequestSortChildren();
			}
			else
			{
				SortRootItems();
			}

			// If we have pending initial paths, and this path added the path, we should select it now
			if (PendingInitialPaths.Num() > 0 && PendingInitialPaths.Contains(CurrentTreeItem->GetItem().GetVirtualPath()))
			{
				RecursiveExpandParents(CurrentTreeItem);
				TreeViewPtr->SetItemSelection(CurrentTreeItem, true);
				TreeViewPtr->RequestScrollIntoView(CurrentTreeItem);
			}
		}

		// Set-up the data for the next level
		ParentTreeItem = CurrentTreeItem;
		CurrentTreeItems = &ParentTreeItem->Children;
	}

	return nullptr;
}

bool SPathView::RemoveFolderItem(const FContentBrowserItemData& InItem)
{
	if (!ensure(TreeViewPtr.IsValid()))
	{
		// No tree view for some reason
		return false;
	}

	if (!InItem.IsFolder())
	{
		// Not a folder
		return false;
	}

	// Find the folder in the tree
	if (TSharedPtr<FTreeItem> ItemToRemove = FindItemRecursive(InItem.GetVirtualPath()))
	{
		// Only fully remove this item if every sub-item is removed (items become invalid when empty)
		ItemToRemove->RemoveItemData(InItem);
		if (ItemToRemove->GetItem().IsValid())
		{
			return true;
		}

		// Found the folder to remove. Remove it.
		if (TSharedPtr<FTreeItem> ItemParent = ItemToRemove->Parent.Pin())
		{
			// Remove the folder from its parent's list
			ItemParent->Children.Remove(ItemToRemove);
		}
		else
		{
			// This is a root item. Remove the folder from the root items list.
			TreeRootItems.Remove(ItemToRemove);
		}

		// Refresh the tree
		TreeViewPtr->RequestTreeRefresh();

		return true;
	}
	
	// Did not find the folder to remove
	return false;
}

void SPathView::RenameFolderItem(const FContentBrowserItem& InItem)
{
	if (!ensure(TreeViewPtr.IsValid()))
	{
		// No tree view for some reason
		return;
	}

	if (!InItem.IsFolder())
	{
		// Not a folder
		return;
	}

	// Find the folder in the tree
	if (TSharedPtr<FTreeItem> ItemToRename = FindItemRecursive(InItem.GetVirtualPath()))
	{
		ItemToRename->SetNamingFolder(true);

		TreeViewPtr->SetSelection(ItemToRename);
		TreeViewPtr->RequestScrollIntoView(ItemToRename);
	}
}

FContentBrowserDataCompiledFilter SPathView::CreateCompiledFolderFilter() const
{
	const UContentBrowserSettings* ContentBrowserSettings = GetDefault<UContentBrowserSettings>();

	FContentBrowserDataFilter DataFilter;
	DataFilter.bRecursivePaths = true;

	DataFilter.ItemTypeFilter = EContentBrowserItemTypeFilter::IncludeFolders;

	DataFilter.ItemCategoryFilter = InitialCategoryFilter;
	if (bAllowClassesFolder && ContentBrowserSettings->GetDisplayCppFolders())
	{
		DataFilter.ItemCategoryFilter |= EContentBrowserItemCategoryFilter::IncludeClasses;
	}
	else
	{
		DataFilter.ItemCategoryFilter &= ~EContentBrowserItemCategoryFilter::IncludeClasses;
	}
	DataFilter.ItemCategoryFilter &= ~EContentBrowserItemCategoryFilter::IncludeCollections;
	
	DataFilter.ItemAttributeFilter = EContentBrowserItemAttributeFilter::IncludeProject
		| (ContentBrowserSettings->GetDisplayEngineFolder() ? EContentBrowserItemAttributeFilter::IncludeEngine : EContentBrowserItemAttributeFilter::IncludeNone)
		| (ContentBrowserSettings->GetDisplayPluginFolders() ? EContentBrowserItemAttributeFilter::IncludePlugins : EContentBrowserItemAttributeFilter::IncludeNone)
		| (ContentBrowserSettings->GetDisplayDevelopersFolder() ? EContentBrowserItemAttributeFilter::IncludeDeveloper : EContentBrowserItemAttributeFilter::IncludeNone)
		| (ContentBrowserSettings->GetDisplayL10NFolder() ? EContentBrowserItemAttributeFilter::IncludeLocalized : EContentBrowserItemAttributeFilter::IncludeNone);

	TSharedPtr<FBlacklistPaths> CombinedFolderBlacklist;
	if ((FolderBlacklist && FolderBlacklist->HasFiltering()) || (WritableFolderBlacklist && WritableFolderBlacklist->HasFiltering() && !bAllowReadOnlyFolders))
	{
		CombinedFolderBlacklist = MakeShared<FBlacklistPaths>();
		if (FolderBlacklist)
		{
			CombinedFolderBlacklist->Append(*FolderBlacklist);
		}
		if (WritableFolderBlacklist && !bAllowReadOnlyFolders)
		{
			CombinedFolderBlacklist->Append(*WritableFolderBlacklist);
		}
	}

	if (PluginPathFilters.IsValid() && PluginPathFilters->Num() > 0 && ContentBrowserSettings->GetDisplayPluginFolders())
	{
		TArray<TSharedRef<IPlugin>> Plugins = IPluginManager::Get().GetEnabledPluginsWithContent();
		for (const TSharedRef<IPlugin>& Plugin : Plugins)
		{
			if (!PluginPathFilters->PassesAllFilters(Plugin))
			{
				FString MountedAssetPath = Plugin->GetMountedAssetPath();
				MountedAssetPath.RemoveFromEnd(TEXT("/"), ESearchCase::CaseSensitive);

				if (!CombinedFolderBlacklist.IsValid())
				{
					CombinedFolderBlacklist = MakeShared<FBlacklistPaths>();
				}
				CombinedFolderBlacklist->AddBlacklistItem("PluginPathFilters", MountedAssetPath);
			}
		}
	}

	ContentBrowserUtils::AppendAssetFilterToContentBrowserFilter(FARFilter(), nullptr, CombinedFolderBlacklist, DataFilter);

	FContentBrowserDataCompiledFilter CompiledDataFilter;
	{
		static const FName RootPath = "/";
		UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
		ContentBrowserData->CompileFilter(RootPath, DataFilter, CompiledDataFilter);
	}
	return CompiledDataFilter;
}

void SPathView::SyncToItems(TArrayView<const FContentBrowserItem> ItemsToSync, const bool bAllowImplicitSync)
{
	TArray<FName> VirtualPathsToSync;
	for (const FContentBrowserItem& Item : ItemsToSync)
	{
		if (Item.IsFile())
		{
			// Files need to sync their parent folder in the tree, so chop off the end of their path
			VirtualPathsToSync.Add(*FPaths::GetPath(Item.GetVirtualPath().ToString()));
		}
		else
		{
			VirtualPathsToSync.Add(Item.GetVirtualPath());
		}
	}

	SyncToVirtualPaths(VirtualPathsToSync, bAllowImplicitSync);
}

void SPathView::SyncToVirtualPaths(TArrayView<const FName> VirtualPathsToSync, const bool bAllowImplicitSync)
{
	// Clear the search box if it potentially hides a path we want to select
	for (const FName& VirtualPathToSync : VirtualPathsToSync)
	{
		if (PathIsFilteredFromViewBySearch(VirtualPathToSync.ToString()))
		{
			SearchPtr->ClearSearch();
			break;
		}
	}

	TArray<TSharedPtr<FTreeItem>> SyncTreeItems;
	{
		TSet<FName> UniqueVirtualPathsToSync;
		for (const FName& VirtualPathToSync : VirtualPathsToSync)
		{
			if (!UniqueVirtualPathsToSync.Contains(VirtualPathToSync))
			{
				UniqueVirtualPathsToSync.Add(VirtualPathToSync);

				TSharedPtr<FTreeItem> Item = FindItemRecursive(VirtualPathToSync);
				if (Item.IsValid())
				{
					SyncTreeItems.Add(Item);
				}
			}
		}
	}

	if ( SyncTreeItems.Num() > 0 )
	{
		// Batch the selection changed event
		FScopedSelectionChangedEvent ScopedSelectionChangedEvent(SharedThis(this));

		if (bAllowImplicitSync)
		{
			// Prune the current selection so that we don't unnecessarily change the path which might disorientate the user.
			// If a parent tree item is currently selected we don't need to clear it and select the child
			auto SelectedTreeItems = TreeViewPtr->GetSelectedItems();

			for (int32 Index = 0; Index < SelectedTreeItems.Num(); ++Index)
			{
				// For each item already selected in the tree
				auto AlreadySelectedTreeItem = SelectedTreeItems[Index];
				if (!AlreadySelectedTreeItem.IsValid())
				{
					continue;
				}

				// Check to see if any of the items to sync are already synced
				for (int32 ToSyncIndex = SyncTreeItems.Num()-1; ToSyncIndex >= 0; --ToSyncIndex)
				{
					auto ToSyncItem = SyncTreeItems[ToSyncIndex];
					if (ToSyncItem == AlreadySelectedTreeItem || ToSyncItem->IsChildOf(*AlreadySelectedTreeItem.Get()))
					{
						// A parent is already selected
						SyncTreeItems.Pop();
					}
					else if (ToSyncIndex == 0)
					{
						// AlreadySelectedTreeItem is not required for SyncTreeItems, so deselect it
						TreeViewPtr->SetItemSelection(AlreadySelectedTreeItem, false);
					}
				}
			}
		}
		else
		{
			// Explicit sync so just clear the selection
			TreeViewPtr->ClearSelection();
		}

		// SyncTreeItems should now only contain items which aren't already shown explicitly or implicitly (as a child)
		for ( auto ItemIt = SyncTreeItems.CreateConstIterator(); ItemIt; ++ItemIt )
		{
			RecursiveExpandParents(*ItemIt);
			TreeViewPtr->SetItemSelection(*ItemIt, true);
		}
	}

	// > 0 as some may have been popped off in the code above
	if (SyncTreeItems.Num() > 0)
	{
		// Scroll the first item into view if applicable
		TreeViewPtr->RequestScrollIntoView(SyncTreeItems[0]);
	}
}

void SPathView::SyncToLegacy(TArrayView<const FAssetData> AssetDataList, TArrayView<const FString> FolderList, const bool bAllowImplicitSync)
{
	TArray<FName> VirtualPathsToSync;
	ContentBrowserUtils::ConvertLegacySelectionToVirtualPaths(AssetDataList, FolderList, /*UseFolderPaths*/true, VirtualPathsToSync);

	SyncToVirtualPaths(VirtualPathsToSync, bAllowImplicitSync);
}

TSharedPtr<FTreeItem> SPathView::FindItemRecursive(const FName Path) const
{
	TStringBuilder<FName::StringBufferSize> PathStr;
	Path.ToString(PathStr);

	for (auto TreeItemIt = TreeRootItems.CreateConstIterator(); TreeItemIt; ++TreeItemIt)
	{
		if ( (*TreeItemIt)->GetItem().GetVirtualPath() == Path)
		{
			// This root item is the path
			return *TreeItemIt;
		}

		// Test whether the node we want is potentially under this root before recursing
		{
			TStringBuilder<FName::StringBufferSize> RootPathStr;
			(*TreeItemIt)->GetItem().GetVirtualPath().ToString(RootPathStr);

			if (!FStringView(PathStr).StartsWith(FStringView(RootPathStr)))
			{
				continue;
			}
		}

		// Try to find the item under this root
		TSharedPtr<FTreeItem> Item = (*TreeItemIt)->FindItemRecursive(Path);
		if ( Item.IsValid() )
		{
			// The item was found under this root
			return Item;
		}
	}

	return TSharedPtr<FTreeItem>();
}

void SPathView::ApplyHistoryData( const FHistoryData& History )
{
	// Prevent the selection changed delegate because it would add more history when we are just setting a state
	FScopedPreventTreeItemChangedDelegate DelegatePrevention( SharedThis(this) );

	// Update paths
	TArray<FString> SelectedPaths;
	for (const FName& HistoryPath : History.SourcesData.VirtualPaths)
	{
		SelectedPaths.Add(HistoryPath.ToString());
	}
	SetSelectedPaths(SelectedPaths);
}

void SPathView::SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const
{
	FString SelectedPathsString;
	TArray< TSharedPtr<FTreeItem> > PathItems = TreeViewPtr->GetSelectedItems();
	for ( auto PathIt = PathItems.CreateConstIterator(); PathIt; ++PathIt )
	{
		if ( SelectedPathsString.Len() > 0 )
		{
			SelectedPathsString += TEXT(",");
		}

		(*PathIt)->GetItem().GetVirtualPath().AppendString(SelectedPathsString);
	}

	GConfig->SetString(*IniSection, *(SettingsString + TEXT(".SelectedPaths")), *SelectedPathsString, IniFilename);

	FString PluginFiltersString;
	if (PluginPathFilters.IsValid())
	{
		for (int32 i=0; i < PluginPathFilters->Num(); ++i)
		{
			if (PluginFiltersString.Len() > 0)
			{
				PluginFiltersString += TEXT(",");
			}

			TSharedPtr<FContentBrowserPluginFilter> Filter = StaticCastSharedPtr<FContentBrowserPluginFilter>(PluginPathFilters->GetFilterAtIndex(i));
			PluginFiltersString += Filter->GetName();
		}
		GConfig->SetString(*IniSection, *(SettingsString + TEXT(".PluginFilters")), *PluginFiltersString, IniFilename);
	}
}

void SPathView::LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString)
{
	// Selected Paths
	FString SelectedPathsString;
	if ( GConfig->GetString(*IniSection, *(SettingsString + TEXT(".SelectedPaths")), SelectedPathsString, IniFilename) )
	{
		TArray<FString> NewSelectedPaths;
		SelectedPathsString.ParseIntoArray(NewSelectedPaths, TEXT(","), /*bCullEmpty*/true);

		UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
		const bool bDiscoveringAssets = ContentBrowserData->IsDiscoveringItems();

		// Batch the selection changed event
		FScopedSelectionChangedEvent ScopedSelectionChangedEvent(SharedThis(this));

		if ( bDiscoveringAssets )
		{
			// Clear any previously selected paths
			LastSelectedPaths.Empty();
			TreeViewPtr->ClearSelection();

			// If the selected paths is empty, the path was "All assets"
			// This should handle that case properly
			for (int32 PathIdx = 0; PathIdx < NewSelectedPaths.Num(); ++PathIdx)
			{
				const FName Path = *NewSelectedPaths[PathIdx];
				if ( !ExplicitlyAddPathToSelection(Path) )
				{
					// If we could not initially select these paths, but are still discovering assets, add them to a pending list to select them later
					PendingInitialPaths.Add(Path);
				}
			}
		}
		else
		{
			// If all assets are already discovered, just select paths the best we can
			SetSelectedPaths(NewSelectedPaths);
		}
	}

	// Plugin Filters
	if (PluginPathFilters.IsValid())
	{
		FString PluginFiltersString;
		if (GConfig->GetString(*IniSection, *(SettingsString + TEXT(".PluginFilters")), PluginFiltersString, IniFilename))
		{
			TArray<FString> NewSelectedFilters;
			PluginFiltersString.ParseIntoArray(NewSelectedFilters, TEXT(","), /*bCullEmpty*/ true);

			for (const TSharedRef<FContentBrowserPluginFilter>& Filter : AllPluginPathFilters)
			{
				bool bFilterActive = NewSelectedFilters.Contains(Filter->GetName());
				SetPluginPathFilterActive(Filter, bFilterActive);
			}
		}
	}
}

EActiveTimerReturnType SPathView::SetFocusPostConstruct( double InCurrentTime, float InDeltaTime )
{
	FWidgetPath WidgetToFocusPath;
	FSlateApplication::Get().GeneratePathToWidgetUnchecked( SearchPtr->GetWidget(), WidgetToFocusPath );
	FSlateApplication::Get().SetKeyboardFocus( WidgetToFocusPath, EFocusCause::SetDirectly );

	return EActiveTimerReturnType::Stop;
}

EActiveTimerReturnType SPathView::TriggerRepopulate(double InCurrentTime, float InDeltaTime)
{
	Populate();
	return EActiveTimerReturnType::Stop;
}

TSharedPtr<SWidget> SPathView::MakePathViewContextMenu()
{
	if (!bAllowContextMenu || !OnGetItemContextMenu.IsBound())
	{
		return nullptr;
	}

	const TArray<FContentBrowserItem> SelectedItems = GetSelectedFolderItems();
	if (SelectedItems.Num() == 0)
	{
		return nullptr;
	}
	
	return OnGetItemContextMenu.Execute(SelectedItems);
}

void SPathView::NewFolderItemRequested(const FContentBrowserItemTemporaryContext& NewItemContext)
{
	bool bAddedTemporaryFolder = false;
	for (const FContentBrowserItemData& NewItemData : NewItemContext.GetItem().GetInternalItems())
	{
		bAddedTemporaryFolder |= AddFolderItem(CopyTemp(NewItemData), /*bUserNamed=*/true).IsValid();
	}

	if (bAddedTemporaryFolder)
	{
		PendingNewFolderContext = NewItemContext;
	}
}

bool SPathView::ExplicitlyAddPathToSelection(const FName Path)
{
	if ( !ensure(TreeViewPtr.IsValid()) )
	{
		return false;
	}

	if (TSharedPtr<FTreeItem> FoundItem = FindItemRecursive(Path))
	{
		// Set the selection to the closest found folder and scroll it into view
		RecursiveExpandParents(FoundItem);
		LastSelectedPaths.Add(FoundItem->GetItem().GetVirtualPath());
		TreeViewPtr->SetItemSelection(FoundItem, true);
		TreeViewPtr->RequestScrollIntoView(FoundItem);

		return true;
	}

	return false;
}

bool SPathView::ShouldAllowTreeItemChangedDelegate() const
{
	return PreventTreeItemChangedDelegateCount == 0;
}

void SPathView::RecursiveExpandParents(const TSharedPtr<FTreeItem>& Item)
{
	if ( Item->Parent.IsValid() )
	{
		RecursiveExpandParents(Item->Parent.Pin());
		TreeViewPtr->SetItemExpansion(Item->Parent.Pin(), true);
	}
}

TSharedRef<ITableRow> SPathView::GenerateTreeRow( TSharedPtr<FTreeItem> TreeItem, const TSharedRef<STableViewBase>& OwnerTable )
{
	check(TreeItem.IsValid());

	return
		SNew( STableRow< TSharedPtr<FTreeItem> >, OwnerTable )
		.OnDragDetected( this, &SPathView::OnFolderDragDetected )
		[
			SNew(SAssetTreeItem)
			.TreeItem(TreeItem)
			.OnNameChanged(this, &SPathView::FolderNameChanged)
			.OnVerifyNameChanged(this, &SPathView::VerifyFolderNameChanged)
			.IsItemExpanded(this, &SPathView::IsTreeItemExpanded, TreeItem)
			.HighlightText(this, &SPathView::GetHighlightText)
			.IsSelected(this, &SPathView::IsTreeItemSelected, TreeItem)
		];
}

void SPathView::TreeItemScrolledIntoView( TSharedPtr<FTreeItem> TreeItem, const TSharedPtr<ITableRow>& Widget )
{
	if ( TreeItem->IsNamingFolder() && Widget.IsValid() && Widget->GetContent().IsValid() )
	{
		TreeItem->OnRenameRequested().Broadcast();
	}
}

void SPathView::GetChildrenForTree( TSharedPtr< FTreeItem > TreeItem, TArray< TSharedPtr<FTreeItem> >& OutChildren )
{
	TreeItem->SortChildrenIfNeeded();
	OutChildren = TreeItem->Children;
}

void SPathView::SetTreeItemExpansionRecursive( TSharedPtr< FTreeItem > TreeItem, bool bInExpansionState )
{
	TreeViewPtr->SetItemExpansion(TreeItem, bInExpansionState);

	// Recursively go through the children.
	for(auto It = TreeItem->Children.CreateIterator(); It; ++It)
	{
		SetTreeItemExpansionRecursive( *It, bInExpansionState );
	}
}

void SPathView::TreeSelectionChanged( TSharedPtr< FTreeItem > TreeItem, ESelectInfo::Type SelectInfo )
{
	if ( ShouldAllowTreeItemChangedDelegate() )
	{
		const TArray<TSharedPtr<FTreeItem>> SelectedItems = TreeViewPtr->GetSelectedItems();

		LastSelectedPaths.Empty();
		for (int32 ItemIdx = 0; ItemIdx < SelectedItems.Num(); ++ItemIdx)
		{
			const TSharedPtr<FTreeItem> Item = SelectedItems[ItemIdx];
			if ( !ensure(Item.IsValid()) )
			{
				// All items must exist
				continue;
			}

			// Keep track of the last paths that we broadcasted for selection reasons when filtering
			LastSelectedPaths.Add(Item->GetItem().GetVirtualPath());
		}

		if ( OnItemSelectionChanged.IsBound() )
		{
			if ( TreeItem.IsValid() )
			{
				OnItemSelectionChanged.Execute(TreeItem->GetItem(), SelectInfo);
			}
			else
			{
				OnItemSelectionChanged.Execute(FContentBrowserItem(), SelectInfo);
			}
		}
	}

	if (TreeItem.IsValid())
	{
		// Prioritize the content scan for the selected path
		UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
		ContentBrowserData->PrioritizeSearchPath(TreeItem->GetItem().GetVirtualPath());
	}
}

void SPathView::TreeExpansionChanged( TSharedPtr< FTreeItem > TreeItem, bool bIsExpanded )
{
	if ( ShouldAllowTreeItemChangedDelegate() )
	{
		TSet<TSharedPtr<FTreeItem>> ExpandedItemSet;
		TreeViewPtr->GetExpandedItems(ExpandedItemSet);
		const TArray<TSharedPtr<FTreeItem>> ExpandedItems = ExpandedItemSet.Array();

		LastExpandedPaths.Empty();
		for (int32 ItemIdx = 0; ItemIdx < ExpandedItems.Num(); ++ItemIdx)
		{
			const TSharedPtr<FTreeItem> Item = ExpandedItems[ItemIdx];
			if ( !ensure(Item.IsValid()) )
			{
				// All items must exist
				continue;
			}

			// Keep track of the last paths that we broadcasted for expansion reasons when filtering
			LastExpandedPaths.Add(Item->GetItem().GetVirtualPath());
		}

		if (!bIsExpanded)
		{
			const TArray<TSharedPtr<FTreeItem>> SelectedItems = TreeViewPtr->GetSelectedItems();
			bool bSelectTreeItem = false;

			// If any selected item was a child of the collapsed node, then add the collapsed node to the current selection
			// This avoids the selection ever becoming empty, as this causes the Content Browser to show everything
			for (const TSharedPtr<FTreeItem>& SelectedItem : SelectedItems)
			{
				if (SelectedItem->IsChildOf(*TreeItem.Get()))
				{
					bSelectTreeItem = true;
					break;
				}
			}

			if (bSelectTreeItem)
			{
				TreeViewPtr->SetItemSelection(TreeItem, true);
			}
		}
	}
}

void SPathView::FilterUpdated()
{
	Populate(/*bIsRefreshingFilter*/true);
}

void SPathView::SetSearchFilterText(const FText& InSearchText, TArray<FText>& OutErrors)
{
	SearchBoxFolderFilter->SetRawFilterText(InSearchText);

	const FText ErrorText = SearchBoxFolderFilter->GetFilterErrorText();
	if (!ErrorText.IsEmpty())
	{
		OutErrors.Add(ErrorText);
	}
}

FText SPathView::GetHighlightText() const
{
	return SearchBoxFolderFilter->GetRawFilterText();
}

void SPathView::Populate(const bool bIsRefreshingFilter)
{
	const bool bFilteringByText = !SearchBoxFolderFilter->GetRawFilterText().IsEmpty();

	// Batch the selection changed event
	// Only emit events when the user isn't filtering, as the selection may be artificially limited by the filter
	FScopedSelectionChangedEvent ScopedSelectionChangedEvent(SharedThis(this), !bFilteringByText && !bIsRefreshingFilter);

	// Clear all root items and clear selection
	TreeRootItems.Empty();
	TreeViewPtr->ClearSelection();

	// Populate the view
	{
		const UContentBrowserSettings* ContentBrowserSettings = GetDefault<UContentBrowserSettings>();
		const bool bDisplayEmpty = ContentBrowserSettings->DisplayEmptyFolders;

		UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
		const FContentBrowserDataCompiledFilter CompiledDataFilter = CreateCompiledFolderFilter();

		ContentBrowserData->EnumerateItemsMatchingFilter(CompiledDataFilter, [this, bFilteringByText, bDisplayEmpty, ContentBrowserData](FContentBrowserItemData&& InItemData)
		{
			bool bPassesFilter = bDisplayEmpty || ContentBrowserData->IsFolderVisibleIfHidingEmpty(InItemData.GetVirtualPath());
			if (bPassesFilter && bFilteringByText)
			{
				// Use the whole path so we deliberately include any children of matched parents in the filtered list
				const FString PathStr = InItemData.GetVirtualPath().ToString();
				bPassesFilter &= SearchBoxFolderFilter->PassesFilter(PathStr);
			}

			if (bPassesFilter)
			{
				if (TSharedPtr<FTreeItem> Item = AddFolderItem(MoveTemp(InItemData)))
				{
					const bool bSelectedItem = LastSelectedPaths.Contains(Item->GetItem().GetVirtualPath());
					const bool bExpandedItem = LastExpandedPaths.Contains(Item->GetItem().GetVirtualPath());

					if (bFilteringByText || bSelectedItem)
					{
						RecursiveExpandParents(Item);
					}

					if (bSelectedItem)
					{
						// Tree items that match the last broadcasted paths should be re-selected them after they are added
						if (!TreeViewPtr->IsItemSelected(Item))
						{
							TreeViewPtr->SetItemSelection(Item, true);
						}
						TreeViewPtr->RequestScrollIntoView(Item);
					}

					if (bExpandedItem)
					{
						// Tree items that were previously expanded should be re-expanded when repopulating
						if (!TreeViewPtr->IsItemExpanded(Item))
						{
							TreeViewPtr->SetItemExpansion(Item, true);
						}
					}
				}
			}

			return true;
		});
	}

	SortRootItems();
}

void SPathView::SortRootItems()
{
	// TODO: Make more abstract sorting via the new API?

	// First sort the root items by their display name, but also making sure that content to appears before classes
	TreeRootItems.Sort([](const TSharedPtr<FTreeItem>& One, const TSharedPtr<FTreeItem>& Two) -> bool
	{
		static const FString ClassesPrefix = TEXT("Classes_");

		FString OneModuleName = One->GetItem().GetItemName().ToString();
		const bool bOneIsClass = OneModuleName.StartsWith(ClassesPrefix);
		if(bOneIsClass)
		{
			OneModuleName.MidInline(ClassesPrefix.Len(), MAX_int32, false);
		}

		FString TwoModuleName = Two->GetItem().GetItemName().ToString();
		const bool bTwoIsClass = TwoModuleName.StartsWith(ClassesPrefix);
		if(bTwoIsClass)
		{
			TwoModuleName.MidInline(ClassesPrefix.Len(), MAX_int32, false);
		}

		// We want to sort content before classes if both items belong to the same module
		if(OneModuleName == TwoModuleName)
		{
			if(!bOneIsClass && bTwoIsClass)
			{
				return true;
			}
			return false;
		}

		return One->GetItem().GetDisplayName().ToString() < Two->GetItem().GetDisplayName().ToString();
	});

	// We have some manual sorting requirements that game must come before engine, and engine before everything else - we do that here after sorting everything by name
	// The array below is in the inverse order as we iterate through and move each match to the beginning of the root items array
	const TArray<FString> SpecialDefaultFolders = {
		TEXT("Game"),
		TEXT("Classes_Game"),
		TEXT("Engine"),
		TEXT("Classes_Engine"),
	};

	const FString ClassesPrefix = TEXT("Classes_");

	struct FRootItemSortInfo
	{
		FString FolderName;
		float Priority;
		int32 SpecialDefaultFolderPriority;
		bool bIsClassesFolder;
	};

	TMap<FTreeItem*, FRootItemSortInfo> SortInfoMap;
	for (const TSharedPtr<FTreeItem>& RootItem : TreeRootItems)
	{
		FRootItemSortInfo SortInfo;
		SortInfo.FolderName = RootItem->GetItem().GetItemName().ToString();
		SortInfo.bIsClassesFolder = SortInfo.FolderName.StartsWith(ClassesPrefix);
		int32 SpecialDefaultFolderIdx = SpecialDefaultFolders.IndexOfByKey(SortInfo.FolderName);
		if (SortInfo.bIsClassesFolder)
		{
			SortInfo.FolderName.MidInline(ClassesPrefix.Len(), MAX_int32, false);
		}
		SortInfo.SpecialDefaultFolderPriority = SpecialDefaultFolderIdx != INDEX_NONE ? SpecialDefaultFolders.Num() - SpecialDefaultFolderIdx : 0;
		SortInfo.Priority = SpecialDefaultFolderIdx == INDEX_NONE ? FContentBrowserSingleton::Get().GetPluginSettings(FName(*SortInfo.FolderName)).RootFolderSortPriority : 1.f;
		SortInfoMap.Add(RootItem.Get(), SortInfo);
	}

	
	TreeRootItems.Sort([&SortInfoMap](const TSharedPtr<FTreeItem>& RootItemA, const TSharedPtr<FTreeItem>& RootItemB) {
		const FRootItemSortInfo& SortInfoA = SortInfoMap.FindChecked(RootItemA.Get());
		const FRootItemSortInfo& SortInfoB = SortInfoMap.FindChecked(RootItemB.Get());
		if (SortInfoA.Priority != SortInfoB.Priority)
		{
			// Not the same priority, use priority to sort
			return SortInfoA.Priority > SortInfoB.Priority;
		}
		else if (SortInfoA.SpecialDefaultFolderPriority != SortInfoB.SpecialDefaultFolderPriority)
		{
			// Special folders use the index to sort. Non special folders are all set to 0.
			return SortInfoA.SpecialDefaultFolderPriority > SortInfoB.SpecialDefaultFolderPriority;
		}
		else if (SortInfoA.FolderName != SortInfoB.FolderName)
		{
			// Two non special folders of the same priority, sort alphabetically
			return SortInfoA.FolderName < SortInfoB.FolderName;
		}
		else
		{
			// Classes folders have the same name so sort them adjacent but under non-classes
			return !SortInfoA.bIsClassesFolder;
		}
	});

	TreeViewPtr->RequestTreeRefresh();
}

void SPathView::PopulateFolderSearchStrings( const FString& FolderName, OUT TArray< FString >& OutSearchStrings ) const
{
	OutSearchStrings.Add( FolderName );
}

FReply SPathView::OnFolderDragDetected(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	if ( MouseEvent.IsMouseButtonDown( EKeys::LeftMouseButton ) )
	{
		if (TSharedPtr<FDragDropOperation> DragDropOp = DragDropHandler::CreateDragOperation(GetSelectedFolderItems()))
		{
			return FReply::Handled().BeginDragDrop(DragDropOp.ToSharedRef());
		}
	}

	return FReply::Unhandled();
}

bool SPathView::VerifyFolderNameChanged(const TSharedPtr< FTreeItem >& TreeItem, const FString& ProposedName, FText& OutErrorMessage) const
{
	if (PendingNewFolderContext.IsValid())
	{
		checkf(FContentBrowserItemKey(TreeItem->GetItem()) == FContentBrowserItemKey(PendingNewFolderContext.GetItem()), TEXT("PendingNewFolderContext was still set when attempting to rename a different item!"));

		return PendingNewFolderContext.ValidateItem(ProposedName, &OutErrorMessage);
	}
	else if (!TreeItem->GetItem().GetItemName().ToString().Equals(ProposedName))
	{
		return TreeItem->GetItem().CanRename(&ProposedName, &OutErrorMessage);
	}

	return true;
}

void SPathView::FolderNameChanged( const TSharedPtr< FTreeItem >& TreeItem, const FString& ProposedName, const FVector2D& MessageLocation, const ETextCommit::Type CommitType )
{
	bool bSuccess = false;
	FText ErrorMessage;

	FContentBrowserItem NewItem;
	if (PendingNewFolderContext.IsValid())
	{
		checkf(FContentBrowserItemKey(TreeItem->GetItem()) == FContentBrowserItemKey(PendingNewFolderContext.GetItem()), TEXT("PendingNewFolderContext was still set when attempting to rename a different item!"));

		// Remove the temporary item before we do any work to ensure the new item creation is not prevented
		RemoveFolderItem(TreeItem);

		// Clearing the rename box on a newly created item cancels the entire creation process
		if (CommitType == ETextCommit::OnCleared)
		{
			// We need to select the parent item of this folder, as the folder would have become selected while it was being named
			if (TSharedPtr<FTreeItem> ParentTreeItem = TreeItem->Parent.Pin())
			{
				TreeViewPtr->SetItemSelection(ParentTreeItem, true);
			}
			else
			{
				TreeViewPtr->ClearSelection();
			}
		}
		else
		{
			if (PendingNewFolderContext.ValidateItem(ProposedName, &ErrorMessage))
			{
				NewItem = PendingNewFolderContext.FinalizeItem(ProposedName, &ErrorMessage);
				if (NewItem.IsValid())
				{
					bSuccess = true;
				}
			}
		}

		PendingNewFolderContext = FContentBrowserItemTemporaryContext();
	}
	else if (CommitType != ETextCommit::OnCleared && !TreeItem->GetItem().GetItemName().ToString().Equals(ProposedName))
	{
		if (TreeItem->GetItem().CanRename(&ProposedName, &ErrorMessage) && TreeItem->GetItem().Rename(ProposedName, &NewItem))
		{
			bSuccess = true;
		}
	}

	if (bSuccess && NewItem.IsValid())
	{
		// Add result to view
		TSharedPtr<FTreeItem> NewTreeItem;
		for (const FContentBrowserItemData& NewItemData : NewItem.GetInternalItems())
		{
			NewTreeItem = AddFolderItem(CopyTemp(NewItemData));
		}

		// Select the new item
		if (NewTreeItem)
		{
			TreeViewPtr->SetItemSelection(NewTreeItem, true);
			TreeViewPtr->RequestScrollIntoView(NewTreeItem);
		}
	}

	if (!bSuccess && !ErrorMessage.IsEmpty())
	{
		// Display the reason why the folder was invalid
		FSlateRect MessageAnchor(MessageLocation.X, MessageLocation.Y, MessageLocation.X, MessageLocation.Y);
		ContentBrowserUtils::DisplayMessage(ErrorMessage, MessageAnchor, SharedThis(this));
	}
}

bool SPathView::FolderAlreadyExists(const TSharedPtr< FTreeItem >& TreeItem, TSharedPtr< FTreeItem >& ExistingItem)
{
	ExistingItem.Reset();

	if ( TreeItem.IsValid() )
	{
		if ( TreeItem->Parent.IsValid() )
		{
			// This item has a parent, try to find it in its parent's children
			TSharedPtr<FTreeItem> ParentItem = TreeItem->Parent.Pin();

			for ( auto ChildIt = ParentItem->Children.CreateConstIterator(); ChildIt; ++ChildIt )
			{
				const TSharedPtr<FTreeItem>& Child = *ChildIt;
				if ( Child != TreeItem && Child->GetItem().GetItemName() == TreeItem->GetItem().GetItemName() )
				{
					// The item is in its parent already
					ExistingItem = Child;
					break;
				}
			}
		}
		else
		{
			// This item is part of the root set
			for ( auto RootIt = TreeRootItems.CreateConstIterator(); RootIt; ++RootIt )
			{
				const TSharedPtr<FTreeItem>& Root = *RootIt;
				if ( Root != TreeItem && Root->GetItem().GetItemName() == TreeItem->GetItem().GetItemName() )
				{
					// The item is part of the root set already
					ExistingItem = Root;
					break;
				}
			}
		}
	}

	return ExistingItem.IsValid();
}

void SPathView::RemoveFolderItem(const TSharedPtr< FTreeItem >& TreeItem)
{
	if ( TreeItem.IsValid() )
	{
		if ( TreeItem->Parent.IsValid() )
		{
			// Remove this item from it's parent's list
			TreeItem->Parent.Pin()->Children.Remove(TreeItem);
		}
		else
		{
			// This was a root node, remove from the root list
			TreeRootItems.Remove(TreeItem);
		}

		TreeViewPtr->RequestTreeRefresh();
	}
}

bool SPathView::IsTreeItemExpanded(TSharedPtr<FTreeItem> TreeItem) const
{
	return TreeViewPtr->IsItemExpanded(TreeItem);
}

bool SPathView::IsTreeItemSelected(TSharedPtr<FTreeItem> TreeItem) const
{
	return TreeViewPtr->IsItemSelected(TreeItem);
}

void SPathView::HandleItemDataUpdated(TArrayView<const FContentBrowserItemDataUpdate> InUpdatedItems)
{
	if (InUpdatedItems.Num() == 0)
	{
		return;
	}

	const bool bFilteringByText = !SearchBoxFolderFilter->GetRawFilterText().IsEmpty();

	// Batch the selection changed event
	// Only emit events when the user isn't filtering, as the selection may be artificially limited by the filter
	FScopedSelectionChangedEvent ScopedSelectionChangedEvent(SharedThis(this), !bFilteringByText);

	const double HandleItemDataUpdatedStartTime = FPlatformTime::Seconds();

	const UContentBrowserSettings* ContentBrowserSettings = GetDefault<UContentBrowserSettings>();
	const bool bDisplayEmpty = ContentBrowserSettings->DisplayEmptyFolders;

	UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();

	// We defer this compilation as it's quite expensive due to being recursive, and not all updates will contain new folders
	bool bHasCompiledDataFilter = false;
	FContentBrowserDataCompiledFilter CompiledDataFilter;
	auto ConditionalCompileFilter = [this, &bHasCompiledDataFilter, &CompiledDataFilter]()
	{
		if (!bHasCompiledDataFilter)
		{
			bHasCompiledDataFilter = true;
			CompiledDataFilter = CreateCompiledFolderFilter();
		}
	};

	auto DoesItemPassFilter = [this, bFilteringByText, bDisplayEmpty, ContentBrowserData, &CompiledDataFilter](const FContentBrowserItemData& InItemData)
	{
		UContentBrowserDataSource* ItemDataSource = InItemData.GetOwnerDataSource();
		if (!ItemDataSource->DoesItemPassFilter(InItemData, CompiledDataFilter))
		{
			return false;
		}

		if (!bDisplayEmpty && !ContentBrowserData->IsFolderVisibleIfHidingEmpty(InItemData.GetVirtualPath()))
		{
			return false;
		}

		if (bFilteringByText)
		{
			// Use the whole path so we deliberately include any children of matched parents in the filtered list
			const FString PathStr = InItemData.GetVirtualPath().ToString();
			if (!SearchBoxFolderFilter->PassesFilter(PathStr))
			{
				return false;
			}
		}

		return true;
	};

	for (const FContentBrowserItemDataUpdate& ItemDataUpdate : InUpdatedItems)
	{
		const FContentBrowserItemData& ItemData = ItemDataUpdate.GetItemData();
		if (!ItemData.IsFolder())
		{
			continue;
		}

		ConditionalCompileFilter();

		switch (ItemDataUpdate.GetUpdateType())
		{
		case EContentBrowserItemUpdateType::Added:
		case EContentBrowserItemUpdateType::Modified:
			if (DoesItemPassFilter(ItemData))
			{
				AddFolderItem(CopyTemp(ItemData));
			}
			else
			{
				RemoveFolderItem(ItemData);
			}
			break;

		case EContentBrowserItemUpdateType::Moved:
		{
			const FContentBrowserItemData OldMinimalItemData(ItemData.GetOwnerDataSource(), ItemData.GetItemType(), ItemDataUpdate.GetPreviousVirtualPath(), NAME_None, FText(), nullptr);
			RemoveFolderItem(OldMinimalItemData);

			if (DoesItemPassFilter(ItemData))
			{
				AddFolderItem(CopyTemp(ItemData));
			}
		}
		break;

		case EContentBrowserItemUpdateType::Removed:
			RemoveFolderItem(ItemData);
			break;

		default:
			checkf(false, TEXT("Unexpected EContentBrowserItemUpdateType!"));
			break;
		}
	}

	UE_LOG(LogContentBrowser, VeryVerbose, TEXT("PathView - HandleItemDataUpdated completed in %0.4f seconds for %d items"), FPlatformTime::Seconds() - HandleItemDataUpdatedStartTime, InUpdatedItems.Num());
}

void SPathView::HandleItemDataRefreshed()
{
	// Populate immediately, as the path view must be up to date for Content Browser selection to work correctly
	// and since it defaults to being hidden, it potentially won't be ticked to run this update latently
	Populate();

	/*
	// The class hierarchy has changed in some way, so we need to refresh our set of paths
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SPathView::TriggerRepopulate));
	*/
}

void SPathView::HandleItemDataDiscoveryComplete()
{
	// If there were any more initial paths, they no longer exist so clear them now.
	PendingInitialPaths.Empty();
}

bool SPathView::PathIsFilteredFromViewBySearch(const FString& InPath) const
{
	return !SearchBoxFolderFilter->GetRawFilterText().IsEmpty()
		&& !SearchBoxFolderFilter->PassesFilter(InPath)
		&& !FindItemRecursive(*InPath);
}

void SPathView::HandleSettingChanged(FName PropertyName)
{
	if ((PropertyName == GET_MEMBER_NAME_CHECKED(UContentBrowserSettings, DisplayEmptyFolders)) ||
		(PropertyName == "DisplayDevelopersFolder") ||
		(PropertyName == "DisplayEngineFolder") ||
		(PropertyName == "DisplayPluginFolders") ||
		(PropertyName == "DisplayL10NFolder") ||
		(PropertyName == NAME_None))	// @todo: Needed if PostEditChange was called manually, for now
	{
		// If the dev or engine folder is no longer visible but we're inside it...
		const bool bDisplayEmpty = GetDefault<UContentBrowserSettings>()->DisplayEmptyFolders;
		const bool bDisplayDev = GetDefault<UContentBrowserSettings>()->GetDisplayDevelopersFolder();
		const bool bDisplayEngine = GetDefault<UContentBrowserSettings>()->GetDisplayEngineFolder();
		const bool bDisplayPlugins = GetDefault<UContentBrowserSettings>()->GetDisplayPluginFolders();
		const bool bDisplayL10N = GetDefault<UContentBrowserSettings>()->GetDisplayL10NFolder();
		if (!bDisplayEmpty || !bDisplayDev || !bDisplayEngine || !bDisplayPlugins || !bDisplayL10N)
		{
			const TArray<FContentBrowserItem> OldSelectedItems = GetSelectedFolderItems();
			if (OldSelectedItems.Num() > 0)
			{
				const FContentBrowserItem& OldSelectedItem = OldSelectedItems[0];
				UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();

				if ((!bDisplayEmpty && !ContentBrowserData->IsFolderVisibleIfHidingEmpty(OldSelectedItem.GetVirtualPath())) ||
					(!bDisplayDev && ContentBrowserUtils::IsItemDeveloperContent(OldSelectedItem)) ||
					(!bDisplayEngine && ContentBrowserUtils::IsItemEngineContent(OldSelectedItem)) ||
					(!bDisplayPlugins && ContentBrowserUtils::IsItemPluginContent(OldSelectedItem)) ||
					(!bDisplayL10N && ContentBrowserUtils::IsItemLocalizedContent(OldSelectedItem))
					)
				{
					// Set the folder back to the root, and refresh the contents
					TSharedPtr<FTreeItem> GameRoot = FindItemRecursive(TEXT("/Game"));
					if (GameRoot.IsValid())
					{
						TreeViewPtr->SetSelection(GameRoot);
					}
					else
					{
						TreeViewPtr->ClearSelection();
					}
				}
			}
		}

		// Update our path view so that it can include/exclude the dev folder
		Populate();

		// If the dev or engine folder has become visible and we're inside it...
		if (bDisplayDev || bDisplayEngine || bDisplayPlugins || bDisplayL10N)
		{
			const TArray<FContentBrowserItem> NewSelectedItems = GetSelectedFolderItems();
			if (NewSelectedItems.Num() > 0)
			{
				const FContentBrowserItem& NewSelectedItem = NewSelectedItems[0];

				if ((bDisplayDev && ContentBrowserUtils::IsItemDeveloperContent(NewSelectedItem)) ||
					(bDisplayEngine && ContentBrowserUtils::IsItemEngineContent(NewSelectedItem)) ||
					(bDisplayPlugins && ContentBrowserUtils::IsItemPluginContent(NewSelectedItem)) ||
					(bDisplayL10N && ContentBrowserUtils::IsItemLocalizedContent(NewSelectedItem))
					)
				{
					// Refresh the contents
					OnItemSelectionChanged.ExecuteIfBound(NewSelectedItem, ESelectInfo::Direct);
				}
			}
		}
	}
}


void SFavoritePathView::Construct(const FArguments& InArgs)
{
	SAssignNew(TreeViewPtr, STreeView< TSharedPtr<FTreeItem> >)
		.TreeItemsSource(&TreeRootItems)
		.OnGetChildren(this, &SFavoritePathView::GetChildrenForTree)
		.OnGenerateRow(this, &SFavoritePathView::GenerateTreeRow)
		.OnItemScrolledIntoView(this, &SFavoritePathView::TreeItemScrolledIntoView)
		.ItemHeight(18)
		.SelectionMode(InArgs._SelectionMode)
		.OnSelectionChanged(this, &SFavoritePathView::TreeSelectionChanged)
		.OnContextMenuOpening(this, &SFavoritePathView::MakePathViewContextMenu)
		.ClearSelectionOnClick(false);

	// Bind the favorites menu to update after folder changes
	AssetViewUtils::OnFolderPathChanged().AddSP(this, &SFavoritePathView::FixupFavoritesFromExternalChange);

	SPathView::Construct(InArgs);
}

void SFavoritePathView::Populate(const bool bIsRefreshingFilter)
{
	// Don't allow the selection changed delegate to be fired here
	FScopedPreventTreeItemChangedDelegate DelegatePrevention(SharedThis(this));

	// Clear all root items and clear selection
	TreeRootItems.Empty();
	TreeViewPtr->ClearSelection();

	const TArray<FString>& FavoritePaths = ContentBrowserUtils::GetFavoriteFolders();
	if (FavoritePaths.Num() > 0)
	{
		UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
		const FContentBrowserDataCompiledFilter CompiledDataFilter = CreateCompiledFolderFilter();

		for (const FString& Path : FavoritePaths)
		{
			// Use the whole path so we deliberately include any children of matched parents in the filtered list
			if (SearchBoxFolderFilter->PassesFilter(Path))
			{
				ContentBrowserData->EnumerateItemsAtPath(*Path, CompiledDataFilter.ItemTypeFilter, [this, &CompiledDataFilter](FContentBrowserItemData&& InItemData)
				{
					UContentBrowserDataSource* ItemDataSource = InItemData.GetOwnerDataSource();
					if (ItemDataSource->DoesItemPassFilter(InItemData, CompiledDataFilter))
					{
						if (TSharedPtr<FTreeItem> Item = AddFolderItem(MoveTemp(InItemData)))
						{
							const bool bSelectedItem = LastSelectedPaths.Contains(Item->GetItem().GetVirtualPath());

							if (bSelectedItem)
							{
								// Tree items that match the last broadcasted paths should be re-selected them after they are added
								TreeViewPtr->SetItemSelection(Item, true);
								TreeViewPtr->RequestScrollIntoView(Item);
							}
						}
					}

					return true;
				});
			}
		}
	}

	SortRootItems();
}

void SFavoritePathView::SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const
{
	SPathView::SaveSettings(IniFilename, IniSection, SettingsString);

	FString FavoritePathsString;
	const TArray<FString>& FavoritePaths = ContentBrowserUtils::GetFavoriteFolders();
	for (const FString& PathIt : FavoritePaths)
	{
		if (FavoritePathsString.Len() > 0)
		{
			FavoritePathsString += TEXT(",");
		}

		FavoritePathsString += PathIt;
	}

	GConfig->SetString(*IniSection, TEXT("FavoritePaths"), *FavoritePathsString, IniFilename);
}

void SFavoritePathView::LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString)
{
	SPathView::LoadSettings(IniFilename, IniSection, SettingsString);

	// We clear the initial selection for the favorite view, as it conflicts with the main paths view and results in a phantomly selected favorite item
	ClearSelection();

	// Favorite Paths
	FString FavoritePathsString;
	TArray<FString> NewFavoritePaths;
	if (GConfig->GetString(*IniSection, TEXT("FavoritePaths"), FavoritePathsString, IniFilename))
	{
		FavoritePathsString.ParseIntoArray(NewFavoritePaths, TEXT(","), /*bCullEmpty*/true);
	}

	if (NewFavoritePaths.Num() > 0)
	{
		// Keep track if we changed at least one source so we know to fire the bulk selection changed delegate later
		bool bAddedAtLeastOnePath = false;
		{
			// If the selected paths is empty, the path was "All assets"
			// This should handle that case properly
			for (int32 PathIdx = 0; PathIdx < NewFavoritePaths.Num(); ++PathIdx)
			{
				const FString& Path = NewFavoritePaths[PathIdx];
				ContentBrowserUtils::AddFavoriteFolder(Path, false);
				bAddedAtLeastOnePath = true;
			}
		}

		if (bAddedAtLeastOnePath)
		{
			Populate();
		}
	}
}

TSharedPtr<FTreeItem> SFavoritePathView::AddFolderItem(FContentBrowserItemData&& InItem, const bool bUserNamed)
{
	if (!ensure(TreeViewPtr.IsValid()))
	{
		// No tree view for some reason
		return nullptr;
	}

	// The favorite view will add all items at the root level

	// Try and find an existing tree item
	for (const TSharedPtr<FTreeItem>& PotentialTreeItem : TreeRootItems)
	{
		if (PotentialTreeItem->GetItem().GetVirtualPath() == InItem.GetVirtualPath())
		{
			// Found a match - merge the new item data
			PotentialTreeItem->AppendItemData(InItem);
			return PotentialTreeItem;
		}
	}

	// No match - create a new item
	TSharedPtr<FTreeItem> CurrentTreeItem = MakeShared<FTreeItem>(MoveTemp(InItem));
	TreeRootItems.Add(CurrentTreeItem);
	//TreeViewPtr->SetSelection(CurrentTreeItem);
	TreeViewPtr->RequestTreeRefresh();
	return CurrentTreeItem;
}

TSharedRef<ITableRow> SFavoritePathView::GenerateTreeRow(TSharedPtr<FTreeItem> TreeItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	check(TreeItem.IsValid());

	return
		SNew( STableRow< TSharedPtr<FTreeItem> >, OwnerTable )
		.OnDragDetected( this, &SFavoritePathView::OnFolderDragDetected )
		[
			SNew(SAssetTreeItem)
			.TreeItem(TreeItem)
			.OnNameChanged(this, &SFavoritePathView::FolderNameChanged)
			.OnVerifyNameChanged(this, &SFavoritePathView::VerifyFolderNameChanged)
			.IsItemExpanded(false)
			.HighlightText(this, &SFavoritePathView::GetHighlightText)
			.IsSelected(this, &SFavoritePathView::IsTreeItemSelected, TreeItem)
			.FontOverride(FEditorStyle::GetFontStyle("ContentBrowser.SourceTreeItemFont"))
		];
}

void SFavoritePathView::HandleItemDataUpdated(TArrayView<const FContentBrowserItemDataUpdate> InUpdatedItems)
{
	if (InUpdatedItems.Num() == 0)
	{
		return;
	}

	TSet<FName> FavoritePaths;
	{
		const TArray<FString>& FavoritePathStrs = ContentBrowserUtils::GetFavoriteFolders();
		for (const FString& Path : FavoritePathStrs)
		{
			FavoritePaths.Add(*Path);
		}
	}
	if (FavoritePaths.Num() == 0)
	{
		return;
	}

	// Don't allow the selection changed delegate to be fired here
	FScopedPreventTreeItemChangedDelegate DelegatePrevention(SharedThis(this));

	const double HandleItemDataUpdatedStartTime = FPlatformTime::Seconds();

	const bool bFilteringByText = !SearchBoxFolderFilter->GetRawFilterText().IsEmpty();

	// We defer this compilation as it's quite expensive due to being recursive, and not all updates will contain new folders
	bool bHasCompiledDataFilter = false;
	FContentBrowserDataCompiledFilter CompiledDataFilter;
	auto ConditionalCompileFilter = [this, &bHasCompiledDataFilter, &CompiledDataFilter]()
	{
		if (!bHasCompiledDataFilter)
		{
			bHasCompiledDataFilter = true;
			CompiledDataFilter = CreateCompiledFolderFilter();
		}
	};

	auto DoesItemPassFilter = [this, bFilteringByText, &CompiledDataFilter, &FavoritePaths](const FContentBrowserItemData& InItemData)
	{
		if (!FavoritePaths.Contains(InItemData.GetVirtualPath()))
		{
			return false;
		}

		UContentBrowserDataSource* ItemDataSource = InItemData.GetOwnerDataSource();
		if (!ItemDataSource->DoesItemPassFilter(InItemData, CompiledDataFilter))
		{
			return false;
		}

		if (bFilteringByText)
		{
			// Use the whole path so we deliberately include any children of matched parents in the filtered list
			const FString PathStr = InItemData.GetVirtualPath().ToString();
			if (!SearchBoxFolderFilter->PassesFilter(PathStr))
			{
				return false;
			}
		}

		return true;
	};

	for (const FContentBrowserItemDataUpdate& ItemDataUpdate : InUpdatedItems)
	{
		const FContentBrowserItemData& ItemData = ItemDataUpdate.GetItemData();
		if (!ItemData.IsFolder())
		{
			continue;
		}

		ConditionalCompileFilter();

		switch (ItemDataUpdate.GetUpdateType())
		{
		case EContentBrowserItemUpdateType::Added:
		case EContentBrowserItemUpdateType::Modified:
			if (DoesItemPassFilter(ItemData))
			{
				AddFolderItem(CopyTemp(ItemData));
			}
			else
			{
				RemoveFolderItem(ItemData);
			}
			break;

		case EContentBrowserItemUpdateType::Moved:
		{
			const FContentBrowserItemData OldMinimalItemData(ItemData.GetOwnerDataSource(), ItemData.GetItemType(), ItemDataUpdate.GetPreviousVirtualPath(), NAME_None, FText(), nullptr);
			RemoveFolderItem(OldMinimalItemData);

			if (DoesItemPassFilter(ItemData))
			{
				AddFolderItem(CopyTemp(ItemData));
			}

			ContentBrowserUtils::RemoveFavoriteFolder(ItemDataUpdate.GetPreviousVirtualPath().ToString());
		}
		break;

		case EContentBrowserItemUpdateType::Removed:
			RemoveFolderItem(ItemData);
			ContentBrowserUtils::RemoveFavoriteFolder(ItemData.GetVirtualPath().ToString());
			break;

		default:
			checkf(false, TEXT("Unexpected EContentBrowserItemUpdateType!"));
			break;
		}
	}

	UE_LOG(LogContentBrowser, VeryVerbose, TEXT("FavoritePathView - HandleItemDataUpdated completed in %0.4f seconds for %d items"), FPlatformTime::Seconds() - HandleItemDataUpdatedStartTime, InUpdatedItems.Num());
}

bool SFavoritePathView::PathIsFilteredFromViewBySearch(const FString& InPath) const
{
	return SPathView::PathIsFilteredFromViewBySearch(InPath)
		&& ContentBrowserUtils::IsFavoriteFolder(InPath);
}

void SFavoritePathView::FixupFavoritesFromExternalChange(TArrayView<const AssetViewUtils::FMovedContentFolder> MovedFolders)
{
	for (const AssetViewUtils::FMovedContentFolder& MovedFolder : MovedFolders)
	{
		const bool bWasFavorite = ContentBrowserUtils::IsFavoriteFolder(MovedFolder.Key);
		if (bWasFavorite)
		{
			// Remove the original path
			ContentBrowserUtils::RemoveFavoriteFolder(MovedFolder.Key, false);

			// Add the new path to favorites instead
			const FString& NewPath = MovedFolder.Value;
			ContentBrowserUtils::AddFavoriteFolder(NewPath, false);
			TSharedPtr<FTreeItem> Item = FindItemRecursive(*NewPath);
			if (Item.IsValid())
			{
				TreeViewPtr->SetItemSelection(Item, true);
				TreeViewPtr->RequestScrollIntoView(Item);
			}
		}
	}
	Populate();
}

#undef LOCTEXT_NAMESPACE
