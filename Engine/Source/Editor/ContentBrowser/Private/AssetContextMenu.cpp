// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetContextMenu.h"
#include "AssetData.h"

#include "ToolMenus.h"
#include "ContentBrowserMenuContexts.h"
#include "ContentBrowserDataSource.h"

#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "ContentBrowserUtils.h"
#include "SAssetView.h"
#include "ContentBrowserModule.h"
#include "EditorStyleSet.h"
#include "HAL/FileManager.h"

#include "ICollectionManager.h"
#include "CollectionManagerModule.h"
#include "CollectionAssetManagement.h"

#include "Toolkits/GlobalEditorCommonCommands.h"
#include "Framework/Commands/GenericCommands.h"
#include "ContentBrowserCommands.h"

#define LOCTEXT_NAMESPACE "ContentBrowser"

FAssetContextMenu::FAssetContextMenu(const TWeakPtr<SAssetView>& InAssetView)
	: AssetView(InAssetView)
{
}

void FAssetContextMenu::BindCommands(TSharedPtr< FUICommandList >& Commands)
{
	Commands->MapAction(FGenericCommands::Get().Duplicate, FUIAction(
		FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteDuplicate),
		FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecuteDuplicate)
		));

	Commands->MapAction(FGlobalEditorCommonCommands::Get().FindInContentBrowser, FUIAction(
		FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteSyncToAssetTree),
		FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecuteSyncToAssetTree)
		));
}

TSharedRef<SWidget> FAssetContextMenu::MakeContextMenu(TArrayView<const FContentBrowserItem> InSelectedItems, const FSourcesData& InSourcesData, TSharedPtr< FUICommandList > InCommandList)
{
	SetSelectedItems(InSelectedItems);
	SourcesData = InSourcesData;

	// Cache any vars that are used in determining if you can execute any actions.
	// Useful for actions whose "CanExecute" will not change or is expensive to calculate.
	CacheCanExecuteVars();

	// Get all menu extenders for this context menu from the content browser module
	FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>( TEXT("ContentBrowser") );
	TArray<FContentBrowserMenuExtender_SelectedAssets> MenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();

	TSharedPtr<FExtender> MenuExtender;
	{
		TArray<FAssetData> SelectedAssets;
		for (const FContentBrowserItem& SelectedFile : SelectedFiles)
		{
			FAssetData ItemAssetData;
			if (SelectedFile.Legacy_TryGetAssetData(ItemAssetData))
			{
				SelectedAssets.Add(ItemAssetData);
			}
		}

		if (SelectedAssets.Num() > 0)
		{
			TArray<TSharedPtr<FExtender>> Extenders;
			for (int32 i = 0; i < MenuExtenderDelegates.Num(); ++i)
			{
				if (MenuExtenderDelegates[i].IsBound())
				{
					Extenders.Add(MenuExtenderDelegates[i].Execute(SelectedAssets));
				}
			}
			MenuExtender = FExtender::Combine(Extenders);
		}
	}

	UContentBrowserAssetContextMenuContext* ContextObject = NewObject<UContentBrowserAssetContextMenuContext>();
	ContextObject->AssetContextMenu = SharedThis(this);

	UToolMenus* ToolMenus = UToolMenus::Get();

	static const FName BaseMenuName("ContentBrowser.AssetContextMenu");
	static const FName ItemContextMenuName("ContentBrowser.ItemContextMenu");
	RegisterContextMenu(BaseMenuName);

	TArray<UObject*> SelectedObjects;

	// Create menu hierarchy based on class hierarchy
	FName MenuName = BaseMenuName;
	{
		// TODO: Ideally all of this asset specific stuff would happen in the asset data source, however we 
		// need to keep it here for now to build the correct menu name and register the correct extenders

		// Objects must be loaded for this operation... for now
		TArray<FString> ObjectPaths;
		UContentBrowserDataSource* CommonDataSource = nullptr;
		bool bKeepCheckingCommonDataSource = true;
		for (const FContentBrowserItem& SelectedItem : SelectedItems)
		{
			if (bKeepCheckingCommonDataSource)
			{
				if (const FContentBrowserItemData* PrimaryInternalItem = SelectedItem.GetPrimaryInternalItem())
				{
					if (UContentBrowserDataSource* OwnerDataSource = PrimaryInternalItem->GetOwnerDataSource())
					{
						if (CommonDataSource == nullptr)
						{
							CommonDataSource = OwnerDataSource;
						}
						else if (CommonDataSource != OwnerDataSource)
						{
							CommonDataSource = nullptr;
							bKeepCheckingCommonDataSource = false;
						}
					}
				}
			}

			FAssetData ItemAssetData;
			if (SelectedItem.Legacy_TryGetAssetData(ItemAssetData))
			{
				ObjectPaths.Add(ItemAssetData.ObjectPath.ToString());
			}
		}

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		const TSharedRef<FBlacklistPaths>& WritableFolderFilter = AssetToolsModule.Get().GetWritableFolderBlacklist();

		ContextObject->bCanBeModified = ObjectPaths.Num() == 0;

		ContextObject->SelectedObjects.Reset();
		if (ContentBrowserUtils::LoadAssetsIfNeeded(ObjectPaths, SelectedObjects) && SelectedObjects.Num() > 0)
		{
			ContextObject->SelectedObjects.Append(SelectedObjects);

			// Find common class for selected objects
			UClass* CommonClass = SelectedObjects[0]->GetClass();
			for (int32 ObjIdx = 1; ObjIdx < SelectedObjects.Num(); ++ObjIdx)
			{
				while (!SelectedObjects[ObjIdx]->IsA(CommonClass))
				{
					CommonClass = CommonClass->GetSuperClass();
				}
			}
			ContextObject->CommonClass = CommonClass;

			ContextObject->bCanBeModified = true;

			if (WritableFolderFilter->HasFiltering())
			{
				for (const UObject* SelectedObject : SelectedObjects)
				{
					if (SelectedObject)
					{
						UPackage* SelectedObjectPackage = SelectedObject->GetOutermost();
						if (SelectedObjectPackage && !WritableFolderFilter->PassesStartsWithFilter(SelectedObjectPackage->GetFName()))
						{
							ContextObject->bCanBeModified = false;
							break;
						}
					}
				}
			}

			MenuName = UToolMenus::JoinMenuPaths(BaseMenuName, CommonClass->GetFName());

			RegisterMenuHierarchy(CommonClass);

			// Find asset actions for common class
			TSharedPtr<IAssetTypeActions> CommonAssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(ContextObject->CommonClass).Pin();
			if (CommonAssetTypeActions.IsValid() && CommonAssetTypeActions->HasActions(SelectedObjects))
			{
				ContextObject->CommonAssetTypeActions = CommonAssetTypeActions;
			}
		}
		else if (SelectedObjects.Num() == 0)
		{
			if (CommonDataSource)
			{
				ContextObject->bCanBeModified = true;

				if (WritableFolderFilter->HasFiltering())
				{
					for (const FContentBrowserItem& SelectedItem : SelectedItems)
					{
						if (!WritableFolderFilter->PassesStartsWithFilter(SelectedItem.GetVirtualPath()))
						{
							ContextObject->bCanBeModified = false;
							break;
						}
					}
				}

				MenuName = UToolMenus::JoinMenuPaths(ItemContextMenuName, CommonDataSource->GetFName());

				if (!ToolMenus->IsMenuRegistered(MenuName))
				{
					ToolMenus->RegisterMenu(MenuName, BaseMenuName);
				}
			}
		}
	}

	FToolMenuContext MenuContext(InCommandList, MenuExtender, ContextObject);

	{
		UContentBrowserDataMenuContext_FileMenu* DataContextObject = NewObject<UContentBrowserDataMenuContext_FileMenu>();
		DataContextObject->SelectedItems = SelectedItems;
		DataContextObject->SelectedCollections = SourcesData.Collections;
		DataContextObject->bCanBeModified = ContextObject->bCanBeModified;
		DataContextObject->ParentWidget = AssetView;
		DataContextObject->OnShowInPathsView = OnShowInPathsViewRequested;
		DataContextObject->OnRefreshView = OnAssetViewRefreshRequested;
		MenuContext.AddObject(DataContextObject);
	}

	return ToolMenus->GenerateWidget(MenuName, MenuContext);
}

void FAssetContextMenu::RegisterMenuHierarchy(UClass* InClass)
{
	static const FName BaseMenuName("ContentBrowser.AssetContextMenu");

	UToolMenus* ToolMenus = UToolMenus::Get();

	for (UClass* CurrentClass = InClass; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
	{
		FName CurrentMenuName = UToolMenus::JoinMenuPaths(BaseMenuName, CurrentClass->GetFName());
		if (!ToolMenus->IsMenuRegistered(CurrentMenuName))
		{
			FName ParentMenuName;
			UClass* ParentClass = CurrentClass->GetSuperClass();
			if (ParentClass == UObject::StaticClass() || ParentClass == nullptr)
			{
				ParentMenuName = BaseMenuName;
			}
			else
			{
				ParentMenuName = UToolMenus::JoinMenuPaths(BaseMenuName, ParentClass->GetFName());
			}

			ToolMenus->RegisterMenu(CurrentMenuName, ParentMenuName);

			if (ParentMenuName == BaseMenuName)
			{
				break;
			}
		}
	}
}

void FAssetContextMenu::RegisterContextMenu(const FName MenuName)
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus->IsMenuRegistered(MenuName))
	{
		UToolMenu* Menu = ToolMenus->RegisterMenu(MenuName);
		FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");

		Section.AddDynamicEntry("GetActions", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
			if (Context && Context->CommonAssetTypeActions.IsValid())
			{
				Context->CommonAssetTypeActions.Pin()->GetActions(Context->GetSelectedObjects(), InSection);
			}
		}));

		Section.AddDynamicEntry("GetActionsLegacy", FNewToolMenuDelegateLegacy::CreateLambda([](FMenuBuilder& MenuBuilder, UToolMenu* InMenu)
		{
			UContentBrowserAssetContextMenuContext* Context = InMenu->FindContext<UContentBrowserAssetContextMenuContext>();
			if (Context && Context->CommonAssetTypeActions.IsValid())
			{
				Context->CommonAssetTypeActions.Pin()->GetActions(Context->GetSelectedObjects(), MenuBuilder);
			}
		}));

		Menu->AddDynamicSection("AddMenuOptions", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
		{
			UContentBrowserAssetContextMenuContext* Context = InMenu->FindContext<UContentBrowserAssetContextMenuContext>();
			if (Context && Context->AssetContextMenu.IsValid())
			{
				Context->AssetContextMenu.Pin()->AddMenuOptions(InMenu);
			}
		}));
	}
}

void FAssetContextMenu::AddMenuOptions(UToolMenu* InMenu)
{
	UContentBrowserDataMenuContext_FileMenu* Context = InMenu->FindContext<UContentBrowserDataMenuContext_FileMenu>();
	const bool bCanBeModified = !Context || Context->bCanBeModified;

	// Add any type-specific context menu options
	AddAssetTypeMenuOptions(InMenu);

	// Add quick access to common commands.
	AddCommonMenuOptions(InMenu);

	// Add quick access to view commands
	AddExploreMenuOptions(InMenu);

	// Add reference options
	AddReferenceMenuOptions(InMenu);

	// Add collection options
	if (bCanBeModified)
	{
		AddCollectionMenuOptions(InMenu);
	}
}

void FAssetContextMenu::SetSelectedItems(TArrayView<const FContentBrowserItem> InSelectedItems)
{
	SelectedItems.Reset();
	SelectedItems.Append(InSelectedItems.GetData(), InSelectedItems.Num());

	SelectedFiles.Reset();
	SelectedFolders.Reset();
	for (const FContentBrowserItem& SelectedItem : SelectedItems)
	{
		if (SelectedItem.IsFile())
		{
			SelectedFiles.Add(SelectedItem);
		}

		if (SelectedItem.IsFolder())
		{
			SelectedFolders.Add(SelectedItem);
		}
	}
}

void FAssetContextMenu::SetOnShowInPathsViewRequested(const FOnShowInPathsViewRequested& InOnShowInPathsViewRequested)
{
	OnShowInPathsViewRequested = InOnShowInPathsViewRequested;
}

void FAssetContextMenu::SetOnRenameRequested(const FOnRenameRequested& InOnRenameRequested)
{
	OnRenameRequested = InOnRenameRequested;
}

void FAssetContextMenu::SetOnDuplicateRequested(const FOnDuplicateRequested& InOnDuplicateRequested)
{
	OnDuplicateRequested = InOnDuplicateRequested;
}

void FAssetContextMenu::SetOnEditRequested(const FOnEditRequested& InOnEditRequested)
{
	OnEditRequested = InOnEditRequested;
}

void FAssetContextMenu::SetOnAssetViewRefreshRequested(const FOnAssetViewRefreshRequested& InOnAssetViewRefreshRequested)
{
	OnAssetViewRefreshRequested = InOnAssetViewRefreshRequested;
}

bool FAssetContextMenu::AddCommonMenuOptions(UToolMenu* Menu)
{
	UContentBrowserDataMenuContext_FileMenu* Context = Menu->FindContext<UContentBrowserDataMenuContext_FileMenu>();
	const bool bCanBeModified = !Context || Context->bCanBeModified;

	{
		FToolMenuSection& Section = Menu->AddSection("CommonAssetActions", LOCTEXT("CommonAssetActionsMenuHeading", "Common"));

		
		if (bCanBeModified)
		{
			// Edit
			Section.AddMenuEntry(
				"EditAsset",
				LOCTEXT("EditAsset", "Edit..."),
				LOCTEXT("EditAssetTooltip", "Opens the selected item(s) for edit."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.Edit"),
				FUIAction(
					FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteEditItems),
					FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecuteEditItems)
				)
			);

			// Rename
			Section.AddMenuEntry(FGenericCommands::Get().Rename,
				LOCTEXT("Rename", "Rename"),
				LOCTEXT("RenameTooltip", "Rename the selected item."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.Rename")
			);

			// Duplicate
			Section.AddMenuEntry(FGenericCommands::Get().Duplicate,
				LOCTEXT("Duplicate", "Duplicate"),
				LOCTEXT("DuplicateTooltip", "Create a copy of the selected item(s)."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.Duplicate")
			);

			// Save
			Section.AddMenuEntry(FContentBrowserCommands::Get().SaveSelectedAsset,
				LOCTEXT("SaveAsset", "Save"),
				LOCTEXT("SaveAssetTooltip", "Saves the item to file."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "Level.SaveIcon16x")
			);

			// Delete
			Section.AddMenuEntry(FGenericCommands::Get().Delete,
				LOCTEXT("Delete", "Delete"),
				LOCTEXT("DeleteTooltip", "Delete the selected items."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.Delete")
			);
		}
	}

	return true;
}

void FAssetContextMenu::AddExploreMenuOptions(UToolMenu* Menu)
{
	FToolMenuSection& Section = Menu->AddSection("AssetContextExploreMenuOptions", LOCTEXT("AssetContextExploreMenuOptionsHeading", "Explore"));
	{
		// Find in Content Browser
		Section.AddMenuEntry(
			FGlobalEditorCommonCommands::Get().FindInContentBrowser, 
			LOCTEXT("ShowInFolderView", "Show in Folder View"),
			LOCTEXT("ShowInFolderViewTooltip", "Selects the folder that contains this asset in the Content Browser Sources Panel.")
			);

		// Find in Explorer
		Section.AddMenuEntry(
			"FindInExplorer",
			ContentBrowserUtils::GetExploreFolderText(),
			LOCTEXT("FindInExplorerTooltip", "Finds this asset on disk"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SystemWideCommands.FindInContentBrowser"),
			FUIAction(
				FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteFindInExplorer),
				FCanExecuteAction::CreateSP(this, &FAssetContextMenu::CanExecuteFindInExplorer)
			)
		);
	}
}

bool FAssetContextMenu::AddReferenceMenuOptions(UToolMenu* Menu)
{
	UContentBrowserDataMenuContext_FileMenu* Context = Menu->FindContext<UContentBrowserDataMenuContext_FileMenu>();

	{
		FToolMenuSection& Section = Menu->AddSection("AssetContextReferences", LOCTEXT("ReferencesMenuHeading", "References"));

		Section.AddMenuEntry(
			"CopyReference",
			LOCTEXT("CopyReference", "Copy Reference"),
			LOCTEXT("CopyReferenceTooltip", "Copies reference paths for the selected assets to the clipboard."),
			FSlateIcon(),
			FUIAction( FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteCopyReference ) )
			);
	
		if (Context->bCanBeModified)
		{
			Section.AddMenuEntry(
				"CopyFilePath",
				LOCTEXT("CopyFilePath", "Copy File Path"),
				LOCTEXT("CopyFilePathTooltip", "Copies the file paths on disk for the selected assets to the clipboard."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &FAssetContextMenu::ExecuteCopyFilePath))
			);
		}
	}

	return true;
}

bool FAssetContextMenu::AddAssetTypeMenuOptions(UToolMenu* Menu)
{
	bool bAnyTypeOptions = false;

	UContentBrowserAssetContextMenuContext* Context = Menu->FindContext<UContentBrowserAssetContextMenuContext>();
	if (Context && Context->SelectedObjects.Num() > 0)
	{
		// Label "GetAssetActions" section
		FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
		if (Context->CommonAssetTypeActions.IsValid())
		{
			Section.Label = FText::Format(NSLOCTEXT("AssetTools", "AssetSpecificOptionsMenuHeading", "{0} Actions"), Context->CommonAssetTypeActions.Pin()->GetName());
		}
		else if (Context->CommonClass)
		{
			Section.Label = FText::Format(NSLOCTEXT("AssetTools", "AssetSpecificOptionsMenuHeading", "{0} Actions"), FText::FromName(Context->CommonClass->GetFName()));
		}
		else
		{
			Section.Label = FText::Format(NSLOCTEXT("AssetTools", "AssetSpecificOptionsMenuHeading", "{0} Actions"), FText::FromString(TEXT("Asset")));
		}

		bAnyTypeOptions = true;
	}

	return bAnyTypeOptions;
}

bool FAssetContextMenu::AddCollectionMenuOptions(UToolMenu* Menu)
{
	class FManageCollectionsContextMenu
	{
	public:
		static void CreateManageCollectionsSubMenu(UToolMenu* SubMenu, TSharedRef<FCollectionAssetManagement> QuickAssetManagement)
		{
			FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();

			TArray<FCollectionNameType> AvailableCollections;
			CollectionManagerModule.Get().GetRootCollections(AvailableCollections);

			CreateManageCollectionsSubMenu(SubMenu, QuickAssetManagement, MoveTemp(AvailableCollections));
		}

		static void CreateManageCollectionsSubMenu(UToolMenu* SubMenu, TSharedRef<FCollectionAssetManagement> QuickAssetManagement, TArray<FCollectionNameType> AvailableCollections)
		{
			FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();

			AvailableCollections.Sort([](const FCollectionNameType& One, const FCollectionNameType& Two) -> bool
			{
				return One.Name.LexicalLess(Two.Name);
			});

			FToolMenuSection& Section = SubMenu->AddSection("Section");
			for (const FCollectionNameType& AvailableCollection : AvailableCollections)
			{
				// Never display system collections
				if (AvailableCollection.Type == ECollectionShareType::CST_System)
				{
					continue;
				}

				// Can only manage assets for static collections
				ECollectionStorageMode::Type StorageMode = ECollectionStorageMode::Static;
				CollectionManagerModule.Get().GetCollectionStorageMode(AvailableCollection.Name, AvailableCollection.Type, StorageMode);
				if (StorageMode != ECollectionStorageMode::Static)
				{
					continue;
				}

				TArray<FCollectionNameType> AvailableChildCollections;
				CollectionManagerModule.Get().GetChildCollections(AvailableCollection.Name, AvailableCollection.Type, AvailableChildCollections);

				if (AvailableChildCollections.Num() > 0)
				{
					Section.AddSubMenu(
						NAME_None,
						FText::FromName(AvailableCollection.Name), 
						FText::GetEmpty(), 
						FNewToolMenuDelegate::CreateStatic(&FManageCollectionsContextMenu::CreateManageCollectionsSubMenu, QuickAssetManagement, AvailableChildCollections),
						FUIAction(
							FExecuteAction::CreateStatic(&FManageCollectionsContextMenu::OnCollectionClicked, QuickAssetManagement, AvailableCollection),
							FCanExecuteAction::CreateStatic(&FManageCollectionsContextMenu::IsCollectionEnabled, QuickAssetManagement, AvailableCollection),
							FGetActionCheckState::CreateStatic(&FManageCollectionsContextMenu::GetCollectionCheckState, QuickAssetManagement, AvailableCollection)
							), 
						EUserInterfaceActionType::ToggleButton,
						false,
						FSlateIcon(FEditorStyle::GetStyleSetName(), ECollectionShareType::GetIconStyleName(AvailableCollection.Type))
						);
				}
				else
				{
					Section.AddMenuEntry(
						NAME_None,
						FText::FromName(AvailableCollection.Name), 
						FText::GetEmpty(), 
						FSlateIcon(FEditorStyle::GetStyleSetName(), ECollectionShareType::GetIconStyleName(AvailableCollection.Type)), 
						FUIAction(
							FExecuteAction::CreateStatic(&FManageCollectionsContextMenu::OnCollectionClicked, QuickAssetManagement, AvailableCollection),
							FCanExecuteAction::CreateStatic(&FManageCollectionsContextMenu::IsCollectionEnabled, QuickAssetManagement, AvailableCollection),
							FGetActionCheckState::CreateStatic(&FManageCollectionsContextMenu::GetCollectionCheckState, QuickAssetManagement, AvailableCollection)
							), 
						EUserInterfaceActionType::ToggleButton
						);
				}
			}
		}

	private:
		static bool IsCollectionEnabled(TSharedRef<FCollectionAssetManagement> QuickAssetManagement, FCollectionNameType InCollectionKey)
		{
			return QuickAssetManagement->IsCollectionEnabled(InCollectionKey);
		}

		static ECheckBoxState GetCollectionCheckState(TSharedRef<FCollectionAssetManagement> QuickAssetManagement, FCollectionNameType InCollectionKey)
		{
			return QuickAssetManagement->GetCollectionCheckState(InCollectionKey);
		}

		static void OnCollectionClicked(TSharedRef<FCollectionAssetManagement> QuickAssetManagement, FCollectionNameType InCollectionKey)
		{
			// The UI actions don't give you the new check state, so we need to emulate the behavior of SCheckBox
			// Basically, checked will transition to unchecked (removing items), and anything else will transition to checked (adding items)
			if (GetCollectionCheckState(QuickAssetManagement, InCollectionKey) == ECheckBoxState::Checked)
			{
				QuickAssetManagement->RemoveCurrentAssetsFromCollection(InCollectionKey);
			}
			else
			{
				QuickAssetManagement->AddCurrentAssetsToCollection(InCollectionKey);
			}
		}
	};

	bool bHasAddedItems = false;

	FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();

	FToolMenuSection& Section = Menu->AddSection("AssetContextCollections", LOCTEXT("AssetCollectionOptionsMenuHeading", "Collections"));

	// Show a sub-menu that allows you to quickly add or remove the current asset selection from the available collections
	if (CollectionManagerModule.Get().HasCollections())
	{
		TSharedRef<FCollectionAssetManagement> QuickAssetManagement = MakeShared<FCollectionAssetManagement>();

		TArray<FName> SelectedItemCollectionIds;
		for (const FContentBrowserItem& SelectedItem : SelectedFiles)
		{
			FName ItemCollectionId;
			if (SelectedItem.TryGetCollectionId(ItemCollectionId))
			{
				SelectedItemCollectionIds.Add(ItemCollectionId);
			}
		}
		QuickAssetManagement->SetCurrentAssetPaths(SelectedItemCollectionIds);

		Section.AddSubMenu(
			"ManageCollections",
			LOCTEXT("ManageCollections", "Manage Collections"),
			FText::Format(LOCTEXT("ManageCollections_ToolTip", "Manage the collections that the selected {0}|plural(one=item belongs, other=items belong) to."), SelectedFiles.Num()),
			FNewToolMenuDelegate::CreateStatic(&FManageCollectionsContextMenu::CreateManageCollectionsSubMenu, QuickAssetManagement)
			);

		bHasAddedItems = true;
	}

	// "Remove from collection" (only display option if exactly one collection is selected)
	if ( SourcesData.Collections.Num() == 1 && !SourcesData.IsDynamicCollection() )
	{
		Section.AddMenuEntry(
			"RemoveFromCollection",
			FText::Format(LOCTEXT("RemoveFromCollectionFmt", "Remove From {0}"), FText::FromName(SourcesData.Collections[0].Name)),
			LOCTEXT("RemoveFromCollection_ToolTip", "Removes the selected item from the current collection."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &FAssetContextMenu::ExecuteRemoveFromCollection ),
				FCanExecuteAction::CreateSP( this, &FAssetContextMenu::CanExecuteRemoveFromCollection )
				)
			);

		bHasAddedItems = true;
	}


	return bHasAddedItems;
}

void FAssetContextMenu::ExecuteSyncToAssetTree()
{
	// Copy this as the sync may adjust our selected assets array
	const TArray<FContentBrowserItem> SelectedFilesCopy = SelectedFiles;
	OnShowInPathsViewRequested.ExecuteIfBound(SelectedFilesCopy);
}

void FAssetContextMenu::ExecuteFindInExplorer()
{
	for (const FContentBrowserItem& SelectedItem : SelectedFiles)
	{
		FString ItemFilename;
		if (SelectedItem.GetItemPhysicalPath(ItemFilename) && FPaths::FileExists(ItemFilename))
		{
			FPlatformProcess::ExploreFolder(*IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*ItemFilename));
		}
	}
}

bool FAssetContextMenu::CanExecuteEditItems() const
{
	bool bCanEdit = false;
	for (const FContentBrowserItem& SelectedItem : SelectedFiles)
	{
		bCanEdit |= SelectedItem.CanEdit();
	}
	return bCanEdit;
}

void FAssetContextMenu::ExecuteEditItems()
{
	if (SelectedFiles.Num() > 0)
	{
		OnEditRequested.ExecuteIfBound(SelectedFiles);
	}
}

void FAssetContextMenu::ExecuteSaveAsset()
{
	const EContentBrowserItemSaveFlags SaveFlags = EContentBrowserItemSaveFlags::SaveOnlyIfLoaded;

	// Batch these by their data sources
	TMap<UContentBrowserDataSource*, TArray<FContentBrowserItemData>> SourcesAndItems;
	for (const FContentBrowserItem& SelectedItem : SelectedFiles)
	{
		FContentBrowserItem::FItemDataArrayView ItemDataArray = SelectedItem.GetInternalItems();
		for (const FContentBrowserItemData& ItemData : ItemDataArray)
		{
			if (UContentBrowserDataSource* ItemDataSource = ItemData.GetOwnerDataSource())
			{
				FText SaveErrorMsg;
				if (ItemDataSource->CanSaveItem(ItemData, SaveFlags, &SaveErrorMsg))
				{
					TArray<FContentBrowserItemData>& ItemsForSource = SourcesAndItems.FindOrAdd(ItemDataSource);
					ItemsForSource.Add(ItemData);
				}
				else
				{
					AssetViewUtils::ShowErrorNotifcation(SaveErrorMsg);
				}
			}
		}
	}

	// Execute the operation now
	for (const auto& SourceAndItemsPair : SourcesAndItems)
	{
		SourceAndItemsPair.Key->BulkSaveItems(SourceAndItemsPair.Value, SaveFlags);
	}
}

void FAssetContextMenu::ExecuteDuplicate() 
{
	if (SelectedFiles.Num() > 0)
	{
		OnDuplicateRequested.ExecuteIfBound(SelectedFiles);
	}
}

void FAssetContextMenu::ExecuteRename(EContentBrowserViewContext ViewContext)
{
	if (SelectedItems.Num() == 1)
	{
		OnRenameRequested.ExecuteIfBound(SelectedItems[0], ViewContext);
	}
}

void FAssetContextMenu::ExecuteDelete()
{
	// Batch these by their data sources
	TMap<UContentBrowserDataSource*, TArray<FContentBrowserItemData>> SourcesAndItems;
	for (const FContentBrowserItem& SelectedItem : SelectedFiles)
	{
		FContentBrowserItem::FItemDataArrayView ItemDataArray = SelectedItem.GetInternalItems();
		for (const FContentBrowserItemData& ItemData : ItemDataArray)
		{
			if (UContentBrowserDataSource* ItemDataSource = ItemData.GetOwnerDataSource())
			{
				FText DeleteErrorMsg;
				if (ItemDataSource->CanDeleteItem(ItemData, &DeleteErrorMsg))
				{
					TArray<FContentBrowserItemData>& ItemsForSource = SourcesAndItems.FindOrAdd(ItemDataSource);
					ItemsForSource.Add(ItemData);
				}
				else
				{
					AssetViewUtils::ShowErrorNotifcation(DeleteErrorMsg);
				}
			}
		}
	}

	// Execute the operation now
	for (const auto& SourceAndItemsPair : SourcesAndItems)
	{
		SourceAndItemsPair.Key->BulkDeleteItems(SourceAndItemsPair.Value);
	}

	// If we had any folders selected, ask the user whether they want to delete them 
	// as it can be slow to build the deletion dialog on an accidental click
	if (SelectedFolders.Num() > 0)
	{
		FText Prompt;
		if (SelectedFolders.Num() == 1)
		{
			Prompt = FText::Format(LOCTEXT("FolderDeleteConfirm_Single", "Delete folder '{0}'?"), SelectedFolders[0].GetDisplayName());
		}
		else
		{
			Prompt = FText::Format(LOCTEXT("FolderDeleteConfirm_Multiple", "Delete {0} folders?"), SelectedFolders.Num());
		}

		// Spawn a confirmation dialog since this is potentially a highly destructive operation
		ContentBrowserUtils::DisplayConfirmationPopup(
			Prompt,
			LOCTEXT("FolderDeleteConfirm_Yes", "Delete"),
			LOCTEXT("FolderDeleteConfirm_No", "Cancel"),
			AssetView.Pin().ToSharedRef(),
			FOnClicked::CreateSP(this, &FAssetContextMenu::ExecuteDeleteFolderConfirmed)
		);
	}
}

FReply FAssetContextMenu::ExecuteDeleteFolderConfirmed()
{
	// Batch these by their data sources
	TMap<UContentBrowserDataSource*, TArray<FContentBrowserItemData>> SourcesAndItems;
	for (const FContentBrowserItem& SelectedItem : SelectedFolders)
	{
		FContentBrowserItem::FItemDataArrayView ItemDataArray = SelectedItem.GetInternalItems();
		for (const FContentBrowserItemData& ItemData : ItemDataArray)
		{
			if (UContentBrowserDataSource* ItemDataSource = ItemData.GetOwnerDataSource())
			{
				FText DeleteErrorMsg;
				if (ItemDataSource->CanDeleteItem(ItemData, &DeleteErrorMsg))
				{
					TArray<FContentBrowserItemData>& ItemsForSource = SourcesAndItems.FindOrAdd(ItemDataSource);
					ItemsForSource.Add(ItemData);
				}
				else
				{
					AssetViewUtils::ShowErrorNotifcation(DeleteErrorMsg);
				}
			}
		}
	}

	// Execute the operation now
	for (const auto& SourceAndItemsPair : SourcesAndItems)
	{
		SourceAndItemsPair.Key->BulkDeleteItems(SourceAndItemsPair.Value);
	}

	return FReply::Handled();
}

void FAssetContextMenu::ExecuteCopyReference()
{
	if (SelectedFiles.Num() > 0)
	{
		ContentBrowserUtils::CopyItemReferencesToClipboard(SelectedFiles);
	}
}

void FAssetContextMenu::ExecuteCopyFilePath()
{
	if (SelectedFiles.Num() > 0)
	{
		ContentBrowserUtils::CopyFilePathsToClipboard(SelectedFiles);
	}
}

void FAssetContextMenu::ExecuteRemoveFromCollection()
{
	if ( ensure(SourcesData.Collections.Num() == 1) )
	{
		TArray<FName> SelectedItemCollectionIds;
		for (const FContentBrowserItem& SelectedItem : SelectedFiles)
		{
			FName ItemCollectionId;
			if (SelectedItem.TryGetCollectionId(ItemCollectionId))
			{
				SelectedItemCollectionIds.Add(ItemCollectionId);
			}
		}

		if ( SelectedItemCollectionIds.Num() > 0 )
		{
			FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();

			const FCollectionNameType& Collection = SourcesData.Collections[0];
			CollectionManagerModule.Get().RemoveFromCollection(Collection.Name, Collection.Type, SelectedItemCollectionIds);
			OnAssetViewRefreshRequested.ExecuteIfBound();
		}
	}
}

bool FAssetContextMenu::CanExecuteSyncToAssetTree() const
{
	return SelectedFiles.Num() > 0;
}

bool FAssetContextMenu::CanExecuteFindInExplorer() const
{
	return bCanExecuteFindInExplorer;
}

bool FAssetContextMenu::CanExecuteRemoveFromCollection() const 
{
	return SourcesData.Collections.Num() == 1 && !SourcesData.IsDynamicCollection();
}

bool FAssetContextMenu::CanExecuteDuplicate() const
{
	bool bCanDuplicate = false;
	for (const FContentBrowserItem& SelectedItem : SelectedFiles)
	{
		bCanDuplicate |= SelectedItem.CanDuplicate();
	}
	return bCanDuplicate;
}

bool FAssetContextMenu::CanExecuteRename() const
{
	return ContentBrowserUtils::CanRenameFromAssetView(AssetView);
}

bool FAssetContextMenu::CanExecuteDelete() const
{
	return ContentBrowserUtils::CanDeleteFromAssetView(AssetView);
}

bool FAssetContextMenu::CanExecuteSaveAsset() const
{
	bool bCanSave = false;
	for (const FContentBrowserItem& SelectedItem : SelectedFiles)
	{
		bCanSave |= SelectedItem.CanSave(EContentBrowserItemSaveFlags::SaveOnlyIfLoaded);
	}
	return bCanSave;
}

void FAssetContextMenu::CacheCanExecuteVars()
{
	bCanExecuteFindInExplorer = false;

	// Selection must contain at least one file that has exists on disk
	for (const FContentBrowserItem& SelectedItem : SelectedFiles)
	{
		FString ItemFilename;
		if (SelectedItem.GetItemPhysicalPath(ItemFilename) && FPaths::FileExists(ItemFilename))
		{
			bCanExecuteFindInExplorer = true;
			break;
		}
	}
}

#undef LOCTEXT_NAMESPACE
