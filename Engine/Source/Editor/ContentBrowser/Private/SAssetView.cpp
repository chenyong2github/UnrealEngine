// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAssetView.h"
#include "Algo/Transform.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "Widgets/SOverlay.h"
#include "Engine/GameViewportClient.h"
#include "Factories/Factory.h"
#include "Framework/Commands/UIAction.h"
#include "Textures/SlateIcon.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "SlateOptMacros.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSlider.h"
#include "Framework/Docking/TabManager.h"
#include "EditorStyleSet.h"
#include "Settings/ContentBrowserSettings.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "AssetSelection.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "ContentBrowserLog.h"
#include "FrontendFilterBase.h"
#include "ContentBrowserSingleton.h"
#include "EditorWidgetsModule.h"
#include "AssetViewTypes.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "DragDropHandler.h"
#include "AssetViewWidgets.h"
#include "ContentBrowserModule.h"
#include "ObjectTools.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Layout/SSplitter.h"
#include "HAL/PlatformApplicationMisc.h"
#include "DesktopPlatformModule.h"
#include "Misc/FileHelper.h"
#include "Misc/TextFilterUtils.h"
#include "Misc/BlacklistNames.h"
#include "AssetRegistryState.h"
#include "Materials/Material.h"
#include "ContentBrowserMenuContexts.h"
#include "ContentBrowserUtils.h"
#include "ToolMenus.h"

#include "IContentBrowserDataModule.h"
#include "ContentBrowserDataSource.h"
#include "ContentBrowserDataSubsystem.h"
#include "ContentBrowserDataLegacyBridge.h"
#include "ContentBrowserDataDragDropOp.h"

#define LOCTEXT_NAMESPACE "ContentBrowser"
#define MAX_THUMBNAIL_SIZE 4096

#define ASSET_VIEW_PARANOIA_LIST_CHECKS (0)
#if ASSET_VIEW_PARANOIA_LIST_CHECKS
	#define checkAssetList(cond) check(cond)
#else
	#define checkAssetList(cond)
#endif

namespace
{
	/** Time delay between recently added items being added to the filtered asset items list */
	const double TimeBetweenAddingNewAssets = 4.0;

	/** Time delay between performing the last jump, and the jump term being reset */
	const double JumpDelaySeconds = 2.0;
}

class FAssetViewFrontendFilterHelper
{
public:
	explicit FAssetViewFrontendFilterHelper(SAssetView* InAssetView)
		: AssetView(InAssetView)
		, ContentBrowserData(IContentBrowserDataModule::Get().GetSubsystem())
		, bDisplayEmptyFolders(AssetView->IsShowingEmptyFolders())
	{
	}

	bool DoesItemPassQueryFilter(const TSharedPtr<FAssetViewItem>& InItemToFilter)
	{
		// Folders aren't subject to additional filtering
		if (InItemToFilter->IsFolder())
		{
			return true;
		}

		// If we have OnShouldFilterAsset then it is assumed that we really only want to see true assets and 
		// nothing else so only include things that have asset data and also pass the query filter
		FAssetData ItemAssetData;
		if (InItemToFilter->GetItem().Legacy_TryGetAssetData(ItemAssetData))
		{
			if (!AssetView->OnShouldFilterAsset.Execute(ItemAssetData))
			{
				return true;
			}
		}

		return false;
	}

	bool DoesItemPassFrontendFilter(const TSharedPtr<FAssetViewItem>& InItemToFilter)
	{
		// Folders are only subject to "empty" filtering
		if (InItemToFilter->IsFolder())
		{
			return bDisplayEmptyFolders || ContentBrowserData->IsFolderVisibleIfHidingEmpty(InItemToFilter->GetItem().GetVirtualPath());
		}

		// Run the item through the filters
		if (!AssetView->IsFrontendFilterActive() || AssetView->PassesCurrentFrontendFilter(InItemToFilter->GetItem()))
		{
			return true;
		}

		return false;
	}

private:
	SAssetView* AssetView = nullptr;
	UContentBrowserDataSubsystem* ContentBrowserData = nullptr;
	const bool bDisplayEmptyFolders = true;
};

SAssetView::~SAssetView()
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

	// Remove the listener for when view settings are changed
	UContentBrowserSettings::OnSettingChanged().RemoveAll(this);

	if ( FrontendFilters.IsValid() )
	{
		// Clear the frontend filter changed delegate
		FrontendFilters->OnChanged().RemoveAll( this );
	}

	// Release all rendering resources being held onto
	AssetThumbnailPool.Reset();
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SAssetView::Construct( const FArguments& InArgs )
{
	bIsWorking = false;
	TotalAmortizeTime = 0;
	AmortizeStartTime = 0;
	MaxSecondsPerFrame = 0.015;

	bFillEmptySpaceInTileView = InArgs._FillEmptySpaceInTileView;
	FillScale = 1.0f;

	ThumbnailHintFadeInSequence.JumpToStart();
	ThumbnailHintFadeInSequence.AddCurve(0, 0.5f, ECurveEaseFunction::Linear);

	UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
	ContentBrowserData->OnItemDataUpdated().AddSP(this, &SAssetView::HandleItemDataUpdated);
	ContentBrowserData->OnItemDataRefreshed().AddSP(this, &SAssetView::RequestSlowFullListRefresh);
	ContentBrowserData->OnItemDataDiscoveryComplete().AddSP(this, &SAssetView::HandleItemDataDiscoveryComplete);

	FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();
	CollectionManagerModule.Get().OnAssetsAdded().AddSP( this, &SAssetView::OnAssetsAddedToCollection );
	CollectionManagerModule.Get().OnAssetsRemoved().AddSP( this, &SAssetView::OnAssetsRemovedFromCollection );
	CollectionManagerModule.Get().OnCollectionRenamed().AddSP( this, &SAssetView::OnCollectionRenamed );
	CollectionManagerModule.Get().OnCollectionUpdated().AddSP( this, &SAssetView::OnCollectionUpdated );

	// Listen for when view settings are changed
	UContentBrowserSettings::OnSettingChanged().AddSP(this, &SAssetView::HandleSettingChanged);

	// Get desktop metrics
	FDisplayMetrics DisplayMetrics;
	FSlateApplication::Get().GetCachedDisplayMetrics( DisplayMetrics );

	const FVector2D DisplaySize(
		DisplayMetrics.PrimaryDisplayWorkAreaRect.Right - DisplayMetrics.PrimaryDisplayWorkAreaRect.Left,
		DisplayMetrics.PrimaryDisplayWorkAreaRect.Bottom - DisplayMetrics.PrimaryDisplayWorkAreaRect.Top );

	const float ThumbnailScaleRangeScalar = ( DisplaySize.Y / 1080 );

	// Create a thumbnail pool for rendering thumbnails	
	AssetThumbnailPool = MakeShareable( new FAssetThumbnailPool(1024, InArgs._AreRealTimeThumbnailsAllowed) );
	NumOffscreenThumbnails = 64;
	ListViewThumbnailResolution = 128;
	ListViewThumbnailSize = 64;
	ListViewThumbnailPadding = 4;
	TileViewThumbnailResolution = 256;
	TileViewThumbnailSize = 128;
	TileViewThumbnailPadding = 5;

	TileViewNameHeight = 36;
	ThumbnailScaleSliderValue = InArgs._ThumbnailScale; 

	if ( !ThumbnailScaleSliderValue.IsBound() )
	{
		ThumbnailScaleSliderValue = FMath::Clamp<float>(ThumbnailScaleSliderValue.Get(), 0.0f, 1.0f);
	}

	MinThumbnailScale = 0.2f * ThumbnailScaleRangeScalar;
	MaxThumbnailScale = 2.0f * ThumbnailScaleRangeScalar;

	bCanShowClasses = InArgs._CanShowClasses;

	bCanShowFolders = InArgs._CanShowFolders;

	bFilterRecursivelyWithBackendFilter = InArgs._FilterRecursivelyWithBackendFilter;
		
	bCanShowRealTimeThumbnails = InArgs._CanShowRealTimeThumbnails;

	bCanShowDevelopersFolder = InArgs._CanShowDevelopersFolder;

	bCanShowFavorites = InArgs._CanShowFavorites;
	bCanDockCollections = InArgs._CanDockCollections;
	bPreloadAssetsForContextMenu = InArgs._PreloadAssetsForContextMenu;

	SelectionMode = InArgs._SelectionMode;

	bShowPathInColumnView = InArgs._ShowPathInColumnView;
	bShowTypeInColumnView = InArgs._ShowTypeInColumnView;
	bSortByPathInColumnView = bShowPathInColumnView & InArgs._SortByPathInColumnView;
	bForceShowEngineContent = InArgs._ForceShowEngineContent;
	bForceShowPluginContent = InArgs._ForceShowPluginContent;

	bPendingUpdateThumbnails = false;
	bShouldNotifyNextAssetSync = true;
	CurrentThumbnailSize = TileViewThumbnailSize;

	SourcesData = InArgs._InitialSourcesData;
	BackendFilter = InArgs._InitialBackendFilter;

	FrontendFilters = InArgs._FrontendFilters;
	if ( FrontendFilters.IsValid() )
	{
		FrontendFilters->OnChanged().AddSP( this, &SAssetView::OnFrontendFiltersChanged );
	}

	OnShouldFilterAsset = InArgs._OnShouldFilterAsset;
	OnNewItemRequested = InArgs._OnNewItemRequested;
	OnItemSelectionChanged = InArgs._OnItemSelectionChanged;
	OnItemsActivated = InArgs._OnItemsActivated;
	OnGetItemContextMenu = InArgs._OnGetItemContextMenu;
	OnItemRenameCommitted = InArgs._OnItemRenameCommitted;
	OnAssetTagWantsToBeDisplayed = InArgs._OnAssetTagWantsToBeDisplayed;
	OnIsAssetValidForCustomToolTip = InArgs._OnIsAssetValidForCustomToolTip;
	OnGetCustomAssetToolTip = InArgs._OnGetCustomAssetToolTip;
	OnVisualizeAssetToolTip = InArgs._OnVisualizeAssetToolTip;
	OnAssetToolTipClosing = InArgs._OnAssetToolTipClosing;
	OnGetCustomSourceAssets = InArgs._OnGetCustomSourceAssets;
	HighlightedText = InArgs._HighlightedText;
	ThumbnailLabel = InArgs._ThumbnailLabel;
	AllowThumbnailHintLabel = InArgs._AllowThumbnailHintLabel;
	InitialCategoryFilter = InArgs._InitialCategoryFilter;
	AssetShowWarningText = InArgs._AssetShowWarningText;
	bAllowDragging = InArgs._AllowDragging;
	bAllowFocusOnSync = InArgs._AllowFocusOnSync;
	HiddenColumnNames = DefaultHiddenColumnNames = InArgs._HiddenColumnNames;
	CustomColumns = InArgs._CustomColumns;
	OnSearchOptionsChanged = InArgs._OnSearchOptionsChanged;
	bShowPathViewFilters = InArgs._bShowPathViewFilters;
	OnExtendAssetViewOptionsMenuContext = InArgs._OnExtendAssetViewOptionsMenuContext;

	if ( InArgs._InitialViewType >= 0 && InArgs._InitialViewType < EAssetViewType::MAX )
	{
		CurrentViewType = InArgs._InitialViewType;
	}
	else
	{
		CurrentViewType = EAssetViewType::Tile;
	}

	bPendingSortFilteredItems = false;
	bQuickFrontendListRefreshRequested = false;
	bSlowFullListRefreshRequested = false;
	LastSortTime = 0;
	SortDelaySeconds = 8;

	bBulkSelecting = false;
	bAllowThumbnailEditMode = InArgs._AllowThumbnailEditMode;
	bThumbnailEditMode = false;
	bUserSearching = false;
	bPendingFocusOnSync = false;
	bWereItemsRecursivelyFiltered = false;

	NumVisibleColumns = 0;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetClassBlacklist = AssetToolsModule.Get().GetAssetClassBlacklist();
	FolderBlacklist = AssetToolsModule.Get().GetFolderBlacklist();

	FEditorWidgetsModule& EditorWidgetsModule = FModuleManager::LoadModuleChecked<FEditorWidgetsModule>("EditorWidgets");
	TSharedRef<SWidget> AssetDiscoveryIndicator = EditorWidgetsModule.CreateAssetDiscoveryIndicator(EAssetDiscoveryIndicatorScaleMode::Scale_Vertical);

	TSharedRef<SVerticalBox> VerticalBox = SNew(SVerticalBox);

	ChildSlot
	[
		VerticalBox
	];

	// Assets area
	VerticalBox->AddSlot()
	.FillHeight(1.f)
	[
		SNew( SVerticalBox ) 

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew( SBox )
			.Visibility_Lambda([this] { return bIsWorking ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
			.HeightOverride( 2 )
			[
				SNew( SProgressBar )
				.Percent( this, &SAssetView::GetIsWorkingProgressBarState )
				.Style( FEditorStyle::Get(), "WorkingBar" )
				.BorderPadding( FVector2D(0,0) )
			]
		]
		
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				// Container for the view types
				SAssignNew(ViewContainer, SBorder)
				.Padding(0)
				.BorderImage(FEditorStyle::GetBrush("NoBorder"))
			]

			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.Padding(FMargin(0, 14, 0, 0))
			[
				// A warning to display when there are no assets to show
				SNew( STextBlock )
				.Justification( ETextJustify::Center )
				.Text( this, &SAssetView::GetAssetShowWarningText )
				.Visibility( this, &SAssetView::IsAssetShowWarningTextVisible )
				.AutoWrapText( true )
			]

			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Bottom)
			.Padding(FMargin(24, 0, 24, 0))
			[
				// Asset discovery indicator
				AssetDiscoveryIndicator
			]

			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Bottom)
			.Padding(FMargin(8, 0))
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ErrorReporting.EmptyBox"))
				.BorderBackgroundColor(this, &SAssetView::GetQuickJumpColor)
				.Visibility(this, &SAssetView::IsQuickJumpVisible)
				[
					SNew(STextBlock)
					.Text(this, &SAssetView::GetQuickJumpTerm)
				]
			]
		]
	];

	// Thumbnail edit mode banner
	VerticalBox->AddSlot()
	.AutoHeight()
	.Padding(0, 4)
	[
		SNew(SBorder)
		.Visibility( this, &SAssetView::GetEditModeLabelVisibility )
		.BorderImage( FEditorStyle::GetBrush("ContentBrowser.EditModeLabelBorder") )
		.Content()
		[
			SNew( SHorizontalBox )

			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			.FillWidth(1.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ThumbnailEditModeLabel", "Editing Thumbnails. Drag a thumbnail to rotate it if there is a 3D environment."))
				.TextStyle( FEditorStyle::Get(), "ContentBrowser.EditModeLabelFont" )
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text( LOCTEXT("EndThumbnailEditModeButton", "Done Editing") )
				.OnClicked( this, &SAssetView::EndThumbnailEditModeClicked )
			]
		]
	];

	if (InArgs._ShowBottomToolbar)
	{
		// Bottom panel
		VerticalBox->AddSlot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			// Asset count
			+SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(8, 0)
			[
				SNew(STextBlock)
				.Text(this, &SAssetView::GetAssetCountText)
			]

			// View mode combo button
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew( ViewOptionsComboButton, SComboButton )
				.ContentPadding(0)
				.ForegroundColor( this, &SAssetView::GetViewButtonForegroundColor )
				.ButtonStyle( FEditorStyle::Get(), "ToggleButton" ) // Use the tool bar item style for this button
				.OnGetMenuContent( this, &SAssetView::GetViewButtonContent )
				.ButtonContent()
				[
					SNew(SHorizontalBox)
 
					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SImage).Image( FEditorStyle::GetBrush("GenericViewButton") )
					]
 
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2, 0, 0, 0)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text( LOCTEXT("ViewButton", "View Options") )
					]
				]
			]
		];
	}

	CreateCurrentView();

	if( InArgs._InitialAssetSelection.IsValid() )
	{
		// sync to the initial item without notifying of selection
		bShouldNotifyNextAssetSync = false;
		SyncToLegacy( MakeArrayView(&InArgs._InitialAssetSelection, 1), TArrayView<const FString>() );
	}

	// If currently looking at column, and you could choose to sort by path in column first and then name
	// Generalizing this is a bit difficult because the column ID is not accessible or is not known
	// Currently I assume this won't work, if this view mode is not column. Otherwise, I don't think sorting by path
	// is a good idea. 
	if (CurrentViewType == EAssetViewType::Column && bSortByPathInColumnView)
	{
		SortManager.SetSortColumnId(EColumnSortPriority::Primary, SortManager.PathColumnId);
		SortManager.SetSortColumnId(EColumnSortPriority::Secondary, SortManager.NameColumnId);
		SortManager.SetSortMode(EColumnSortPriority::Primary, EColumnSortMode::Ascending);
		SortManager.SetSortMode(EColumnSortPriority::Secondary, EColumnSortMode::Ascending);
		SortList();
	}
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

TOptional< float > SAssetView::GetIsWorkingProgressBarState() const
{
	if (bIsWorking)
	{
		const int32 TotalAssetCount = FilteredAssetItems.Num() + ItemsPendingFrontendFilter.Num();
		if (TotalAssetCount > 0)
		{
			return static_cast<float>(FilteredAssetItems.Num()) / static_cast<float>(TotalAssetCount);
		}
	}
	return 0.0f;
}

void SAssetView::SetSourcesData(const FSourcesData& InSourcesData)
{
	// Update the path and collection lists
	SourcesData = InSourcesData;
	RequestSlowFullListRefresh();
	ClearSelection();
}

const FSourcesData& SAssetView::GetSourcesData() const
{
	return SourcesData;
}

bool SAssetView::IsAssetPathSelected() const
{
	int32 NumAssetPaths, NumClassPaths;
	ContentBrowserUtils::CountPathTypes(SourcesData.VirtualPaths, NumAssetPaths, NumClassPaths);

	// Check that only asset paths are selected
	return NumAssetPaths > 0 && NumClassPaths == 0;
}

void SAssetView::SetBackendFilter(const FARFilter& InBackendFilter)
{
	// Update the path and collection lists
	BackendFilter = InBackendFilter;
	RequestSlowFullListRefresh();
}

void SAssetView::AppendBackendFilter(FARFilter& FilterToAppendTo) const
{
	FilterToAppendTo.Append(BackendFilter);
}

void SAssetView::NewFolderItemRequested(const FContentBrowserItemTemporaryContext& NewItemContext)
{
	// Don't allow asset creation while renaming
	if (IsRenamingAsset())
	{
		return;
	}

	// we should only be creating one deferred folder per tick
	check(!DeferredItemToCreate.IsValid());

	// Folder creation requires focus to give object a name, otherwise object will not be created
	TSharedPtr<SWindow> OwnerWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (OwnerWindow.IsValid() && !OwnerWindow->HasAnyUserFocusOrFocusedDescendants())
	{
		FSlateApplication::Get().SetUserFocus(FSlateApplication::Get().GetUserIndexForKeyboard(), AsShared(), EFocusCause::SetDirectly);
	}

	// Notify that we're about to start creating this item, as we may need to do things like ensure the parent folder is visible
	OnNewItemRequested.ExecuteIfBound(NewItemContext.GetItem());

	// Defer folder creation until next tick, so we get a chance to refresh the view
	DeferredItemToCreate = MakeUnique<FCreateDeferredItemData>();
	DeferredItemToCreate->ItemContext = NewItemContext;

	UE_LOG(LogContentBrowser, Log, TEXT("Deferred new asset folder creation: %s"), *NewItemContext.GetItem().GetItemName().ToString());
}

void SAssetView::NewFileItemRequested(const FContentBrowserItemDataTemporaryContext& NewItemContext)
{
	// Don't allow asset creation while renaming
	if (IsRenamingAsset())
	{
		return;
	}

	// We should only be creating one deferred file at a time
	check(!DeferredItemToCreate.IsValid());

	// File creation requires focus to give item a name, otherwise the item will not be created
	TSharedPtr<SWindow> OwnerWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (OwnerWindow.IsValid() && !OwnerWindow->HasAnyUserFocusOrFocusedDescendants())
	{
		FSlateApplication::Get().SetUserFocus(FSlateApplication::Get().GetUserIndexForKeyboard(), AsShared(), EFocusCause::SetDirectly);
	}

	// Notify that we're about to start creating this item, as we may need to do things like ensure the parent folder is visible
	if (OnNewItemRequested.IsBound())
	{
		OnNewItemRequested.Execute(FContentBrowserItem(NewItemContext.GetItemData()));
	}

	// Defer file creation until next tick, so we get a chance to refresh the view
	DeferredItemToCreate = MakeUnique<FCreateDeferredItemData>();
	DeferredItemToCreate->ItemContext.AppendContext(CopyTemp(NewItemContext));

	UE_LOG(LogContentBrowser, Log, TEXT("Deferred new asset file creation: %s"), *NewItemContext.GetItemData().GetItemName().ToString());
}

void SAssetView::BeginCreateDeferredItem()
{
	if (DeferredItemToCreate.IsValid() && !DeferredItemToCreate->bWasAddedToView)
	{
		TSharedPtr<FAssetViewItem> NewItem = MakeShared<FAssetViewItem>(DeferredItemToCreate->ItemContext.GetItem());
		NewItem->RenameWhenScrolledIntoView();
		DeferredItemToCreate->bWasAddedToView = true;

		FilteredAssetItems.Insert(NewItem, 0);
		SortManager.SortList(FilteredAssetItems, MajorityAssetType, CustomColumns);

		SetSelection(NewItem);
		RequestScrollIntoView(NewItem);

		UE_LOG(LogContentBrowser, Log, TEXT("Creating deferred item: %s"), *NewItem->GetItem().GetItemName().ToString());
	}
}

FContentBrowserItem SAssetView::EndCreateDeferredItem(const TSharedPtr<FAssetViewItem>& InItem, const FString& InName, const bool bFinalize, FText& OutErrorText)
{
	FContentBrowserItem FinalizedItem;

	if (DeferredItemToCreate.IsValid() && DeferredItemToCreate->bWasAddedToView)
	{
		checkf(FContentBrowserItemKey(InItem->GetItem()) == FContentBrowserItemKey(DeferredItemToCreate->ItemContext.GetItem()), TEXT("DeferredItemToCreate was still set when attempting to rename a different item!"));

		// Remove the temporary item before we do any work to ensure the new item creation is not prevented
		FilteredAssetItems.Remove(InItem);
		RefreshList();

		// If not finalizing then we just discard the temporary
		if (bFinalize)
		{
			if (DeferredItemToCreate->ItemContext.ValidateItem(InName, &OutErrorText))
			{
				FinalizedItem = DeferredItemToCreate->ItemContext.FinalizeItem(InName, &OutErrorText);
			}
		}
	}

	// Always reset the deferred item to avoid having it dangle, which can lead to potential crashes.
	DeferredItemToCreate.Reset();

	UE_LOG(LogContentBrowser, Log, TEXT("End creating deferred item %s"), *InItem->GetItem().GetItemName().ToString());

	return FinalizedItem;
}

void SAssetView::CreateNewAsset(const FString& DefaultAssetName, const FString& PackagePath, UClass* AssetClass, UFactory* Factory)
{
	ContentBrowserDataLegacyBridge::OnCreateNewAsset().ExecuteIfBound(*DefaultAssetName, *PackagePath, AssetClass, Factory, UContentBrowserDataMenuContext_AddNewMenu::FOnBeginItemCreation::CreateSP(this, &SAssetView::NewFileItemRequested));
}

void SAssetView::RenameItem(const FContentBrowserItem& ItemToRename)
{
	if (const TSharedPtr<FAssetViewItem> Item = AvailableBackendItems.FindRef(FContentBrowserItemKey(ItemToRename)))
	{
		Item->RenameWhenScrolledIntoView();
		
		SetSelection(Item);
		RequestScrollIntoView(Item);
	}
}

void SAssetView::SyncToItems(TArrayView<const FContentBrowserItem> ItemsToSync, const bool bFocusOnSync)
{
	PendingSyncItems.Reset();

	for (const FContentBrowserItem& Item : ItemsToSync)
	{
		PendingSyncItems.SelectedVirtualPaths.Add(Item.GetVirtualPath());
	}

	bPendingFocusOnSync = bFocusOnSync;
}

void SAssetView::SyncToVirtualPaths(TArrayView<const FName> VirtualPathsToSync, const bool bFocusOnSync)
{
	PendingSyncItems.Reset();
	for (const FName& VirtualPathToSync : VirtualPathsToSync)
	{
		PendingSyncItems.SelectedVirtualPaths.Add(VirtualPathToSync);
	}

	bPendingFocusOnSync = bFocusOnSync;
}

void SAssetView::SyncToLegacy(TArrayView<const FAssetData> AssetDataList, TArrayView<const FString> FolderList, const bool bFocusOnSync)
{
	PendingSyncItems.Reset();
	ContentBrowserUtils::ConvertLegacySelectionToVirtualPaths(AssetDataList, FolderList, /*UseFolderPaths*/false, PendingSyncItems.SelectedVirtualPaths);

	bPendingFocusOnSync = bFocusOnSync;
}

void SAssetView::SyncToSelection( const bool bFocusOnSync )
{
	PendingSyncItems.Reset();

	TArray<TSharedPtr<FAssetViewItem>> SelectedItems = GetSelectedViewItems();
	for (const TSharedPtr<FAssetViewItem>& Item : SelectedItems)
	{
		if (Item.IsValid())
		{
			PendingSyncItems.SelectedVirtualPaths.Add(Item->GetItem().GetVirtualPath());
		}
	}

	bPendingFocusOnSync = bFocusOnSync;
}

void SAssetView::ApplyHistoryData( const FHistoryData& History )
{
	SetSourcesData(History.SourcesData);
	PendingSyncItems = History.SelectionData;
	bPendingFocusOnSync = true;
}

TArray<TSharedPtr<FAssetViewItem>> SAssetView::GetSelectedViewItems() const
{
	switch ( GetCurrentViewType() )
	{
		case EAssetViewType::List: return ListView->GetSelectedItems();
		case EAssetViewType::Tile: return TileView->GetSelectedItems();
		case EAssetViewType::Column: return ColumnView->GetSelectedItems();
		default:
		ensure(0); // Unknown list type
		return TArray<TSharedPtr<FAssetViewItem>>();
	}
}

TArray<FContentBrowserItem> SAssetView::GetSelectedItems() const
{
	TArray<TSharedPtr<FAssetViewItem>> SelectedViewItems = GetSelectedViewItems();

	TArray<FContentBrowserItem> SelectedItems;
	for (const TSharedPtr<FAssetViewItem>& SelectedViewItem : SelectedViewItems)
	{
		if (!SelectedViewItem->IsTemporary())
		{
			SelectedItems.Emplace(SelectedViewItem->GetItem());
		}
	}
	return SelectedItems;
}

TArray<FContentBrowserItem> SAssetView::GetSelectedFolderItems() const
{
	TArray<TSharedPtr<FAssetViewItem>> SelectedViewItems = GetSelectedViewItems();

	TArray<FContentBrowserItem> SelectedFolders;
	for (const TSharedPtr<FAssetViewItem>& SelectedViewItem : SelectedViewItems)
	{
		if (SelectedViewItem->IsFolder() && !SelectedViewItem->IsTemporary())
		{
			SelectedFolders.Emplace(SelectedViewItem->GetItem());
		}
	}
	return SelectedFolders;
}

TArray<FContentBrowserItem> SAssetView::GetSelectedFileItems() const
{
	TArray<TSharedPtr<FAssetViewItem>> SelectedViewItems = GetSelectedViewItems();

	TArray<FContentBrowserItem> SelectedFiles;
	for (const TSharedPtr<FAssetViewItem>& SelectedViewItem : SelectedViewItems)
	{
		if (SelectedViewItem->IsFile() && !SelectedViewItem->IsTemporary())
		{
			SelectedFiles.Emplace(SelectedViewItem->GetItem());
		}
	}
	return SelectedFiles;
}

TArray<FAssetData> SAssetView::GetSelectedAssets() const
{
	TArray<TSharedPtr<FAssetViewItem>> SelectedViewItems = GetSelectedViewItems();

	// TODO: Abstract away?
	TArray<FAssetData> SelectedAssets;
	for (const TSharedPtr<FAssetViewItem>& SelectedViewItem : SelectedViewItems)
	{
		// Only report non-temporary & non-folder items
		FAssetData ItemAssetData;
		if (!SelectedViewItem->IsTemporary() && SelectedViewItem->IsFile() && SelectedViewItem->GetItem().Legacy_TryGetAssetData(ItemAssetData))
		{
			SelectedAssets.Add(MoveTemp(ItemAssetData));
		}
	}
	return SelectedAssets;
}

TArray<FString> SAssetView::GetSelectedFolders() const
{
	TArray<TSharedPtr<FAssetViewItem>> SelectedViewItems = GetSelectedViewItems();

	// TODO: Abstract away?
	TArray<FString> SelectedFolders;
	for (const TSharedPtr<FAssetViewItem>& SelectedViewItem : SelectedViewItems)
	{
		if (SelectedViewItem->IsFolder())
		{
			SelectedFolders.Emplace(SelectedViewItem->GetItem().GetVirtualPath().ToString());
		}
	}
	return SelectedFolders;
}

void SAssetView::RequestSlowFullListRefresh()
{
	bSlowFullListRefreshRequested = true;
}

void SAssetView::RequestQuickFrontendListRefresh()
{
	bQuickFrontendListRefreshRequested = true;
}

FString SAssetView::GetThumbnailScaleSettingPath(const FString& SettingsString) const
{
	return SettingsString + TEXT(".ThumbnailSizeScale");
}

FString SAssetView::GetCurrentViewTypeSettingPath(const FString& SettingsString) const
{
	return SettingsString + TEXT(".CurrentViewType");
}

void SAssetView::SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const
{
	GConfig->SetFloat(*IniSection, *GetThumbnailScaleSettingPath(SettingsString), ThumbnailScaleSliderValue.Get(), IniFilename);
	GConfig->SetInt(*IniSection, *GetCurrentViewTypeSettingPath(SettingsString), CurrentViewType, IniFilename);
	
	GConfig->SetArray(*IniSection, *(SettingsString + TEXT(".HiddenColumns")), HiddenColumnNames, IniFilename);
}

void SAssetView::LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString)
{
	float Scale = 0.f;
	if ( GConfig->GetFloat(*IniSection, *GetThumbnailScaleSettingPath(SettingsString), Scale, IniFilename) )
	{
		// Clamp value to normal range and update state
		Scale = FMath::Clamp<float>(Scale, 0.f, 1.f);
		SetThumbnailScale(Scale);
	}

	int32 ViewType = EAssetViewType::Tile;
	if ( GConfig->GetInt(*IniSection, *GetCurrentViewTypeSettingPath(SettingsString), ViewType, IniFilename) )
	{
		// Clamp value to normal range and update state
		if ( ViewType < 0 || ViewType >= EAssetViewType::MAX)
		{
			ViewType = EAssetViewType::Tile;
		}
		SetCurrentViewType( (EAssetViewType::Type)ViewType );
	}
	
	TArray<FString> LoadedHiddenColumnNames;
	GConfig->GetArray(*IniSection, *(SettingsString + TEXT(".HiddenColumns")), LoadedHiddenColumnNames, IniFilename);
	if (LoadedHiddenColumnNames.Num() > 0)
	{
		HiddenColumnNames = LoadedHiddenColumnNames;
	}
}

// Adjusts the selected asset by the selection delta, which should be +1 or -1)
void SAssetView::AdjustActiveSelection(int32 SelectionDelta)
{
	// Find the index of the first selected item
	TArray<TSharedPtr<FAssetViewItem>> SelectionSet = GetSelectedViewItems();
	
	int32 SelectedSuggestion = INDEX_NONE;

	if (SelectionSet.Num() > 0)
	{
		if (!FilteredAssetItems.Find(SelectionSet[0], /*out*/ SelectedSuggestion))
		{
			// Should never happen
			ensureMsgf(false, TEXT("SAssetView has a selected item that wasn't in the filtered list"));
			return;
		}
	}
	else
	{
		SelectedSuggestion = 0;
		SelectionDelta = 0;
	}

	if (FilteredAssetItems.Num() > 0)
	{
		// Move up or down one, wrapping around
		SelectedSuggestion = (SelectedSuggestion + SelectionDelta + FilteredAssetItems.Num()) % FilteredAssetItems.Num();

		// Pick the new asset
		const TSharedPtr<FAssetViewItem>& NewSelection = FilteredAssetItems[SelectedSuggestion];

		RequestScrollIntoView(NewSelection);
		SetSelection(NewSelection);
	}
	else
	{
		ClearSelection();
	}
}

void SAssetView::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	CalculateFillScale( AllottedGeometry );

	CurrentTime = InCurrentTime;

	if (FSlateApplication::Get().GetActiveModalWindow().IsValid())
	{
		// If we're in a model window then we need to tick the thumbnail pool in order for thumbnails to render correctly.
		AssetThumbnailPool->Tick(InDeltaTime);
	}

	CalculateThumbnailHintColorAndOpacity();

	if (bPendingUpdateThumbnails)
	{
		UpdateThumbnails();
		bPendingUpdateThumbnails = false;
	}

	if (bSlowFullListRefreshRequested)
	{
		RefreshSourceItems();
		bSlowFullListRefreshRequested = false;
		bQuickFrontendListRefreshRequested = true;
	}

	bool bForceViewUpdate = false;
	if (bQuickFrontendListRefreshRequested)
	{
		ResetQuickJump();

		RefreshFilteredItems();

		bQuickFrontendListRefreshRequested = false;
		bForceViewUpdate = true; // If HasItemsPendingFilter is empty we still need to update the view
	}

	if (HasItemsPendingFilter() || bForceViewUpdate)
	{
		bForceViewUpdate = false;

		const double TickStartTime = FPlatformTime::Seconds();
		const bool bWasWorking = bIsWorking;

		// Mark the first amortize time
		if (AmortizeStartTime == 0)
		{
			AmortizeStartTime = FPlatformTime::Seconds();
			bIsWorking = true;
		}

		ProcessItemsPendingFilter(bUserSearching ? -1.0 : TickStartTime);

		if (HasItemsPendingFilter())
		{
			if (bPendingSortFilteredItems && InCurrentTime > LastSortTime + SortDelaySeconds)
			{
				// Don't sync to selection if we are just going to do it below
				SortList(!PendingSyncItems.Num());
			}

			// Need to finish processing queried items before rest of function is safe
			return;
		}
		else
		{
			TotalAmortizeTime += FPlatformTime::Seconds() - AmortizeStartTime;
			AmortizeStartTime = 0;
			bIsWorking = false;

			// Update the columns in the column view now that we know the majority type
			if (CurrentViewType == EAssetViewType::Column)
			{
				int32 HighestCount = 0;
				FName HighestType;
				for (auto TypeIt = FilteredAssetItemTypeCounts.CreateConstIterator(); TypeIt; ++TypeIt)
				{
					if (TypeIt.Value() > HighestCount)
					{
						HighestType = TypeIt.Key();
						HighestCount = TypeIt.Value();
					}
				}

				SetMajorityAssetType(HighestType);
			}

			if (bPendingSortFilteredItems && (bWasWorking || (InCurrentTime > LastSortTime + SortDelaySeconds)))
			{
				// Don't sync to selection if we are just going to do it below
				SortList(!PendingSyncItems.Num());
			}
		}
	}

	if ( PendingSyncItems.Num() > 0 )
	{
		if (bPendingSortFilteredItems)
		{
			// Don't sync to selection because we are just going to do it below
			SortList(/*bSyncToSelection=*/false);
		}
		
		bBulkSelecting = true;
		ClearSelection();
		bool bFoundScrollIntoViewTarget = false;

		for ( auto ItemIt = FilteredAssetItems.CreateConstIterator(); ItemIt; ++ItemIt )
		{
			const auto& Item = *ItemIt;
			if(Item.IsValid())
			{
				if (PendingSyncItems.SelectedVirtualPaths.Contains(Item->GetItem().GetVirtualPath()))
				{
					SetItemSelection(Item, true, ESelectInfo::OnNavigation);
					
					// Scroll the first item in the list that can be shown into view
					if ( !bFoundScrollIntoViewTarget )
					{
						RequestScrollIntoView(Item);
						bFoundScrollIntoViewTarget = true;
					}
				}
			}
		}
	
		bBulkSelecting = false;

		if (bShouldNotifyNextAssetSync && !bUserSearching)
		{
			AssetSelectionChanged(TSharedPtr<FAssetViewItem>(), ESelectInfo::Direct);
		}

		// Default to always notifying
		bShouldNotifyNextAssetSync = true;

		PendingSyncItems.Reset();

		if (bAllowFocusOnSync && bPendingFocusOnSync)
		{
			FocusList();
		}
	}

	if ( IsHovered() )
	{
		// This prevents us from sorting the view immediately after the cursor leaves it
		LastSortTime = CurrentTime;
	}
	else if ( bPendingSortFilteredItems && InCurrentTime > LastSortTime + SortDelaySeconds )
	{
		SortList();
	}

	// create any pending items now
	BeginCreateDeferredItem();

	// Do quick-jump last as the Tick function might have canceled it
	if(QuickJumpData.bHasChangedSinceLastTick)
	{
		QuickJumpData.bHasChangedSinceLastTick = false;

		const bool bWasJumping = QuickJumpData.bIsJumping;
		QuickJumpData.bIsJumping = true;

		QuickJumpData.LastJumpTime = InCurrentTime;
		QuickJumpData.bHasValidMatch = PerformQuickJump(bWasJumping);
	}
	else if(QuickJumpData.bIsJumping && InCurrentTime > QuickJumpData.LastJumpTime + JumpDelaySeconds)
	{
		ResetQuickJump();
	}

	TSharedPtr<FAssetViewItem> AssetAwaitingRename = AwaitingRename.Pin();
	if (AssetAwaitingRename.IsValid())
	{
		TSharedPtr<SWindow> OwnerWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
		if (!OwnerWindow.IsValid())
		{
			AssetAwaitingRename->ClearRenameWhenScrolledIntoView();
			AwaitingRename = nullptr;
		}
		else if (OwnerWindow->HasAnyUserFocusOrFocusedDescendants())
		{
			AssetAwaitingRename->OnRenameRequested().ExecuteIfBound();
			AssetAwaitingRename->ClearRenameWhenScrolledIntoView();
			AwaitingRename = nullptr;
		}
	}
}

void SAssetView::CalculateFillScale( const FGeometry& AllottedGeometry )
{
	if ( bFillEmptySpaceInTileView && CurrentViewType == EAssetViewType::Tile )
	{
		float ItemWidth = GetTileViewItemBaseWidth();

		// Scrollbars are 16, but we add 1 to deal with half pixels.
		const float ScrollbarWidth = 16 + 1;
		float TotalWidth = AllottedGeometry.GetLocalSize().X - ( ScrollbarWidth / AllottedGeometry.Scale );
		float Coverage = TotalWidth / ItemWidth;
		int32 Items = (int)( TotalWidth / ItemWidth );

		// If there isn't enough room to support even a single item, don't apply a fill scale.
		if ( Items > 0 )
		{
			float GapSpace = ItemWidth * ( Coverage - Items );
			float ExpandAmount = GapSpace / (float)Items;
			FillScale = ( ItemWidth + ExpandAmount ) / ItemWidth;
			FillScale = FMath::Max( 1.0f, FillScale );
		}
		else
		{
			FillScale = 1.0f;
		}
	}
	else
	{
		FillScale = 1.0f;
	}
}

void SAssetView::CalculateThumbnailHintColorAndOpacity()
{
	if ( HighlightedText.Get().IsEmpty() )
	{
		if ( ThumbnailHintFadeInSequence.IsPlaying() )
		{
			if ( ThumbnailHintFadeInSequence.IsForward() )
			{
				ThumbnailHintFadeInSequence.Reverse();
			}
		}
		else if ( ThumbnailHintFadeInSequence.IsAtEnd() ) 
		{
			ThumbnailHintFadeInSequence.PlayReverse(this->AsShared());
		}
	}
	else 
	{
		if ( ThumbnailHintFadeInSequence.IsPlaying() )
		{
			if ( ThumbnailHintFadeInSequence.IsInReverse() )
			{
				ThumbnailHintFadeInSequence.Reverse();
			}
		}
		else if ( ThumbnailHintFadeInSequence.IsAtStart() ) 
		{
			ThumbnailHintFadeInSequence.Play(this->AsShared());
		}
	}

	const float Opacity = ThumbnailHintFadeInSequence.GetLerp();
	ThumbnailHintColorAndOpacity = FLinearColor( 1.0, 1.0, 1.0, Opacity );
}

bool SAssetView::HasItemsPendingFilter() const
{
	return (ItemsPendingPriorityFilter.Num() + ItemsPendingFrontendFilter.Num()) > 0;
}

void SAssetView::ProcessItemsPendingFilter(const double TickStartTime)
{
	const double ProcessItemsPendingFilterStartTime = FPlatformTime::Seconds();

	FAssetViewFrontendFilterHelper FrontendFilterHelper(this);

	auto UpdateFilteredAssetItemTypeCounts = [this](const TSharedPtr<FAssetViewItem>& InItem)
	{
		if (CurrentViewType == EAssetViewType::Column)
		{
			const FContentBrowserItemDataAttributeValue TypeNameValue = InItem->GetItem().GetItemAttribute(ContentBrowserItemAttributes::ItemTypeName);
			if (TypeNameValue.IsValid())
			{
				FilteredAssetItemTypeCounts.FindOrAdd(TypeNameValue.GetValue<FName>())++;
			}
		}
	};

	const bool bRunQueryFilter = OnShouldFilterAsset.IsBound();
	const bool bFlushAllPendingItems = TickStartTime < 0;

	bool bRefreshList = false;
	bool bHasTimeRemaining = true;

	auto FilterItem = [this, bRunQueryFilter, &bRefreshList, &FrontendFilterHelper, &UpdateFilteredAssetItemTypeCounts](const TSharedPtr<FAssetViewItem>& ItemToFilter)
	{
		// Run the query filter if required
		if (bRunQueryFilter)
		{
			const bool bPassedBackendFilter = FrontendFilterHelper.DoesItemPassQueryFilter(ItemToFilter);
			if (!bPassedBackendFilter)
			{
				AvailableBackendItems.Remove(FContentBrowserItemKey(ItemToFilter->GetItem()));
				return;
			}
		}

		// Run the frontend filter
		{
			const bool bPassedFrontendFilter = FrontendFilterHelper.DoesItemPassFrontendFilter(ItemToFilter);
			if (bPassedFrontendFilter)
			{
				checkAssetList(!FilteredAssetItems.Contains(ItemToFilter));

				bRefreshList = true;
				FilteredAssetItems.Add(ItemToFilter);
				UpdateFilteredAssetItemTypeCounts(ItemToFilter);
			}
		}
	};

	// Run the prioritized set first
	// This data must be processed this frame, so skip the amortization time checks within the loop itself
	if (ItemsPendingPriorityFilter.Num() > 0)
	{
		for (const TSharedPtr<FAssetViewItem>& ItemToFilter : ItemsPendingPriorityFilter)
		{
			// Make sure this item isn't pending in another list
			{
				const uint32 ItemToFilterHash = GetTypeHash(ItemToFilter);
				ItemsPendingFrontendFilter.RemoveByHash(ItemToFilterHash, ItemToFilter);
			}

			// Apply any filters and update the view
			FilterItem(ItemToFilter);
		}
		ItemsPendingPriorityFilter.Reset();

		// Check to see if we have run out of time in this tick
		if (!bFlushAllPendingItems && (FPlatformTime::Seconds() - TickStartTime) > MaxSecondsPerFrame)
		{
			bHasTimeRemaining = false;
		}
	}

	// Filter as many items as possible until we run out of time
	if (bHasTimeRemaining && ItemsPendingFrontendFilter.Num() > 0)
	{
		for (auto ItemIter = ItemsPendingFrontendFilter.CreateIterator(); ItemIter; ++ItemIter)
		{
			const TSharedPtr<FAssetViewItem> ItemToFilter = *ItemIter;
			ItemIter.RemoveCurrent();

			// Apply any filters and update the view
			FilterItem(ItemToFilter);

			// Check to see if we have run out of time in this tick
			if (!bFlushAllPendingItems && (FPlatformTime::Seconds() - TickStartTime) > MaxSecondsPerFrame)
			{
				bHasTimeRemaining = false;
				break;
			}
		}
	}

	if (bRefreshList)
	{
		bPendingSortFilteredItems = true;
		RefreshList();
	}

	UE_LOG(LogContentBrowser, VeryVerbose, TEXT("AssetView - ProcessItemsPendingFilter completed in %0.4f seconds"), FPlatformTime::Seconds() - ProcessItemsPendingFilterStartTime);
}

void SAssetView::OnDragLeave( const FDragDropEvent& DragDropEvent )
{
	TSharedPtr< FAssetDragDropOp > AssetDragDropOp = DragDropEvent.GetOperationAs< FAssetDragDropOp >();
	if( AssetDragDropOp.IsValid() )
	{
		AssetDragDropOp->ResetToDefaultToolTip();
	}

	TSharedPtr<FDragDropOperation> DragDropOp = DragDropEvent.GetOperation();
	if (DragDropOp.IsValid())
	{
		// Do we have a custom handler for this drag event?
		FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>("ContentBrowser");
		const TArray<FAssetViewDragAndDropExtender>& AssetViewDragAndDropExtenders = ContentBrowserModule.GetAssetViewDragAndDropExtenders();
		for (const auto& AssetViewDragAndDropExtender : AssetViewDragAndDropExtenders)
		{
			if (AssetViewDragAndDropExtender.OnDragLeaveDelegate.IsBound() && AssetViewDragAndDropExtender.OnDragLeaveDelegate.Execute(FAssetViewDragAndDropExtender::FPayload(DragDropOp, SourcesData.VirtualPaths, SourcesData.Collections)))
			{
				return;
			}
		}
	}
}

FReply SAssetView::OnDragOver( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	TSharedPtr<FDragDropOperation> DragDropOp = DragDropEvent.GetOperation();
	if (DragDropOp.IsValid())
	{
		// Do we have a custom handler for this drag event?
		FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>("ContentBrowser");
		const TArray<FAssetViewDragAndDropExtender>& AssetViewDragAndDropExtenders = ContentBrowserModule.GetAssetViewDragAndDropExtenders();
		for (const auto& AssetViewDragAndDropExtender : AssetViewDragAndDropExtenders)
		{
			if (AssetViewDragAndDropExtender.OnDragOverDelegate.IsBound() && AssetViewDragAndDropExtender.OnDragOverDelegate.Execute(FAssetViewDragAndDropExtender::FPayload(DragDropOp, SourcesData.VirtualPaths, SourcesData.Collections)))
			{
				return FReply::Handled();
			}
		}
	}

	if (SourcesData.HasVirtualPaths())
	{
		UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();

		const FContentBrowserItem DropFolderItem = ContentBrowserData->GetItemAtPath(SourcesData.VirtualPaths[0], EContentBrowserItemTypeFilter::IncludeFolders);
		if (DropFolderItem.IsValid() && DragDropHandler::HandleDragOverItem(DropFolderItem, DragDropEvent))
		{
			return FReply::Handled();
		}
	}
	else if (HasSingleCollectionSource())
	{
		TArray<FName> NewCollectionItems;

		if (TSharedPtr<FContentBrowserDataDragDropOp> ContentDragDropOp = DragDropEvent.GetOperationAs<FContentBrowserDataDragDropOp>())
		{
			for (const FContentBrowserItem& DraggedItem : ContentDragDropOp->GetDraggedFiles())
			{
				FName CollectionItemId;
				if (DraggedItem.TryGetCollectionId(CollectionItemId))
				{
					NewCollectionItems.Add(CollectionItemId);
				}
			}
		}
		else
		{
			const TArray<FAssetData> AssetDatas = AssetUtil::ExtractAssetDataFromDrag(DragDropEvent);
			Algo::Transform(AssetDatas, NewCollectionItems, &FAssetData::ObjectPath);
		}

		if (NewCollectionItems.Num() > 0)
		{
			if (TSharedPtr<FAssetDragDropOp> AssetDragDropOp = DragDropEvent.GetOperationAs<FAssetDragDropOp>())
			{
				TArray< FName > ObjectPaths;
				FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();
				const FCollectionNameType& Collection = SourcesData.Collections[0];
				CollectionManagerModule.Get().GetObjectsInCollection(Collection.Name, Collection.Type, ObjectPaths);

				bool IsValidDrop = false;
				for (const FName& NewCollectionItem : NewCollectionItems)
				{
					if (!ObjectPaths.Contains(NewCollectionItem))
					{
						IsValidDrop = true;
						break;
					}
				}

				if (IsValidDrop)
				{
					AssetDragDropOp->SetToolTip(NSLOCTEXT("AssetView", "OnDragOverCollection", "Add to Collection"), FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.OK")));
				}
			}

			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

FReply SAssetView::OnDrop( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	TSharedPtr<FDragDropOperation> DragDropOp = DragDropEvent.GetOperation();
	if (DragDropOp.IsValid())
	{
		// Do we have a custom handler for this drag event?
		FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>("ContentBrowser");
		const TArray<FAssetViewDragAndDropExtender>& AssetViewDragAndDropExtenders = ContentBrowserModule.GetAssetViewDragAndDropExtenders();
		for (const auto& AssetViewDragAndDropExtender : AssetViewDragAndDropExtenders)
		{
			if (AssetViewDragAndDropExtender.OnDropDelegate.IsBound() && AssetViewDragAndDropExtender.OnDropDelegate.Execute(FAssetViewDragAndDropExtender::FPayload(DragDropOp, SourcesData.VirtualPaths, SourcesData.Collections)))
			{
				return FReply::Handled();
			}
		}
	}

	if (SourcesData.HasVirtualPaths())
	{
		UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();

		const FContentBrowserItem DropFolderItem = ContentBrowserData->GetItemAtPath(SourcesData.VirtualPaths[0], EContentBrowserItemTypeFilter::IncludeFolders);
		if (DropFolderItem.IsValid() && DragDropHandler::HandleDragDropOnItem(DropFolderItem, DragDropEvent, AsShared()))
		{
			return FReply::Handled();
		}
	}
	else if (HasSingleCollectionSource())
	{
		TArray<FName> NewCollectionItems;

		if (TSharedPtr<FContentBrowserDataDragDropOp> ContentDragDropOp = DragDropEvent.GetOperationAs<FContentBrowserDataDragDropOp>())
		{
			for (const FContentBrowserItem& DraggedItem : ContentDragDropOp->GetDraggedFiles())
			{
				FName CollectionItemId;
				if (DraggedItem.TryGetCollectionId(CollectionItemId))
				{
					NewCollectionItems.Add(CollectionItemId);
				}
			}
		}
		else
		{
			const TArray<FAssetData> AssetDatas = AssetUtil::ExtractAssetDataFromDrag(DragDropEvent);
			Algo::Transform(AssetDatas, NewCollectionItems, &FAssetData::ObjectPath);
		}

		if (NewCollectionItems.Num() > 0)
		{
			FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();
			const FCollectionNameType& Collection = SourcesData.Collections[0];
			CollectionManagerModule.Get().AddToCollection(Collection.Name, Collection.Type, NewCollectionItems);

			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

FReply SAssetView::OnKeyChar( const FGeometry& MyGeometry,const FCharacterEvent& InCharacterEvent )
{
	const bool bIsControlOrCommandDown = InCharacterEvent.IsControlDown() || InCharacterEvent.IsCommandDown();
	
	const bool bTestOnly = false;
	if(HandleQuickJumpKeyDown(InCharacterEvent.GetCharacter(), bIsControlOrCommandDown, InCharacterEvent.IsAltDown(), bTestOnly).IsEventHandled())
	{
		return FReply::Handled();
	}

	// If the user pressed a key we couldn't handle, reset the quick-jump search
	ResetQuickJump();

	return FReply::Unhandled();
}

static bool IsValidObjectPath(const FString& Path)
{
	int32 NameStartIndex = INDEX_NONE;
	Path.FindChar(TCHAR('\''), NameStartIndex);
	if (NameStartIndex != INDEX_NONE)
	{
		int32 NameEndIndex = INDEX_NONE;
		Path.FindLastChar(TCHAR('\''), NameEndIndex);
		if (NameEndIndex > NameStartIndex)
		{
			const FString ClassName = Path.Left(NameStartIndex);
			const FString PathName = Path.Mid(NameStartIndex + 1, NameEndIndex - NameStartIndex - 1);

			UClass* Class = FindObject<UClass>(ANY_PACKAGE, *ClassName, true);
			if (Class)
			{
				return FPackageName::IsValidLongPackageName(FPackageName::ObjectPathToPackageName(PathName));
			}
		}
	}

	return false;
}

static bool ContainsT3D(const FString& ClipboardText)
{
	return (ClipboardText.StartsWith(TEXT("Begin Object")) && ClipboardText.EndsWith(TEXT("End Object")))
		|| (ClipboardText.StartsWith(TEXT("Begin Map")) && ClipboardText.EndsWith(TEXT("End Map")));
}

FReply SAssetView::OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent )
{
	const bool bIsControlOrCommandDown = InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown();
	
	if (bIsControlOrCommandDown && InKeyEvent.GetCharacter() == 'V' && IsAssetPathSelected())
	{
		FString AssetPaths;
		TArray<FString> AssetPathsSplit;

		// Get the copied asset paths
		FPlatformApplicationMisc::ClipboardPaste(AssetPaths);

		// Make sure the clipboard does not contain T3D
		AssetPaths.TrimEndInline();
		if (!ContainsT3D(AssetPaths))
		{
			AssetPaths.ParseIntoArrayLines(AssetPathsSplit);

			// Get assets and copy them
			TArray<UObject*> AssetsToCopy;
			for (const FString& AssetPath : AssetPathsSplit)
			{
				// Validate string
				if (IsValidObjectPath(AssetPath))
				{
					UObject* ObjectToCopy = LoadObject<UObject>(nullptr, *AssetPath);
					if (ObjectToCopy && !ObjectToCopy->IsA(UClass::StaticClass()))
					{
						AssetsToCopy.Add(ObjectToCopy);
					}
				}
			}

			if (AssetsToCopy.Num())
			{
				ContentBrowserUtils::CopyAssets(AssetsToCopy, SourcesData.VirtualPaths[0].ToString());
			}
		}

		return FReply::Handled();
	}
	// Swallow the key-presses used by the quick-jump in OnKeyChar to avoid other things (such as the viewport commands) getting them instead
	// eg) Pressing "W" without this would set the viewport to "translate" mode
	else if(HandleQuickJumpKeyDown(InKeyEvent.GetCharacter(), bIsControlOrCommandDown, InKeyEvent.IsAltDown(), /*bTestOnly*/true).IsEventHandled())
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SAssetView::OnMouseWheel( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if( MouseEvent.IsControlDown() )
	{
		const float DesiredScale = FMath::Clamp<float>(GetThumbnailScale() + ( MouseEvent.GetWheelDelta() * 0.05f ), 0.0f, 1.0f);
		if ( DesiredScale != GetThumbnailScale() )
		{
			SetThumbnailScale( DesiredScale );
		}		
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SAssetView::OnFocusChanging( const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent)
{
	ResetQuickJump();
}

TSharedRef<SAssetTileView> SAssetView::CreateTileView()
{
	return SNew(SAssetTileView)
		.SelectionMode( SelectionMode )
		.ListItemsSource(&FilteredAssetItems)
		.OnGenerateTile(this, &SAssetView::MakeTileViewWidget)
		.OnItemScrolledIntoView(this, &SAssetView::ItemScrolledIntoView)
		.OnContextMenuOpening(this, &SAssetView::OnGetContextMenuContent)
		.OnMouseButtonDoubleClick(this, &SAssetView::OnListMouseButtonDoubleClick)
		.OnSelectionChanged(this, &SAssetView::AssetSelectionChanged)
		.ItemHeight(this, &SAssetView::GetTileViewItemHeight)
		.ItemWidth(this, &SAssetView::GetTileViewItemWidth);
}

TSharedRef<SAssetListView> SAssetView::CreateListView()
{
	return SNew(SAssetListView)
		.SelectionMode( SelectionMode )
		.ListItemsSource(&FilteredAssetItems)
		.OnGenerateRow(this, &SAssetView::MakeListViewWidget)
		.OnItemScrolledIntoView(this, &SAssetView::ItemScrolledIntoView)
		.OnContextMenuOpening(this, &SAssetView::OnGetContextMenuContent)
		.OnMouseButtonDoubleClick(this, &SAssetView::OnListMouseButtonDoubleClick)
		.OnSelectionChanged(this, &SAssetView::AssetSelectionChanged)
		.ItemHeight(this, &SAssetView::GetListViewItemHeight);
}

TSharedRef<SAssetColumnView> SAssetView::CreateColumnView()
{
	TSharedPtr<SAssetColumnView> NewColumnView = SNew(SAssetColumnView)
		.SelectionMode( SelectionMode )
		.ListItemsSource(&FilteredAssetItems)
		.OnGenerateRow(this, &SAssetView::MakeColumnViewWidget)
		.OnItemScrolledIntoView(this, &SAssetView::ItemScrolledIntoView)
		.OnContextMenuOpening(this, &SAssetView::OnGetContextMenuContent)
		.OnMouseButtonDoubleClick(this, &SAssetView::OnListMouseButtonDoubleClick)
		.OnSelectionChanged(this, &SAssetView::AssetSelectionChanged)
		.Visibility(this, &SAssetView::GetColumnViewVisibility)
		.HeaderRow
		(
			SNew(SHeaderRow)
			.ResizeMode(ESplitterResizeMode::FixedSize)
			+ SHeaderRow::Column(SortManager.NameColumnId)
			.FillWidth(300)
			.SortMode( TAttribute< EColumnSortMode::Type >::Create( TAttribute< EColumnSortMode::Type >::FGetter::CreateSP( this, &SAssetView::GetColumnSortMode, SortManager.NameColumnId ) ) )
			.SortPriority(TAttribute< EColumnSortPriority::Type >::Create(TAttribute< EColumnSortPriority::Type >::FGetter::CreateSP(this, &SAssetView::GetColumnSortPriority, SortManager.NameColumnId)))
			.OnSort( FOnSortModeChanged::CreateSP( this, &SAssetView::OnSortColumnHeader ) )
			.DefaultLabel( LOCTEXT("Column_Name", "Name") )
			.ShouldGenerateWidget(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SAssetView::ShouldColumnGenerateWidget, SortManager.NameColumnId.ToString())))
			.MenuContent()
			[
				CreateRowHeaderMenuContent(SortManager.NameColumnId.ToString())
			]
		);

	NewColumnView->GetHeaderRow()->SetOnGetMaxRowSizeForColumn(FOnGetMaxRowSizeForColumn::CreateRaw(NewColumnView.Get(), &SAssetColumnView::GetMaxRowSizeForColumn));


	NumVisibleColumns = HiddenColumnNames.Contains(SortManager.NameColumnId.ToString()) ? 0 : 1;

	if(bShowTypeInColumnView)
	{
		NewColumnView->GetHeaderRow()->AddColumn(
				SHeaderRow::Column(SortManager.ClassColumnId)
				.FillWidth(160)
				.SortMode(TAttribute< EColumnSortMode::Type >::Create(TAttribute< EColumnSortMode::Type >::FGetter::CreateSP(this, &SAssetView::GetColumnSortMode, SortManager.ClassColumnId)))
				.SortPriority(TAttribute< EColumnSortPriority::Type >::Create(TAttribute< EColumnSortPriority::Type >::FGetter::CreateSP(this, &SAssetView::GetColumnSortPriority, SortManager.ClassColumnId)))
				.OnSort(FOnSortModeChanged::CreateSP(this, &SAssetView::OnSortColumnHeader))
				.DefaultLabel(LOCTEXT("Column_Class", "Type"))
				.ShouldGenerateWidget(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SAssetView::ShouldColumnGenerateWidget, SortManager.ClassColumnId.ToString())))
				.MenuContent()
				[
					CreateRowHeaderMenuContent(SortManager.ClassColumnId.ToString())
				]
			);

		NumVisibleColumns += HiddenColumnNames.Contains(SortManager.ClassColumnId.ToString()) ? 0 : 1;
	}


	if (bShowPathInColumnView)
	{
		NewColumnView->GetHeaderRow()->AddColumn(
				SHeaderRow::Column(SortManager.PathColumnId)
				.FillWidth(160)
				.SortMode(TAttribute< EColumnSortMode::Type >::Create(TAttribute< EColumnSortMode::Type >::FGetter::CreateSP(this, &SAssetView::GetColumnSortMode, SortManager.PathColumnId)))
				.SortPriority(TAttribute< EColumnSortPriority::Type >::Create(TAttribute< EColumnSortPriority::Type >::FGetter::CreateSP(this, &SAssetView::GetColumnSortPriority, SortManager.PathColumnId)))
				.OnSort(FOnSortModeChanged::CreateSP(this, &SAssetView::OnSortColumnHeader))
				.DefaultLabel(LOCTEXT("Column_Path", "Path"))
				.ShouldGenerateWidget(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SAssetView::ShouldColumnGenerateWidget, SortManager.PathColumnId.ToString())))
				.MenuContent()
				[
					CreateRowHeaderMenuContent(SortManager.PathColumnId.ToString())
				]
			);


		NumVisibleColumns += HiddenColumnNames.Contains(SortManager.PathColumnId.ToString()) ? 0 : 1;
	}

	return NewColumnView.ToSharedRef();
}

bool SAssetView::IsValidSearchToken(const FString& Token) const
{
	if ( Token.Len() == 0 )
	{
		return false;
	}

	// A token may not be only apostrophe only, or it will match every asset because the text filter compares against the pattern Class'ObjectPath'
	if ( Token.Len() == 1 && Token[0] == '\'' )
	{
		return false;
	}

	return true;
}

FContentBrowserDataFilter SAssetView::CreateBackendDataFilter() const
{
	// Assemble the filter using the current sources
	// Force recursion when the user is searching
	const bool bHasCollections = SourcesData.HasCollections();
	const bool bRecurse = ShouldFilterRecursively();
	const bool bUsingFolders = IsShowingFolders() && !bRecurse;

	// Check whether any legacy delegates are bound (the Content Browser doesn't use these, only pickers do)
	// These limit the view to things that might use FAssetData
	const bool bHasLegacyDelegateBindings 
		=  OnIsAssetValidForCustomToolTip.IsBound()
		|| OnGetCustomAssetToolTip.IsBound()
		|| OnVisualizeAssetToolTip.IsBound()
		|| OnAssetToolTipClosing.IsBound()
		|| OnShouldFilterAsset.IsBound();

	FContentBrowserDataFilter DataFilter;
	DataFilter.bRecursivePaths = bRecurse || !bUsingFolders || bHasCollections;

	DataFilter.ItemTypeFilter = EContentBrowserItemTypeFilter::IncludeFiles
		| ((bUsingFolders && !bHasCollections) ? EContentBrowserItemTypeFilter::IncludeFolders : EContentBrowserItemTypeFilter::IncludeNone);

	DataFilter.ItemCategoryFilter = bHasLegacyDelegateBindings ? EContentBrowserItemCategoryFilter::IncludeAssets : InitialCategoryFilter;
	if (IsShowingCppContent())
	{
		DataFilter.ItemCategoryFilter |= EContentBrowserItemCategoryFilter::IncludeClasses;
	}
	else
	{
		DataFilter.ItemCategoryFilter &= ~EContentBrowserItemCategoryFilter::IncludeClasses;
	}
	DataFilter.ItemCategoryFilter |= EContentBrowserItemCategoryFilter::IncludeCollections;

	DataFilter.ItemAttributeFilter = EContentBrowserItemAttributeFilter::IncludeProject
		| (IsShowingEngineContent() ? EContentBrowserItemAttributeFilter::IncludeEngine : EContentBrowserItemAttributeFilter::IncludeNone)
		| (IsShowingPluginContent() ? EContentBrowserItemAttributeFilter::IncludePlugins : EContentBrowserItemAttributeFilter::IncludeNone)
		| (IsShowingDevelopersContent() ? EContentBrowserItemAttributeFilter::IncludeDeveloper : EContentBrowserItemAttributeFilter::IncludeNone)
		| (IsShowingLocalizedContent() ? EContentBrowserItemAttributeFilter::IncludeLocalized : EContentBrowserItemAttributeFilter::IncludeNone);

	ContentBrowserUtils::AppendAssetFilterToContentBrowserFilter(BackendFilter, AssetClassBlacklist, FolderBlacklist, DataFilter);

	if (bHasCollections && !SourcesData.IsDynamicCollection())
	{
		FContentBrowserDataCollectionFilter& CollectionFilter = DataFilter.ExtraFilters.FindOrAddFilter<FContentBrowserDataCollectionFilter>();
		CollectionFilter.SelectedCollections = SourcesData.Collections;
		CollectionFilter.bIncludeChildCollections = !bUsingFolders;
	}

	if (OnGetCustomSourceAssets.IsBound())
	{
		FContentBrowserDataLegacyFilter& LegacyFilter = DataFilter.ExtraFilters.FindOrAddFilter<FContentBrowserDataLegacyFilter>();
		LegacyFilter.OnGetCustomSourceAssets = OnGetCustomSourceAssets;
	}

	return DataFilter;
}

void SAssetView::RefreshSourceItems()
{
	const double RefreshSourceItemsStartTime = FPlatformTime::Seconds();

	FilteredAssetItems.Reset();
	FilteredAssetItemTypeCounts.Reset();
	VisibleItems.Reset();
	RelevantThumbnails.Reset();

	TMap<FContentBrowserItemKey, TSharedPtr<FAssetViewItem>> PreviousAvailableBackendItems = MoveTemp(AvailableBackendItems);
	AvailableBackendItems.Reset();
	ItemsPendingPriorityFilter.Reset();
	ItemsPendingFrontendFilter.Reset();
	{
		UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
		const FContentBrowserDataFilter DataFilter = CreateBackendDataFilter();

		bWereItemsRecursivelyFiltered = DataFilter.bRecursivePaths;

		if (SourcesData.HasCollections() && EnumHasAnyFlags(DataFilter.ItemCategoryFilter, EContentBrowserItemCategoryFilter::IncludeCollections))
		{
			// If we are showing collections then we may need to add dummy folder items for the child collections
			// Note: We don't check the IncludeFolders flag here, as that is forced to false when collections are selected,
			// instead we check the state of bIncludeChildCollections which will be false when we want to show collection folders
			const FContentBrowserDataCollectionFilter* CollectionFilter = DataFilter.ExtraFilters.FindFilter<FContentBrowserDataCollectionFilter>();
			if (CollectionFilter && !CollectionFilter->bIncludeChildCollections)
			{
				FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();
				
				TArray<FCollectionNameType> ChildCollections;
				for(const FCollectionNameType& Collection : SourcesData.Collections)
				{
					ChildCollections.Reset();
					CollectionManagerModule.Get().GetChildCollections(Collection.Name, Collection.Type, ChildCollections);

					for (const FCollectionNameType& ChildCollection : ChildCollections)
					{
						// Use "Collections" as the root of the path to avoid this being confused with other view folders - see ContentBrowserUtils::IsCollectionPath
						FContentBrowserItemData FolderItemData(
							nullptr, 
							EContentBrowserItemFlags::Type_Folder | EContentBrowserItemFlags::Category_Collection, 
							*FString::Printf(TEXT("/Collections/%s/%s"), ECollectionShareType::ToString(ChildCollection.Type), *ChildCollection.Name.ToString()), 
							ChildCollection.Name, 
							FText::FromName(ChildCollection.Name), 
							nullptr
							);

						const FContentBrowserItemKey FolderItemDataKey(FolderItemData);
						AvailableBackendItems.Add(FolderItemDataKey, MakeShared<FAssetViewItem>(MoveTemp(FolderItemData)));
					}
				}
			}
		}

		static const FName RootPath = "/";
		const TArrayView<const FName> DataSourcePaths = SourcesData.HasVirtualPaths() ? MakeArrayView(SourcesData.VirtualPaths) : MakeArrayView(&RootPath, 1);
		for (const FName& DataSourcePath : DataSourcePaths)
		{
			ContentBrowserData->EnumerateItemsUnderPath(DataSourcePath, DataFilter, [this, &PreviousAvailableBackendItems](FContentBrowserItemData&& InItemData)
			{
				const FContentBrowserItemKey ItemDataKey(InItemData);
				const uint32 ItemDataKeyHash = GetTypeHash(ItemDataKey);
				
				TSharedPtr<FAssetViewItem>& NewItem = AvailableBackendItems.FindOrAddByHash(ItemDataKeyHash, ItemDataKey);
				if (!NewItem && InItemData.IsFile())
				{
					// Re-use the old view item where possible to avoid list churn when our backend view already included the item
					if (TSharedPtr<FAssetViewItem>* PreviousItem = PreviousAvailableBackendItems.FindByHash(ItemDataKeyHash, ItemDataKey))
					{
						NewItem = *PreviousItem;
						NewItem->ClearCachedCustomColumns();
					}
				}
				if (NewItem)
				{
					NewItem->AppendItemData(InItemData);
					NewItem->CacheCustomColumns(CustomColumns, true, true, false /*bUpdateExisting*/);
				}
				else
				{
					NewItem = MakeShared<FAssetViewItem>(MoveTemp(InItemData));
				}

				return true;
			});
		}
	}

	UE_LOG(LogContentBrowser, VeryVerbose, TEXT("AssetView - RefreshSourceItems completed in %0.4f seconds"), FPlatformTime::Seconds() - RefreshSourceItemsStartTime);
}

bool SAssetView::IsFilteringRecursively() const
{
	// In some cases we want to not filter recursively even if we have a backend filter (e.g. the open level window)
	// Most of the time, bFilterRecursivelyWithBackendFilter is true
	return bFilterRecursivelyWithBackendFilter && GetDefault<UContentBrowserSettings>()->FilterRecursively;
}

bool SAssetView::IsToggleFilteringRecursivelyAllowed() const
{
	return bFilterRecursivelyWithBackendFilter;
}

void SAssetView::ToggleFilteringRecursively()
{
	check(IsToggleFilteringRecursivelyAllowed());
	GetMutableDefault<UContentBrowserSettings>()->FilterRecursively = !GetDefault<UContentBrowserSettings>()->FilterRecursively;
	GetMutableDefault<UContentBrowserSettings>()->PostEditChange();
}

bool SAssetView::ShouldFilterRecursively() const
{
	// Quick check for conditions which force recursive filtering
	if (bUserSearching)
	{
		return true;
	}

	if (IsFilteringRecursively() && !BackendFilter.IsEmpty() )
	{
		return true;
	}

	// Otherwise, check if there are any non-inverse frontend filters selected
	if (FrontendFilters.IsValid())
	{
		for (int32 FilterIndex = 0; FilterIndex < FrontendFilters->Num(); ++FilterIndex)
		{
			const auto* Filter = static_cast<FFrontendFilter*>(FrontendFilters->GetFilterAtIndex(FilterIndex).Get());
			if (Filter)
			{
				if (!Filter->IsInverseFilter())
				{
					return true;
				}
			}
		}
	}

	// No sources - view will show everything
	if (SourcesData.IsEmpty())
	{
		return true;
	}

	// No filters, do not override folder view with recursive filtering
	return false;
}

void SAssetView::RefreshFilteredItems()
{
	const double RefreshFilteredItemsStartTime = FPlatformTime::Seconds();

	ItemsPendingFrontendFilter.Reset();
	FilteredAssetItems.Reset();
	FilteredAssetItemTypeCounts.Reset();
	RelevantThumbnails.Reset();

	LastSortTime = 0;
	bPendingSortFilteredItems = true;

	ItemsPendingFrontendFilter.Reserve(AvailableBackendItems.Num());
	for (const auto& AvailableBackendItemPair : AvailableBackendItems)
	{
		ItemsPendingFrontendFilter.Add(AvailableBackendItemPair.Value);
	}

	// Let the frontend filters know the currently used asset filter in case it is necessary to conditionally filter based on path or class filters
	if (IsFrontendFilterActive() && FrontendFilters.IsValid())
	{
		static const FName RootPath = "/";
		const TArrayView<const FName> DataSourcePaths = SourcesData.HasVirtualPaths() ? MakeArrayView(SourcesData.VirtualPaths) : MakeArrayView(&RootPath, 1);

		const FContentBrowserDataFilter DataFilter = CreateBackendDataFilter();

		for (int32 FilterIdx = 0; FilterIdx < FrontendFilters->Num(); ++FilterIdx)
		{
			// There are only FFrontendFilters in this collection
			const TSharedPtr<FFrontendFilter>& Filter = StaticCastSharedPtr<FFrontendFilter>(FrontendFilters->GetFilterAtIndex(FilterIdx));
			if (Filter.IsValid())
			{
				Filter->SetCurrentFilter(DataSourcePaths, DataFilter);
			}
		}
	}

	UE_LOG(LogContentBrowser, VeryVerbose, TEXT("AssetView - RefreshFilteredItems completed in %0.4f seconds"), FPlatformTime::Seconds() - RefreshFilteredItemsStartTime);
}

void SAssetView::ToggleShowAllFolder()
{
	GetMutableDefault<UContentBrowserSettings>()->ShowAllFolder = !GetDefault<UContentBrowserSettings>()->ShowAllFolder;
	GetMutableDefault<UContentBrowserSettings>()->PostEditChange();
}

bool SAssetView::IsShowingAllFolder() const
{
	return GetDefault<UContentBrowserSettings>()->ShowAllFolder;
}

void SAssetView::ToggleOrganizeFolders()
{
	GetMutableDefault<UContentBrowserSettings>()->OrganizeFolders = !GetDefault<UContentBrowserSettings>()->OrganizeFolders;
	GetMutableDefault<UContentBrowserSettings>()->PostEditChange();
}

bool SAssetView::IsOrganizingFolders() const
{
	return GetDefault<UContentBrowserSettings>()->OrganizeFolders;
}

void SAssetView::SetMajorityAssetType(FName NewMajorityAssetType)
{
	if (CurrentViewType != EAssetViewType::Column)
	{
		return;
	}

	auto IsFixedColumn = [this](FName InColumnId)
	{
		const bool bIsFixedNameColumn = InColumnId == SortManager.NameColumnId;
		const bool bIsFixedClassColumn = bShowTypeInColumnView && InColumnId == SortManager.ClassColumnId;
		const bool bIsFixedPathColumn = bShowPathInColumnView && InColumnId == SortManager.PathColumnId;
		return bIsFixedNameColumn || bIsFixedClassColumn || bIsFixedPathColumn;
	};


	FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	bool bHasDynamicColumns = ContentBrowserModule.IsDynamicTagAssetClass(NewMajorityAssetType);

	if ( NewMajorityAssetType != MajorityAssetType || bHasDynamicColumns)
	{
		UE_LOG(LogContentBrowser, Verbose, TEXT("The majority of assets in the view are of type: %s"), *NewMajorityAssetType.ToString());

		MajorityAssetType = NewMajorityAssetType;

		TArray<FName> AddedColumns;

		// Since the asset type has changed, remove all columns except name and class
		const TIndirectArray<SHeaderRow::FColumn>& Columns = ColumnView->GetHeaderRow()->GetColumns();

		for ( int32 ColumnIdx = Columns.Num() - 1; ColumnIdx >= 0; --ColumnIdx )
		{
			const FName ColumnId = Columns[ColumnIdx].ColumnId;

			if ( ColumnId != NAME_None && !IsFixedColumn(ColumnId) )
			{
				ColumnView->GetHeaderRow()->RemoveColumn(ColumnId);
			}
		}

		// Keep track of the current column name to see if we need to change it now that columns are being removed
		// Name, Class, and Path are always relevant
		struct FSortOrder
		{
			bool bSortRelevant;
			FName SortColumn;
			FSortOrder(bool bInSortRelevant, const FName& InSortColumn) : bSortRelevant(bInSortRelevant), SortColumn(InSortColumn) {}
		};
		TArray<FSortOrder> CurrentSortOrder;
		for (int32 PriorityIdx = 0; PriorityIdx < EColumnSortPriority::Max; PriorityIdx++)
		{
			const FName SortColumn = SortManager.GetSortColumnId(static_cast<EColumnSortPriority::Type>(PriorityIdx));
			if (SortColumn != NAME_None)
			{
				const bool bSortRelevant = SortColumn == FAssetViewSortManager::NameColumnId
					|| SortColumn == FAssetViewSortManager::ClassColumnId
					|| SortColumn == FAssetViewSortManager::PathColumnId;
				CurrentSortOrder.Add(FSortOrder(bSortRelevant, SortColumn));
			}
		}

		// Add custom columns
		for (const FAssetViewCustomColumn& Column : CustomColumns)
		{
			FName TagName = Column.ColumnName;

			if (AddedColumns.Contains(TagName))
			{
				continue;
			}
			AddedColumns.Add(TagName);

			ColumnView->GetHeaderRow()->AddColumn(
				SHeaderRow::Column(TagName)
				.SortMode(TAttribute< EColumnSortMode::Type >::Create(TAttribute< EColumnSortMode::Type >::FGetter::CreateSP(this, &SAssetView::GetColumnSortMode, TagName)))
				.SortPriority(TAttribute< EColumnSortPriority::Type >::Create(TAttribute< EColumnSortPriority::Type >::FGetter::CreateSP(this, &SAssetView::GetColumnSortPriority, TagName)))
				.OnSort(FOnSortModeChanged::CreateSP(this, &SAssetView::OnSortColumnHeader))
				.DefaultLabel(Column.DisplayName)
				.DefaultTooltip(Column.TooltipText)
				.FillWidth(180)
				.ShouldGenerateWidget(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SAssetView::ShouldColumnGenerateWidget, TagName.ToString())))
				.MenuContent()
				[
					CreateRowHeaderMenuContent(TagName.ToString())
				]);

			NumVisibleColumns += HiddenColumnNames.Contains(TagName.ToString()) ? 0 : 1;

			// If we found a tag the matches the column we are currently sorting on, there will be no need to change the column
			for (int32 SortIdx = 0; SortIdx < CurrentSortOrder.Num(); SortIdx++)
			{
				if (TagName == CurrentSortOrder[SortIdx].SortColumn)
				{
					CurrentSortOrder[SortIdx].bSortRelevant = true;
				}
			}
		}

		// If we have a new majority type, add the new type's columns
		if (NewMajorityAssetType != NAME_None)
		{
			FContentBrowserItemDataAttributeValues UnionedItemAttributes;

			// Find an item of this type so we can extract the relevant attribute data from it
			TSharedPtr<FAssetViewItem> MajorityAssetItem;
			for (const TSharedPtr<FAssetViewItem>& FilteredAssetItem : FilteredAssetItems)
			{
				const FContentBrowserItemDataAttributeValue ClassValue = FilteredAssetItem->GetItem().GetItemAttribute(ContentBrowserItemAttributes::ItemTypeName);
				if (ClassValue.IsValid() && ClassValue.GetValue<FName>() == NewMajorityAssetType)
				{
					if (bHasDynamicColumns)
					{
						const FContentBrowserItemDataAttributeValues ItemAttributes = FilteredAssetItem->GetItem().GetItemAttributes(/*bIncludeMetaData*/true);
						UnionedItemAttributes.Append(ItemAttributes); 
						MajorityAssetItem = FilteredAssetItem;
					}
					else
					{
						MajorityAssetItem = FilteredAssetItem;
						break;
					}
				}
			}

			// Determine the columns by querying the reference item
			if (MajorityAssetItem)
			{
				FContentBrowserItemDataAttributeValues ItemAttributes = bHasDynamicColumns ? UnionedItemAttributes : MajorityAssetItem->GetItem().GetItemAttributes(/*bIncludeMetaData*/true);

				// Add a column for every tag that isn't hidden or using a reserved name
				for (const auto& TagPair : ItemAttributes)
				{
					if (IsFixedColumn(TagPair.Key))
					{
						// Reserved name
						continue;
					}

					if (TagPair.Value.GetMetaData().AttributeType == UObject::FAssetRegistryTag::TT_Hidden)
					{
						// Hidden attribute
						continue;
					}

					if (!OnAssetTagWantsToBeDisplayed.IsBound() || OnAssetTagWantsToBeDisplayed.Execute(NewMajorityAssetType, TagPair.Key))
					{
						if (AddedColumns.Contains(TagPair.Key))
						{
							continue;
						}
						AddedColumns.Add(TagPair.Key);

						ColumnView->GetHeaderRow()->AddColumn(
							SHeaderRow::Column(TagPair.Key)
							.SortMode(TAttribute< EColumnSortMode::Type >::Create(TAttribute< EColumnSortMode::Type >::FGetter::CreateSP(this, &SAssetView::GetColumnSortMode, TagPair.Key)))
							.SortPriority(TAttribute< EColumnSortPriority::Type >::Create(TAttribute< EColumnSortPriority::Type >::FGetter::CreateSP(this, &SAssetView::GetColumnSortPriority, TagPair.Key)))
							.OnSort(FOnSortModeChanged::CreateSP(this, &SAssetView::OnSortColumnHeader))
							.DefaultLabel(TagPair.Value.GetMetaData().DisplayName)
							.DefaultTooltip(TagPair.Value.GetMetaData().TooltipText)
							.FillWidth(180)
							.ShouldGenerateWidget(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SAssetView::ShouldColumnGenerateWidget, TagPair.Key.ToString())))
							.MenuContent()
							[
								CreateRowHeaderMenuContent(TagPair.Key.ToString())
							]);

						NumVisibleColumns += HiddenColumnNames.Contains(TagPair.Key.ToString()) ? 0 : 1;

						// If we found a tag the matches the column we are currently sorting on, there will be no need to change the column
						for (int32 SortIdx = 0; SortIdx < CurrentSortOrder.Num(); SortIdx++)
						{
							if (TagPair.Key == CurrentSortOrder[SortIdx].SortColumn)
							{
								CurrentSortOrder[SortIdx].bSortRelevant = true;
							}
						}
					}
				}
			}
		}

		// Are any of the sort columns irrelevant now, if so remove them from the list
		bool CurrentSortChanged = false;
		for (int32 SortIdx = CurrentSortOrder.Num() - 1; SortIdx >= 0; SortIdx--)
		{
			if (!CurrentSortOrder[SortIdx].bSortRelevant)
			{
				CurrentSortOrder.RemoveAt(SortIdx);
				CurrentSortChanged = true;
			}
		}
		if (CurrentSortOrder.Num() > 0 && CurrentSortChanged)
		{
			// Sort order has changed, update the columns keeping those that are relevant
			int32 PriorityNum = EColumnSortPriority::Primary;
			for (int32 SortIdx = 0; SortIdx < CurrentSortOrder.Num(); SortIdx++)
			{
				check(CurrentSortOrder[SortIdx].bSortRelevant);
				if (!SortManager.SetOrToggleSortColumn(static_cast<EColumnSortPriority::Type>(PriorityNum), CurrentSortOrder[SortIdx].SortColumn))
				{
					// Toggle twice so mode is preserved if this isn't a new column assignation
					SortManager.SetOrToggleSortColumn(static_cast<EColumnSortPriority::Type>(PriorityNum), CurrentSortOrder[SortIdx].SortColumn);
				}				
				bPendingSortFilteredItems = true;
				PriorityNum++;
			}
		}
		else if (CurrentSortOrder.Num() == 0)
		{
			// If the current sort column is no longer relevant, revert to "Name" and resort when convenient
			SortManager.ResetSort();
			bPendingSortFilteredItems = true;
		}
	}
}

void SAssetView::OnAssetsAddedToCollection( const FCollectionNameType& Collection, const TArray< FName >& ObjectPaths )
{
	if ( !SourcesData.Collections.Contains( Collection ) )
	{
		return;
	}

	RequestSlowFullListRefresh();
}

void SAssetView::OnAssetsRemovedFromCollection( const FCollectionNameType& Collection, const TArray< FName >& ObjectPaths )
{
	if ( !SourcesData.Collections.Contains( Collection ) )
	{
		return;
	}

	RequestSlowFullListRefresh();
}

void SAssetView::OnCollectionRenamed( const FCollectionNameType& OriginalCollection, const FCollectionNameType& NewCollection )
{
	int32 FoundIndex = INDEX_NONE;
	if ( SourcesData.Collections.Find( OriginalCollection, FoundIndex ) )
	{
		SourcesData.Collections[ FoundIndex ] = NewCollection;
	}
}

void SAssetView::OnCollectionUpdated( const FCollectionNameType& Collection )
{
	// A collection has changed in some way, so we need to refresh our backend list
	RequestSlowFullListRefresh();
}

void SAssetView::OnFrontendFiltersChanged()
{
	RequestQuickFrontendListRefresh();

	// If we're not operating on recursively filtered data, we need to ensure a full slow
	// refresh is performed.
	if ( ShouldFilterRecursively() && !bWereItemsRecursivelyFiltered )
	{
		RequestSlowFullListRefresh();
	}
}

bool SAssetView::IsFrontendFilterActive() const
{
	return ( FrontendFilters.IsValid() && FrontendFilters->Num() > 0 );
}

bool SAssetView::PassesCurrentFrontendFilter(const FContentBrowserItem& Item) const
{
	return !FrontendFilters.IsValid() || FrontendFilters->PassesAllFilters(Item);
}

void SAssetView::SortList(bool bSyncToSelection)
{
	if ( !IsRenamingAsset() )
	{
		SortManager.SortList(FilteredAssetItems, MajorityAssetType, CustomColumns);

		// Update the thumbnails we were using since the order has changed
		bPendingUpdateThumbnails = true;

		if ( bSyncToSelection )
		{
			// Make sure the selection is in view
			const bool bFocusOnSync = false;
			SyncToSelection(bFocusOnSync);
		}

		RefreshList();
		bPendingSortFilteredItems = false;
		LastSortTime = CurrentTime;
	}
	else
	{
		bPendingSortFilteredItems = true;
	}
}

FLinearColor SAssetView::GetThumbnailHintColorAndOpacity() const
{
	//We update this color in tick instead of here as an optimization
	return ThumbnailHintColorAndOpacity;
}

FSlateColor SAssetView::GetViewButtonForegroundColor() const
{
	static const FName InvertedForegroundName("InvertedForeground");
	static const FName DefaultForegroundName("DefaultForeground");

	return ViewOptionsComboButton->IsHovered() ? FEditorStyle::GetSlateColor(InvertedForegroundName) : FEditorStyle::GetSlateColor(DefaultForegroundName);
}

TSharedRef<SWidget> SAssetView::GetViewButtonContent()
{
	SAssetView::RegisterGetViewButtonMenu();

	// Get all menu extenders for this context menu from the content browser module
	FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>( TEXT("ContentBrowser") );
	TArray<FContentBrowserMenuExtender> MenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewViewMenuExtenders();

	TArray<TSharedPtr<FExtender>> Extenders;
	for (int32 i = 0; i < MenuExtenderDelegates.Num(); ++i)
	{
		if (MenuExtenderDelegates[i].IsBound())
		{
			Extenders.Add(MenuExtenderDelegates[i].Execute());
		}
	}
	TSharedPtr<FExtender> MenuExtender = FExtender::Combine(Extenders);

	UContentBrowserAssetViewContextMenuContext* Context = NewObject<UContentBrowserAssetViewContextMenuContext>();
	Context->AssetView = SharedThis(this);
	FToolMenuContext MenuContext(nullptr, MenuExtender, Context);

	if (OnExtendAssetViewOptionsMenuContext.IsBound())
	{
		OnExtendAssetViewOptionsMenuContext.Execute(MenuContext);
	}

	return UToolMenus::Get()->GenerateWidget("ContentBrowser.AssetViewOptions", MenuContext);
}

void SAssetView::RegisterGetViewButtonMenu()
{
	if (!UToolMenus::Get()->IsMenuRegistered("ContentBrowser.AssetViewOptions"))
	{
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu("ContentBrowser.AssetViewOptions");
		Menu->bCloseSelfOnly = true;
		Menu->AddDynamicSection("DynamicContent", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
		{
			if (UContentBrowserAssetViewContextMenuContext* Context = InMenu->FindContext<UContentBrowserAssetViewContextMenuContext>())
			{
				if (Context->AssetView.IsValid())
				{
					Context->AssetView.Pin()->PopulateViewButtonMenu(InMenu);
				}
			}
		}));
	}
}

void SAssetView::PopulateViewButtonMenu(UToolMenu* Menu)
{
	{
		FToolMenuSection& Section = Menu->AddSection("AssetViewType", LOCTEXT("ViewTypeHeading", "View Type"));
		Section.AddMenuEntry(
			"TileView",
			LOCTEXT("TileViewOption", "Tiles"),
			LOCTEXT("TileViewOptionToolTip", "View assets as tiles in a grid."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SAssetView::SetCurrentViewTypeFromMenu, EAssetViewType::Tile ),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP( this, &SAssetView::IsCurrentViewType, EAssetViewType::Tile )
				),
			EUserInterfaceActionType::RadioButton
			);

		Section.AddMenuEntry(
			"ListView",
			LOCTEXT("ListViewOption", "List"),
			LOCTEXT("ListViewOptionToolTip", "View assets in a list with thumbnails."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SAssetView::SetCurrentViewTypeFromMenu, EAssetViewType::List ),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP( this, &SAssetView::IsCurrentViewType, EAssetViewType::List )
				),
			EUserInterfaceActionType::RadioButton
			);

		Section.AddMenuEntry(
			"ColumnView",
			LOCTEXT("ColumnViewOption", "Columns"),
			LOCTEXT("ColumnViewOptionToolTip", "View assets in a list with columns of details."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SAssetView::SetCurrentViewTypeFromMenu, EAssetViewType::Column ),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP( this, &SAssetView::IsCurrentViewType, EAssetViewType::Column )
				),
			EUserInterfaceActionType::RadioButton
			);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("View", LOCTEXT("ViewHeading", "View"));
		auto CreateShowFoldersSubMenu = [this](UToolMenu* SubMenu)
		{
			FToolMenuSection& ShowEmptyFoldersSection = SubMenu->AddSection("ShowEmptyFolders");
			ShowEmptyFoldersSection.AddMenuEntry(
				"ShowEmptyFolders",
				LOCTEXT("ShowEmptyFoldersOption", "Show Empty Folders"),
				LOCTEXT("ShowEmptyFoldersOptionToolTip", "Show empty folders in the view as well as assets?"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP( this, &SAssetView::ToggleShowEmptyFolders ),
					FCanExecuteAction::CreateSP( this, &SAssetView::IsToggleShowEmptyFoldersAllowed ),
					FIsActionChecked::CreateSP( this, &SAssetView::IsShowingEmptyFolders )
				),
				EUserInterfaceActionType::ToggleButton
			);
		};

		Section.AddEntry(FToolMenuEntry::InitSubMenu(
			"ShowFolders",
			LOCTEXT("ShowFoldersOption", "Show Folders"),
			LOCTEXT("ShowFoldersOptionToolTip", "Show folders in the view as well as assets?"),
			FNewToolMenuDelegate::CreateLambda(CreateShowFoldersSubMenu),
			FUIAction(
				FExecuteAction::CreateSP( this, &SAssetView::ToggleShowFolders ),
				FCanExecuteAction::CreateSP( this, &SAssetView::IsToggleShowFoldersAllowed ),
				FIsActionChecked::CreateSP( this, &SAssetView::IsShowingFolders )
			),
			EUserInterfaceActionType::ToggleButton
		));

		Section.AddMenuEntry(
			"ShowFavorite",
			LOCTEXT("ShowFavoriteOptions", "Show Favorites"),
			LOCTEXT("ShowFavoriteOptionToolTip", "Show the favorite folders in the view?"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SAssetView::ToggleShowFavorites),
				FCanExecuteAction::CreateSP(this, &SAssetView::IsToggleShowFavoritesAllowed),
				FIsActionChecked::CreateSP(this, &SAssetView::IsShowingFavorites)
			),
			EUserInterfaceActionType::ToggleButton
		);

		Section.AddMenuEntry(
			"DockCollections",
			LOCTEXT("DockCollectionsOptions", "Dock Collections"),
			LOCTEXT("DockCollectionsOptionToolTip", "Dock the collections view under the path view?"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SAssetView::ToggleDockCollections),
				FCanExecuteAction::CreateSP(this, &SAssetView::IsToggleDockCollectionsAllowed),
				FIsActionChecked::CreateSP(this, &SAssetView::HasDockedCollections)
			),
			EUserInterfaceActionType::ToggleButton
		);

		Section.AddMenuEntry(
			"FilterRecursively",
			LOCTEXT("FilterRecursivelyOption", "Filter Recursively"),
			LOCTEXT("FilterRecursivelyOptionToolTip", "Should filters apply recursively in the view?"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SAssetView::ToggleFilteringRecursively),
				FCanExecuteAction::CreateSP(this, &SAssetView::IsToggleFilteringRecursivelyAllowed),
				FIsActionChecked::CreateSP(this, &SAssetView::IsFilteringRecursively)
			),
			EUserInterfaceActionType::ToggleButton
		);

		//Section.AddMenuEntry(
		//	"ShowAllFolder",
		//	LOCTEXT("ShowAllFolderOption", "Show All Folder"),
		//	LOCTEXT("ShowAllFolderOptionToolTip", "Show the all folder in the view?"),
		//	FSlateIcon(),
		//	FUIAction(
		//		FExecuteAction::CreateSP(this, &SAssetView::ToggleShowAllFolder),
		//		FCanExecuteAction(),
		//		FIsActionChecked::CreateSP(this, &SAssetView::IsShowingAllFolder)
		//	),
		//	EUserInterfaceActionType::ToggleButton
		//);

		//Section.AddMenuEntry(
		//	"OrganizeFolders",
		//	LOCTEXT("OrganizeFoldersOption", "Organize Folders"),
		//	LOCTEXT("OrganizeFoldersOptionToolTip", "Organize folders in the view?"),
		//	FSlateIcon(),
		//	FUIAction(
		//		FExecuteAction::CreateSP(this, &SAssetView::ToggleOrganizeFolders),
		//		FCanExecuteAction(),
		//		FIsActionChecked::CreateSP(this, &SAssetView::IsOrganizingFolders)
		//	),
		//	EUserInterfaceActionType::ToggleButton
		//);

		if (bShowPathViewFilters)
		{
			Section.AddSubMenu(
				"PathViewFilters",
				LOCTEXT("PathViewFilters", "Path View Filters"),
				LOCTEXT("PathViewFilters_ToolTip", "Path View Filters"),
				FNewToolMenuDelegate());
		}
	}

	{
		FToolMenuSection& Section = Menu->AddSection("Content", LOCTEXT("ContentHeading", "Content"));
		Section.AddMenuEntry(
			"ShowCppClasses",
			LOCTEXT("ShowCppClassesOption", "Show C++ Classes"),
			LOCTEXT("ShowCppClassesOptionToolTip", "Show C++ classes in the view?"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SAssetView::ToggleShowCppContent ),
				FCanExecuteAction::CreateSP( this, &SAssetView::IsToggleShowCppContentAllowed ),
				FIsActionChecked::CreateSP( this, &SAssetView::IsShowingCppContent )
			),
			EUserInterfaceActionType::ToggleButton
		);

		Section.AddMenuEntry(
			"ShowDevelopersContent",
			LOCTEXT("ShowDevelopersContentOption", "Show Developers Content"),
			LOCTEXT("ShowDevelopersContentOptionToolTip", "Show developers content in the view?"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SAssetView::ToggleShowDevelopersContent ),
				FCanExecuteAction::CreateSP( this, &SAssetView::IsToggleShowDevelopersContentAllowed ),
				FIsActionChecked::CreateSP( this, &SAssetView::IsShowingDevelopersContent )
				),
			EUserInterfaceActionType::ToggleButton
		);

		Section.AddMenuEntry(
			"ShowEngineFolder",
			LOCTEXT("ShowEngineFolderOption", "Show Engine Content"),
			LOCTEXT("ShowEngineFolderOptionToolTip", "Show engine content in the view?"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SAssetView::ToggleShowEngineContent ),
				FCanExecuteAction::CreateSP( this, &SAssetView::IsToggleShowEngineContentAllowed),
				FIsActionChecked::CreateSP( this, &SAssetView::IsShowingEngineContent )
			),
			EUserInterfaceActionType::ToggleButton
		);

		Section.AddMenuEntry(
			"ShowPluginFolder",
			LOCTEXT("ShowPluginFolderOption", "Show Plugin Content"),
			LOCTEXT("ShowPluginFolderOptionToolTip", "Show plugin content in the view?"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SAssetView::ToggleShowPluginContent ),
				FCanExecuteAction::CreateSP(this, &SAssetView::IsToggleShowPluginContentAllowed),
				FIsActionChecked::CreateSP( this, &SAssetView::IsShowingPluginContent )
			),
			EUserInterfaceActionType::ToggleButton
		);

		Section.AddMenuEntry(
			"ShowLocalizedContent",
			LOCTEXT("ShowLocalizedContentOption", "Show Localized Content"),
			LOCTEXT("ShowLocalizedContentOptionToolTip", "Show localized content in the view?"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SAssetView::ToggleShowLocalizedContent),
				FCanExecuteAction::CreateSP(this, &SAssetView::IsToggleShowLocalizedContentAllowed),
				FIsActionChecked::CreateSP(this, &SAssetView::IsShowingLocalizedContent)
				),
			EUserInterfaceActionType::ToggleButton
			);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("Search", LOCTEXT("SearchHeading", "Search"));
		Section.AddMenuEntry(
			"IncludeClassName",
			LOCTEXT("IncludeClassNameOption", "Search Asset Class Names"),
			LOCTEXT("IncludeClassesNameOptionTooltip", "Include asset type names in search criteria?  (e.g. Blueprint, Texture, Sound)"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SAssetView::ToggleIncludeClassNames),
				FCanExecuteAction::CreateSP(this, &SAssetView::IsToggleIncludeClassNamesAllowed),
				FIsActionChecked::CreateSP(this, &SAssetView::IsIncludingClassNames)
			),
			EUserInterfaceActionType::ToggleButton
		);

		Section.AddMenuEntry(
			"IncludeAssetPath",
			LOCTEXT("IncludeAssetPathOption", "Search Asset Path"),
			LOCTEXT("IncludeAssetPathOptionTooltip", "Include entire asset path in search criteria?"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SAssetView::ToggleIncludeAssetPaths),
				FCanExecuteAction::CreateSP(this, &SAssetView::IsToggleIncludeAssetPathsAllowed),
				FIsActionChecked::CreateSP(this, &SAssetView::IsIncludingAssetPaths)
			),
			EUserInterfaceActionType::ToggleButton
		);

		Section.AddMenuEntry(
			"IncludeCollectionName",
			LOCTEXT("IncludeCollectionNameOption", "Search Collection Names"),
			LOCTEXT("IncludeCollectionNameOptionTooltip", "Include Collection names in search criteria?"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SAssetView::ToggleIncludeCollectionNames),
				FCanExecuteAction::CreateSP(this, &SAssetView::IsToggleIncludeCollectionNamesAllowed),
				FIsActionChecked::CreateSP(this, &SAssetView::IsIncludingCollectionNames)
			),
			EUserInterfaceActionType::ToggleButton
		);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("AssetThumbnails", LOCTEXT("ThumbnailsHeading", "Thumbnails"));
		Section.AddEntry(FToolMenuEntry::InitWidget(
			"ThumbnailScale",
			SNew(SSlider)
				.ToolTipText( LOCTEXT("ThumbnailScaleToolTip", "Adjust the size of thumbnails.") )
				.Value( this, &SAssetView::GetThumbnailScale )
				.OnValueChanged( this, &SAssetView::SetThumbnailScale )
				.Locked( this, &SAssetView::IsThumbnailScalingLocked ),
			LOCTEXT("ThumbnailScaleLabel", "Scale"),
			/*bNoIndent=*/true
			));

		Section.AddMenuEntry(
			"ThumbnailEditMode",
			LOCTEXT("ThumbnailEditModeOption", "Thumbnail Edit Mode"),
			LOCTEXT("ThumbnailEditModeOptionToolTip", "Toggle thumbnail editing mode. When in this mode you can rotate the camera on 3D thumbnails by dragging them."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SAssetView::ToggleThumbnailEditMode ),
				FCanExecuteAction::CreateSP( this, &SAssetView::IsThumbnailEditModeAllowed ),
				FIsActionChecked::CreateSP( this, &SAssetView::IsThumbnailEditMode )
				),
			EUserInterfaceActionType::ToggleButton
			);

		Section.AddMenuEntry(
			"RealTimeThumbnails",
			LOCTEXT("RealTimeThumbnailsOption", "Real-Time Thumbnails"),
			LOCTEXT("RealTimeThumbnailsOptionToolTip", "Renders the assets thumbnails in real-time"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SAssetView::ToggleRealTimeThumbnails ),
				FCanExecuteAction::CreateSP( this, &SAssetView::CanShowRealTimeThumbnails ),
				FIsActionChecked::CreateSP( this, &SAssetView::IsShowingRealTimeThumbnails )
			),
			EUserInterfaceActionType::ToggleButton
			);
	}

	if (GetColumnViewVisibility() == EVisibility::Visible)
	{
		{
			FToolMenuSection& Section = Menu->AddSection("AssetColumns", LOCTEXT("ToggleColumnsHeading", "Columns"));
			Section.AddSubMenu(
				"ToggleColumns",
				LOCTEXT("ToggleColumnsMenu", "Toggle columns"),
				LOCTEXT("ToggleColumnsMenuTooltip", "Show or hide specific columns."),
				FNewMenuDelegate::CreateSP(this, &SAssetView::FillToggleColumnsMenu),
				false,
				FSlateIcon(),
				false
				);

			Section.AddMenuEntry(
				"ResetColumns",
				LOCTEXT("ResetColumns", "Reset Columns"),
				LOCTEXT("ResetColumnsToolTip", "Reset all columns to be visible again."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SAssetView::ResetColumns)),
				EUserInterfaceActionType::Button
				);

			Section.AddMenuEntry(
				"ExportColumns",
				LOCTEXT("ExportColumns", "Export to CSV"),
				LOCTEXT("ExportColumnsToolTip", "Export column data to CSV."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SAssetView::ExportColumns)),
				EUserInterfaceActionType::Button
			);
		}
	}
}

void SAssetView::ToggleShowFolders()
{
	check( IsToggleShowFoldersAllowed() );
	GetMutableDefault<UContentBrowserSettings>()->DisplayFolders = !GetDefault<UContentBrowserSettings>()->DisplayFolders;
	GetMutableDefault<UContentBrowserSettings>()->PostEditChange();
}

bool SAssetView::IsToggleShowFoldersAllowed() const
{
	return bCanShowFolders;
}

bool SAssetView::IsShowingFolders() const
{
	return IsToggleShowFoldersAllowed() && GetDefault<UContentBrowserSettings>()->DisplayFolders;
}

void SAssetView::ToggleShowEmptyFolders()
{
	check( IsToggleShowEmptyFoldersAllowed() );
	GetMutableDefault<UContentBrowserSettings>()->DisplayEmptyFolders = !GetDefault<UContentBrowserSettings>()->DisplayEmptyFolders;
	GetMutableDefault<UContentBrowserSettings>()->PostEditChange();
}

bool SAssetView::IsToggleShowEmptyFoldersAllowed() const
{
	return bCanShowFolders;
}

bool SAssetView::IsShowingEmptyFolders() const
{
	return IsToggleShowEmptyFoldersAllowed() && GetDefault<UContentBrowserSettings>()->DisplayEmptyFolders;
}

void SAssetView::ToggleRealTimeThumbnails()
{
	check( CanShowRealTimeThumbnails() );
	GetMutableDefault<UContentBrowserSettings>()->RealTimeThumbnails = !GetDefault<UContentBrowserSettings>()->RealTimeThumbnails;
	GetMutableDefault<UContentBrowserSettings>()->PostEditChange();
}

bool SAssetView::CanShowRealTimeThumbnails() const
{
	return bCanShowRealTimeThumbnails;
}

bool SAssetView::IsShowingRealTimeThumbnails() const
{
	return CanShowRealTimeThumbnails() && GetDefault<UContentBrowserSettings>()->RealTimeThumbnails;
}

void SAssetView::ToggleShowPluginContent()
{
	bool bDisplayPlugins = GetDefault<UContentBrowserSettings>()->GetDisplayPluginFolders();
	bool bRawDisplayPlugins = GetDefault<UContentBrowserSettings>()->GetDisplayPluginFolders( true );

	// Only if both these flags are false when toggling we want to enable the flag, otherwise we're toggling off
	if ( !bDisplayPlugins && !bRawDisplayPlugins )
	{
		GetMutableDefault<UContentBrowserSettings>()->SetDisplayPluginFolders( true );
	}
	else
	{
		GetMutableDefault<UContentBrowserSettings>()->SetDisplayPluginFolders( false );
		GetMutableDefault<UContentBrowserSettings>()->SetDisplayPluginFolders( false, true );
	}	
	GetMutableDefault<UContentBrowserSettings>()->PostEditChange();
}

bool SAssetView::IsShowingPluginContent() const
{
	return bForceShowPluginContent || GetDefault<UContentBrowserSettings>()->GetDisplayPluginFolders();
}

void SAssetView::ToggleShowEngineContent()
{
	bool bDisplayEngine = GetDefault<UContentBrowserSettings>()->GetDisplayEngineFolder();
	bool bRawDisplayEngine = GetDefault<UContentBrowserSettings>()->GetDisplayEngineFolder( true );

	// Only if both these flags are false when toggling we want to enable the flag, otherwise we're toggling off
	if ( !bDisplayEngine && !bRawDisplayEngine )
	{
		GetMutableDefault<UContentBrowserSettings>()->SetDisplayEngineFolder( true );
	}
	else
	{
		GetMutableDefault<UContentBrowserSettings>()->SetDisplayEngineFolder( false );
		GetMutableDefault<UContentBrowserSettings>()->SetDisplayEngineFolder( false, true );
	}	
	GetMutableDefault<UContentBrowserSettings>()->PostEditChange();
}

bool SAssetView::IsShowingEngineContent() const
{
	return bForceShowEngineContent || GetDefault<UContentBrowserSettings>()->GetDisplayEngineFolder();
}

void SAssetView::ToggleShowDevelopersContent()
{
	bool bDisplayDev = GetDefault<UContentBrowserSettings>()->GetDisplayDevelopersFolder();
	bool bRawDisplayDev = GetDefault<UContentBrowserSettings>()->GetDisplayDevelopersFolder( true );

	// Only if both these flags are false when toggling we want to enable the flag, otherwise we're toggling off
	if ( !bDisplayDev && !bRawDisplayDev )
	{
		GetMutableDefault<UContentBrowserSettings>()->SetDisplayDevelopersFolder( true );
	}
	else
	{
		GetMutableDefault<UContentBrowserSettings>()->SetDisplayDevelopersFolder( false );
		GetMutableDefault<UContentBrowserSettings>()->SetDisplayDevelopersFolder( false, true );
	}	
	GetMutableDefault<UContentBrowserSettings>()->PostEditChange();
}

bool SAssetView::IsToggleShowDevelopersContentAllowed() const
{
	return bCanShowDevelopersFolder;
}

bool SAssetView::IsToggleShowEngineContentAllowed() const
{
	return !bForceShowEngineContent;
}

bool SAssetView::IsToggleShowPluginContentAllowed() const
{
	return !bForceShowPluginContent;
}

bool SAssetView::IsShowingDevelopersContent() const
{
	return IsToggleShowDevelopersContentAllowed() && GetDefault<UContentBrowserSettings>()->GetDisplayDevelopersFolder();
}

void SAssetView::ToggleShowLocalizedContent()
{
	GetMutableDefault<UContentBrowserSettings>()->SetDisplayL10NFolder(!GetDefault<UContentBrowserSettings>()->GetDisplayL10NFolder());
	GetMutableDefault<UContentBrowserSettings>()->PostEditChange();
}

bool SAssetView::IsToggleShowLocalizedContentAllowed() const
{
	return true;
}

bool SAssetView::IsShowingLocalizedContent() const
{
	return IsToggleShowLocalizedContentAllowed() && GetDefault<UContentBrowserSettings>()->GetDisplayL10NFolder();
}

void SAssetView::ToggleShowFavorites()
{
	const bool bShowingFavorites = GetDefault<UContentBrowserSettings>()->GetDisplayFavorites();
	GetMutableDefault<UContentBrowserSettings>()->SetDisplayFavorites(!bShowingFavorites);
	GetMutableDefault<UContentBrowserSettings>()->PostEditChange();
}

bool SAssetView::IsToggleShowFavoritesAllowed() const
{
	return bCanShowFavorites;
}

bool SAssetView::IsShowingFavorites() const
{
	return IsToggleShowFavoritesAllowed() && GetDefault<UContentBrowserSettings>()->GetDisplayFavorites();
}

void SAssetView::ToggleDockCollections()
{
	const bool bDockCollections = GetDefault<UContentBrowserSettings>()->GetDockCollections();
	GetMutableDefault<UContentBrowserSettings>()->SetDockCollections(!bDockCollections);
	GetMutableDefault<UContentBrowserSettings>()->PostEditChange();
}

bool SAssetView::IsToggleDockCollectionsAllowed() const
{
	return bCanDockCollections;
}

bool SAssetView::HasDockedCollections() const
{
	return IsToggleDockCollectionsAllowed() && GetDefault<UContentBrowserSettings>()->GetDockCollections();
}

void SAssetView::ToggleShowCppContent()
{
	const bool bDisplayCppFolders = GetDefault<UContentBrowserSettings>()->GetDisplayCppFolders();
	GetMutableDefault<UContentBrowserSettings>()->SetDisplayCppFolders(!bDisplayCppFolders);
	GetMutableDefault<UContentBrowserSettings>()->PostEditChange();
}

bool SAssetView::IsToggleShowCppContentAllowed() const
{
	return bCanShowClasses;
}

bool SAssetView::IsShowingCppContent() const
{
	return IsToggleShowCppContentAllowed() && GetDefault<UContentBrowserSettings>()->GetDisplayCppFolders();
}

void SAssetView::ToggleIncludeClassNames()
{
	const bool bIncludeClassNames = GetDefault<UContentBrowserSettings>()->GetIncludeClassNames();
	GetMutableDefault<UContentBrowserSettings>()->SetIncludeClassNames(!bIncludeClassNames);
	GetMutableDefault<UContentBrowserSettings>()->PostEditChange();

	OnSearchOptionsChanged.ExecuteIfBound();
}

bool SAssetView::IsToggleIncludeClassNamesAllowed() const
{
	return true;
}

bool SAssetView::IsIncludingClassNames() const
{
	return IsToggleIncludeClassNamesAllowed() && GetDefault<UContentBrowserSettings>()->GetIncludeClassNames();
}

void SAssetView::ToggleIncludeAssetPaths()
{
	const bool bIncludeAssetPaths = GetDefault<UContentBrowserSettings>()->GetIncludeAssetPaths();
	GetMutableDefault<UContentBrowserSettings>()->SetIncludeAssetPaths(!bIncludeAssetPaths);
	GetMutableDefault<UContentBrowserSettings>()->PostEditChange();

	OnSearchOptionsChanged.ExecuteIfBound();
}

bool SAssetView::IsToggleIncludeAssetPathsAllowed() const
{
	return true;
}

bool SAssetView::IsIncludingAssetPaths() const
{
	return IsToggleIncludeAssetPathsAllowed() && GetDefault<UContentBrowserSettings>()->GetIncludeAssetPaths();
}

void SAssetView::ToggleIncludeCollectionNames()
{
	const bool bIncludeCollectionNames = GetDefault<UContentBrowserSettings>()->GetIncludeCollectionNames();
	GetMutableDefault<UContentBrowserSettings>()->SetIncludeCollectionNames(!bIncludeCollectionNames);
	GetMutableDefault<UContentBrowserSettings>()->PostEditChange();

	OnSearchOptionsChanged.ExecuteIfBound();
}

bool SAssetView::IsToggleIncludeCollectionNamesAllowed() const
{
	return true;
}

bool SAssetView::IsIncludingCollectionNames() const
{
	return IsToggleIncludeCollectionNamesAllowed() && GetDefault<UContentBrowserSettings>()->GetIncludeCollectionNames();
}


void SAssetView::SetCurrentViewType(EAssetViewType::Type NewType)
{
	if ( ensure(NewType != EAssetViewType::MAX) && NewType != CurrentViewType )
	{
		ResetQuickJump();

		CurrentViewType = NewType;
		CreateCurrentView();

		SyncToSelection();

		// Clear relevant thumbnails to render fresh ones in the new view if needed
		RelevantThumbnails.Reset();
		VisibleItems.Reset();

		if ( NewType == EAssetViewType::Tile )
		{
			CurrentThumbnailSize = TileViewThumbnailSize;
			bPendingUpdateThumbnails = true;
		}
		else if ( NewType == EAssetViewType::List )
		{
			CurrentThumbnailSize = ListViewThumbnailSize;
			bPendingUpdateThumbnails = true;
		}
		else if ( NewType == EAssetViewType::Column )
		{
			// No thumbnails, but we do need to refresh filtered items to determine a majority asset type
			MajorityAssetType = NAME_None;
			RefreshFilteredItems();
			SortList();
		}
	}
}

void SAssetView::SetCurrentViewTypeFromMenu(EAssetViewType::Type NewType)
{
	if (NewType != CurrentViewType)
	{
		SetCurrentViewType(NewType);
		FSlateApplication::Get().DismissAllMenus();
	}
}

void SAssetView::CreateCurrentView()
{
	TileView.Reset();
	ListView.Reset();
	ColumnView.Reset();

	TSharedRef<SWidget> NewView = SNullWidget::NullWidget;
	switch (CurrentViewType)
	{
		case EAssetViewType::Tile:
			TileView = CreateTileView();
			NewView = CreateShadowOverlay(TileView.ToSharedRef());
			break;
		case EAssetViewType::List:
			ListView = CreateListView();
			NewView = CreateShadowOverlay(ListView.ToSharedRef());
			break;
		case EAssetViewType::Column:
			ColumnView = CreateColumnView();
			NewView = CreateShadowOverlay(ColumnView.ToSharedRef());
			break;
	}
	
	ViewContainer->SetContent( NewView );
}

TSharedRef<SWidget> SAssetView::CreateShadowOverlay( TSharedRef<STableViewBase> Table )
{
	return SNew(SScrollBorder, Table)
		[
			Table
		];
}

EAssetViewType::Type SAssetView::GetCurrentViewType() const
{
	return CurrentViewType;
}

bool SAssetView::IsCurrentViewType(EAssetViewType::Type ViewType) const
{
	return GetCurrentViewType() == ViewType;
}

void SAssetView::FocusList() const
{
	switch ( GetCurrentViewType() )
	{
		case EAssetViewType::List: FSlateApplication::Get().SetKeyboardFocus(ListView, EFocusCause::SetDirectly); break;
		case EAssetViewType::Tile: FSlateApplication::Get().SetKeyboardFocus(TileView, EFocusCause::SetDirectly); break;
		case EAssetViewType::Column: FSlateApplication::Get().SetKeyboardFocus(ColumnView, EFocusCause::SetDirectly); break;
	}
}

void SAssetView::RefreshList()
{
	switch ( GetCurrentViewType() )
	{
		case EAssetViewType::List: ListView->RequestListRefresh(); break;
		case EAssetViewType::Tile: TileView->RequestListRefresh(); break;
		case EAssetViewType::Column: ColumnView->RequestListRefresh(); break;
	}
}

void SAssetView::SetSelection(const TSharedPtr<FAssetViewItem>& Item)
{
	switch ( GetCurrentViewType() )
	{
		case EAssetViewType::List: ListView->SetSelection(Item); break;
		case EAssetViewType::Tile: TileView->SetSelection(Item); break;
		case EAssetViewType::Column: ColumnView->SetSelection(Item); break;
	}
}

void SAssetView::SetItemSelection(const TSharedPtr<FAssetViewItem>& Item, bool bSelected, const ESelectInfo::Type SelectInfo)
{
	switch ( GetCurrentViewType() )
	{
		case EAssetViewType::List: ListView->SetItemSelection(Item, bSelected, SelectInfo); break;
		case EAssetViewType::Tile: TileView->SetItemSelection(Item, bSelected, SelectInfo); break;
		case EAssetViewType::Column: ColumnView->SetItemSelection(Item, bSelected, SelectInfo); break;
	}
}

void SAssetView::RequestScrollIntoView(const TSharedPtr<FAssetViewItem>& Item)
{
	switch ( GetCurrentViewType() )
	{
		case EAssetViewType::List: ListView->RequestScrollIntoView(Item); break;
		case EAssetViewType::Tile: TileView->RequestScrollIntoView(Item); break;
		case EAssetViewType::Column: ColumnView->RequestScrollIntoView(Item); break;
	}
}

void SAssetView::OnOpenAssetsOrFolders()
{
	if (OnItemsActivated.IsBound())
	{
		const TArray<FContentBrowserItem> SelectedItems = GetSelectedItems();
		OnItemsActivated.Execute(SelectedItems, EAssetTypeActivationMethod::Opened);
	}
}

void SAssetView::OnPreviewAssets()
{
	if (OnItemsActivated.IsBound())
	{
		const TArray<FContentBrowserItem> SelectedItems = GetSelectedItems();
		OnItemsActivated.Execute(SelectedItems, EAssetTypeActivationMethod::Previewed);
	}
}

void SAssetView::ClearSelection(bool bForceSilent)
{
	const bool bTempBulkSelectingValue = bForceSilent ? true : bBulkSelecting;
	TGuardValue<bool> Guard(bBulkSelecting, bTempBulkSelectingValue);
	switch ( GetCurrentViewType() )
	{
		case EAssetViewType::List: ListView->ClearSelection(); break;
		case EAssetViewType::Tile: TileView->ClearSelection(); break;
		case EAssetViewType::Column: ColumnView->ClearSelection(); break;
	}
}

TSharedRef<ITableRow> SAssetView::MakeListViewWidget(TSharedPtr<FAssetViewItem> AssetItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	if ( !ensure(AssetItem.IsValid()) )
	{
		return SNew( STableRow<TSharedPtr<FAssetViewItem>>, OwnerTable );
	}

	VisibleItems.Add(AssetItem);
	bPendingUpdateThumbnails = true;

	if (AssetItem->IsFolder())
	{
		TSharedPtr< STableRow<TSharedPtr<FAssetViewItem>> > TableRowWidget;
		SAssignNew( TableRowWidget, STableRow<TSharedPtr<FAssetViewItem>>, OwnerTable )
			.Style(FEditorStyle::Get(), "ContentBrowser.AssetListView.TableRow")
			.Cursor( bAllowDragging ? EMouseCursor::GrabHand : EMouseCursor::Default )
			.OnDragDetected( this, &SAssetView::OnDraggingAssetItem );

		TSharedRef<SAssetListItem> Item =
			SNew(SAssetListItem)
			.AssetItem(AssetItem)
			.ItemHeight(this, &SAssetView::GetListViewItemHeight)
			.OnRenameBegin(this, &SAssetView::AssetRenameBegin)
			.OnRenameCommit(this, &SAssetView::AssetRenameCommit)
			.OnVerifyRenameCommit(this, &SAssetView::AssetVerifyRenameCommit)
			.OnItemDestroyed(this, &SAssetView::AssetItemWidgetDestroyed)
			.ShouldAllowToolTip(this, &SAssetView::ShouldAllowToolTips)
			.HighlightText(HighlightedText)
			.IsSelected( FIsSelected::CreateSP(TableRowWidget.Get(), &STableRow<TSharedPtr<FAssetViewItem>>::IsSelectedExclusively) );

		TableRowWidget->SetContent(Item);

		return TableRowWidget.ToSharedRef();
	}
	else
	{
		TSharedPtr<FAssetThumbnail>& AssetThumbnail = RelevantThumbnails.FindOrAdd(AssetItem);
		if (!AssetThumbnail)
		{
			const float ThumbnailResolution = ListViewThumbnailResolution;
			AssetThumbnail = MakeShared<FAssetThumbnail>(FAssetData(), ThumbnailResolution, ThumbnailResolution, AssetThumbnailPool);
			AssetItem->GetItem().UpdateThumbnail(*AssetThumbnail);
			AssetThumbnail->GetViewportRenderTargetTexture(); // Access the texture once to trigger it to render
		}

		TSharedPtr< STableRow<TSharedPtr<FAssetViewItem>> > TableRowWidget;
		SAssignNew( TableRowWidget, STableRow<TSharedPtr<FAssetViewItem>>, OwnerTable )
		.Style(FEditorStyle::Get(), "ContentBrowser.AssetListView.TableRow")
		.Cursor( bAllowDragging ? EMouseCursor::GrabHand : EMouseCursor::Default )
		.OnDragDetected( this, &SAssetView::OnDraggingAssetItem );

		TSharedRef<SAssetListItem> Item =
			SNew(SAssetListItem)
			.AssetThumbnail(AssetThumbnail)
			.AssetItem(AssetItem)
			.ThumbnailPadding(ListViewThumbnailPadding)
			.ItemHeight(this, &SAssetView::GetListViewItemHeight)
			.OnRenameBegin(this, &SAssetView::AssetRenameBegin)
			.OnRenameCommit(this, &SAssetView::AssetRenameCommit)
			.OnVerifyRenameCommit(this, &SAssetView::AssetVerifyRenameCommit)
			.OnItemDestroyed(this, &SAssetView::AssetItemWidgetDestroyed)
			.ShouldAllowToolTip(this, &SAssetView::ShouldAllowToolTips)
			.HighlightText(HighlightedText)
			.ThumbnailEditMode(this, &SAssetView::IsThumbnailEditMode)
			.ThumbnailLabel( ThumbnailLabel )
			.ThumbnailHintColorAndOpacity( this, &SAssetView::GetThumbnailHintColorAndOpacity )
			.AllowThumbnailHintLabel( AllowThumbnailHintLabel )
			.IsSelected( FIsSelected::CreateSP(TableRowWidget.Get(), &STableRow<TSharedPtr<FAssetViewItem>>::IsSelectedExclusively) )
			.OnIsAssetValidForCustomToolTip(OnIsAssetValidForCustomToolTip)
			.OnGetCustomAssetToolTip(OnGetCustomAssetToolTip)
			.OnVisualizeAssetToolTip(OnVisualizeAssetToolTip)
			.OnAssetToolTipClosing(OnAssetToolTipClosing);

		TableRowWidget->SetContent(Item);

		return TableRowWidget.ToSharedRef();
	}
}

TSharedRef<ITableRow> SAssetView::MakeTileViewWidget(TSharedPtr<FAssetViewItem> AssetItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	if ( !ensure(AssetItem.IsValid()) )
	{
		return SNew( STableRow<TSharedPtr<FAssetViewItem>>, OwnerTable );
	}

	VisibleItems.Add(AssetItem);
	bPendingUpdateThumbnails = true;

	if (AssetItem->IsFolder())
	{
		TSharedPtr< STableRow<TSharedPtr<FAssetViewItem>> > TableRowWidget;
		SAssignNew( TableRowWidget, STableRow<TSharedPtr<FAssetViewItem>>, OwnerTable )
			.Style( FEditorStyle::Get(), "ContentBrowser.AssetListView.TableRow" )
			.Cursor( bAllowDragging ? EMouseCursor::GrabHand : EMouseCursor::Default )
			.OnDragDetected( this, &SAssetView::OnDraggingAssetItem );

		TSharedRef<SAssetTileItem> Item =
			SNew(SAssetTileItem)
			.AssetItem(AssetItem)
			.ItemWidth(this, &SAssetView::GetTileViewItemWidth)
			.OnRenameBegin(this, &SAssetView::AssetRenameBegin)
			.OnRenameCommit(this, &SAssetView::AssetRenameCommit)
			.OnVerifyRenameCommit(this, &SAssetView::AssetVerifyRenameCommit)
			.OnItemDestroyed(this, &SAssetView::AssetItemWidgetDestroyed)
			.ShouldAllowToolTip(this, &SAssetView::ShouldAllowToolTips)
			.HighlightText( HighlightedText )
			.IsSelected( FIsSelected::CreateSP(TableRowWidget.Get(), &STableRow<TSharedPtr<FAssetViewItem>>::IsSelectedExclusively) );

		TableRowWidget->SetContent(Item);

		return TableRowWidget.ToSharedRef();
	}
	else
	{
		TSharedPtr<FAssetThumbnail>& AssetThumbnail = RelevantThumbnails.FindOrAdd(AssetItem);
		if (!AssetThumbnail)
		{
			const float ThumbnailResolution = TileViewThumbnailResolution;
			AssetThumbnail = MakeShared<FAssetThumbnail>(FAssetData(), ThumbnailResolution, ThumbnailResolution, AssetThumbnailPool);
			AssetItem->GetItem().UpdateThumbnail(*AssetThumbnail);
			AssetThumbnail->GetViewportRenderTargetTexture(); // Access the texture once to trigger it to render
		}

		TSharedPtr< STableRow<TSharedPtr<FAssetViewItem>> > TableRowWidget;
		SAssignNew( TableRowWidget, STableRow<TSharedPtr<FAssetViewItem>>, OwnerTable )
		.Style(FEditorStyle::Get(), "ContentBrowser.AssetListView.TableRow")
		.Cursor( bAllowDragging ? EMouseCursor::GrabHand : EMouseCursor::Default )
		.OnDragDetected( this, &SAssetView::OnDraggingAssetItem );

		TSharedRef<SAssetTileItem> Item =
			SNew(SAssetTileItem)
			.AssetThumbnail(AssetThumbnail)
			.AssetItem(AssetItem)
			.ThumbnailPadding(TileViewThumbnailPadding)
			.ItemWidth(this, &SAssetView::GetTileViewItemWidth)
			.OnRenameBegin(this, &SAssetView::AssetRenameBegin)
			.OnRenameCommit(this, &SAssetView::AssetRenameCommit)
			.OnVerifyRenameCommit(this, &SAssetView::AssetVerifyRenameCommit)
			.OnItemDestroyed(this, &SAssetView::AssetItemWidgetDestroyed)
			.ShouldAllowToolTip(this, &SAssetView::ShouldAllowToolTips)
			.HighlightText( HighlightedText )
			.ThumbnailEditMode(this, &SAssetView::IsThumbnailEditMode)
			.ThumbnailLabel( ThumbnailLabel )
			.ThumbnailHintColorAndOpacity( this, &SAssetView::GetThumbnailHintColorAndOpacity )
			.AllowThumbnailHintLabel( AllowThumbnailHintLabel )
			.IsSelected( FIsSelected::CreateSP(TableRowWidget.Get(), &STableRow<TSharedPtr<FAssetViewItem>>::IsSelectedExclusively) )
			.OnIsAssetValidForCustomToolTip(OnIsAssetValidForCustomToolTip)
			.OnGetCustomAssetToolTip(OnGetCustomAssetToolTip)
			.OnVisualizeAssetToolTip( OnVisualizeAssetToolTip )
			.OnAssetToolTipClosing( OnAssetToolTipClosing );

		TableRowWidget->SetContent(Item);

		return TableRowWidget.ToSharedRef();
	}
}

TSharedRef<ITableRow> SAssetView::MakeColumnViewWidget(TSharedPtr<FAssetViewItem> AssetItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	if ( !ensure(AssetItem.IsValid()) )
	{
		return SNew( STableRow<TSharedPtr<FAssetViewItem>>, OwnerTable )
			.Style(FEditorStyle::Get(), "ContentBrowser.AssetListView.TableRow");
	}

	// Update the cached custom data
	AssetItem->CacheCustomColumns(CustomColumns, false, true, false);
	
	return
		SNew( SAssetColumnViewRow, OwnerTable )
		.OnDragDetected( this, &SAssetView::OnDraggingAssetItem )
		.Cursor( bAllowDragging ? EMouseCursor::GrabHand : EMouseCursor::Default )
		.AssetColumnItem(
			SNew(SAssetColumnItem)
				.AssetItem(AssetItem)
				.OnRenameBegin(this, &SAssetView::AssetRenameBegin)
				.OnRenameCommit(this, &SAssetView::AssetRenameCommit)
				.OnVerifyRenameCommit(this, &SAssetView::AssetVerifyRenameCommit)
				.OnItemDestroyed(this, &SAssetView::AssetItemWidgetDestroyed)
				.HighlightText( HighlightedText )
				.OnIsAssetValidForCustomToolTip(OnIsAssetValidForCustomToolTip)
				.OnGetCustomAssetToolTip(OnGetCustomAssetToolTip)
				.OnVisualizeAssetToolTip( OnVisualizeAssetToolTip )
				.OnAssetToolTipClosing( OnAssetToolTipClosing )
		);
}

void SAssetView::AssetItemWidgetDestroyed(const TSharedPtr<FAssetViewItem>& Item)
{
	if(RenamingAsset.Pin().Get() == Item.Get())
	{
		/* Check if the item is in a temp state and if it is, commit using the default name so that it does not entirely vanish on the user.
		   This keeps the functionality consistent for content to never be in a temporary state */

		if (Item && Item->IsTemporary())
		{
			if (Item->IsFile())
			{
				FText OutErrorText;
				EndCreateDeferredItem(Item, Item->GetItem().GetItemName().ToString(), /*bFinalize*/true, OutErrorText);
			}
			else
			{
				DeferredItemToCreate.Reset();
			}
		}

		RenamingAsset.Reset();
	}

	if ( VisibleItems.Remove(Item) != INDEX_NONE )
	{
		bPendingUpdateThumbnails = true;
	}
}

void SAssetView::UpdateThumbnails()
{
	int32 MinItemIdx = INDEX_NONE;
	int32 MaxItemIdx = INDEX_NONE;
	int32 MinVisibleItemIdx = INDEX_NONE;
	int32 MaxVisibleItemIdx = INDEX_NONE;

	const int32 HalfNumOffscreenThumbnails = NumOffscreenThumbnails * 0.5;
	for ( auto ItemIt = VisibleItems.CreateConstIterator(); ItemIt; ++ItemIt )
	{
		int32 ItemIdx = FilteredAssetItems.Find(*ItemIt);
		if ( ItemIdx != INDEX_NONE )
		{
			const int32 ItemIdxLow = FMath::Max<int32>(0, ItemIdx - HalfNumOffscreenThumbnails);
			const int32 ItemIdxHigh = FMath::Min<int32>(FilteredAssetItems.Num() - 1, ItemIdx + HalfNumOffscreenThumbnails);
			if ( MinItemIdx == INDEX_NONE || ItemIdxLow < MinItemIdx )
			{
				MinItemIdx = ItemIdxLow;
			}
			if ( MaxItemIdx == INDEX_NONE || ItemIdxHigh > MaxItemIdx )
			{
				MaxItemIdx = ItemIdxHigh;
			}
			if ( MinVisibleItemIdx == INDEX_NONE || ItemIdx < MinVisibleItemIdx )
			{
				MinVisibleItemIdx = ItemIdx;
			}
			if ( MaxVisibleItemIdx == INDEX_NONE || ItemIdx > MaxVisibleItemIdx )
			{
				MaxVisibleItemIdx = ItemIdx;
			}
		}
	}

	if ( MinItemIdx != INDEX_NONE && MaxItemIdx != INDEX_NONE && MinVisibleItemIdx != INDEX_NONE && MaxVisibleItemIdx != INDEX_NONE )
	{
		// We have a new min and a new max, compare it to the old min and max so we can create new thumbnails
		// when appropriate and remove old thumbnails that are far away from the view area.
		TMap< TSharedPtr<FAssetViewItem>, TSharedPtr<FAssetThumbnail> > NewRelevantThumbnails;

		// Operate on offscreen items that are furthest away from the visible items first since the thumbnail pool processes render requests in a LIFO order.
		while (MinItemIdx < MinVisibleItemIdx || MaxItemIdx > MaxVisibleItemIdx)
		{
			const int32 LowEndDistance = MinVisibleItemIdx - MinItemIdx;
			const int32 HighEndDistance = MaxItemIdx - MaxVisibleItemIdx;

			if ( HighEndDistance > LowEndDistance )
			{
				if(FilteredAssetItems.IsValidIndex(MaxItemIdx) && FilteredAssetItems[MaxItemIdx]->IsFile())
				{
					AddItemToNewThumbnailRelevancyMap(FilteredAssetItems[MaxItemIdx], NewRelevantThumbnails);
				}
				MaxItemIdx--;
			}
			else
			{
				if(FilteredAssetItems.IsValidIndex(MinItemIdx) && FilteredAssetItems[MinItemIdx]->IsFile())
				{
					AddItemToNewThumbnailRelevancyMap(FilteredAssetItems[MinItemIdx], NewRelevantThumbnails);
				}
				MinItemIdx++;
			}
		}

		// Now operate on VISIBLE items then prioritize them so they are rendered first
		TArray< TSharedPtr<FAssetThumbnail> > ThumbnailsToPrioritize;
		for ( int32 ItemIdx = MinVisibleItemIdx; ItemIdx <= MaxVisibleItemIdx; ++ItemIdx )
		{
			if(FilteredAssetItems.IsValidIndex(ItemIdx) && FilteredAssetItems[ItemIdx]->IsFile())
			{
				TSharedPtr<FAssetThumbnail> Thumbnail = AddItemToNewThumbnailRelevancyMap( FilteredAssetItems[ItemIdx], NewRelevantThumbnails );
				if ( Thumbnail.IsValid() )
				{
					ThumbnailsToPrioritize.Add(Thumbnail);
				}
			}
		}

		// Now prioritize all thumbnails there were in the visible range
		if ( ThumbnailsToPrioritize.Num() > 0 )
		{
			AssetThumbnailPool->PrioritizeThumbnails(ThumbnailsToPrioritize, CurrentThumbnailSize, CurrentThumbnailSize);
		}

		// Assign the new map of relevant thumbnails. This will remove any entries that were no longer relevant.
		RelevantThumbnails = NewRelevantThumbnails;
	}
}

TSharedPtr<FAssetThumbnail> SAssetView::AddItemToNewThumbnailRelevancyMap(const TSharedPtr<FAssetViewItem>& Item, TMap< TSharedPtr<FAssetViewItem>, TSharedPtr<FAssetThumbnail> >& NewRelevantThumbnails)
{
	checkf(Item->IsFile(), TEXT("Only files can have thumbnails!"));

	TSharedPtr<FAssetThumbnail> Thumbnail = RelevantThumbnails.FindRef(Item);
	if (!Thumbnail)
	{
		if (!ensure(CurrentThumbnailSize > 0 && CurrentThumbnailSize <= MAX_THUMBNAIL_SIZE))
		{
			// Thumbnail size must be in a sane range
			CurrentThumbnailSize = 64;
		}

		// The thumbnail newly relevant, create a new thumbnail
		const float ThumbnailResolution = CurrentThumbnailSize * MaxThumbnailScale;
		Thumbnail = MakeShared<FAssetThumbnail>(FAssetData(), ThumbnailResolution, ThumbnailResolution, AssetThumbnailPool);
		Item->GetItem().UpdateThumbnail(*Thumbnail);
		Thumbnail->GetViewportRenderTargetTexture(); // Access the texture once to trigger it to render
	}

	if (Thumbnail)
	{
		NewRelevantThumbnails.Add(Item, Thumbnail);
	}

	return Thumbnail;
}

void SAssetView::AssetSelectionChanged( TSharedPtr< FAssetViewItem > AssetItem, ESelectInfo::Type SelectInfo )
{
	if (!bBulkSelecting)
	{
		if (AssetItem)
		{
			OnItemSelectionChanged.ExecuteIfBound(AssetItem->GetItem(), SelectInfo);
		}
		else
		{
			OnItemSelectionChanged.ExecuteIfBound(FContentBrowserItem(), SelectInfo);
		}
	}
}

void SAssetView::ItemScrolledIntoView(TSharedPtr<FAssetViewItem> AssetItem, const TSharedPtr<ITableRow>& Widget )
{
	if (AssetItem->ShouldRenameWhenScrolledIntoView())
	{
		// Make sure we have window focus to avoid the inline text editor from canceling itself if we try to click on it
		// This can happen if creating an asset opens an intermediary window which steals our focus, 
		// eg, the blueprint and slate widget style class windows (TTP# 314240)
		TSharedPtr<SWindow> OwnerWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
		if (OwnerWindow.IsValid())
		{
			OwnerWindow->BringToFront();
		}

		AwaitingRename = AssetItem;
	}
}

TSharedPtr<SWidget> SAssetView::OnGetContextMenuContent()
{
	if (CanOpenContextMenu())
	{
		if (IsRenamingAsset())
		{
			RenamingAsset.Pin()->OnRenameCanceled().ExecuteIfBound();
			RenamingAsset.Reset();
		}

		const TArray<FContentBrowserItem> SelectedItems = GetSelectedItems();
		return OnGetItemContextMenu.Execute(SelectedItems);
	}

	return nullptr;
}

bool SAssetView::CanOpenContextMenu() const
{
	if (!OnGetItemContextMenu.IsBound())
	{
		// You can only a summon a context menu if one is set up
		return false;
	}

	if (IsThumbnailEditMode())
	{
		// You can not summon a context menu for assets when in thumbnail edit mode because right clicking may happen inadvertently while adjusting thumbnails.
		return false;
	}

	TArray<TSharedPtr<FAssetViewItem>> SelectedItems = GetSelectedViewItems();

	// Detect if at least one temporary item was selected. If there is only a temporary item selected, then deny the context menu.
	int32 NumTemporaryItemsSelected = 0;
	int32 NumCollectionFoldersSelected = 0;
	for (const TSharedPtr<FAssetViewItem>& Item : SelectedItems)
	{
		if (Item->IsTemporary())
		{
			++NumTemporaryItemsSelected;
		}

		if (Item->IsFolder() && EnumHasAnyFlags(Item->GetItem().GetItemCategory(), EContentBrowserItemFlags::Category_Collection))
		{
			++NumCollectionFoldersSelected;
		}
	}

	// If there are only a temporary items selected, deny the context menu
	if (SelectedItems.Num() > 0 && SelectedItems.Num() == NumTemporaryItemsSelected)
	{
		return false;
	}

	// If there are any collection folders selected, deny the context menu
	if (NumCollectionFoldersSelected > 0)
	{
		return false;
	}

	if (bPreloadAssetsForContextMenu)
	{
		// Build a list of selected object paths
		TArray<FString> ObjectPaths;
		for (const TSharedPtr<FAssetViewItem>& Item : SelectedItems)
		{
			FAssetData ItemAssetData;
			if (Item->GetItem().Legacy_TryGetAssetData(ItemAssetData))
			{
				ObjectPaths.Add(ItemAssetData.ObjectPath.ToString());
			}
		}

		TArray<UObject*> LoadedObjects;
		if (ObjectPaths.Num() > 0 && !ContentBrowserUtils::LoadAssetsIfNeeded(ObjectPaths, LoadedObjects, /*bAllowedToPrompt*/false))
		{
			// Do not show the context menu if the load failed
			return false;
		}
	}

	return true;
}

void SAssetView::OnListMouseButtonDoubleClick(TSharedPtr<FAssetViewItem> AssetItem)
{
	if ( !ensure(AssetItem.IsValid()) )
	{
		return;
	}

	if ( IsThumbnailEditMode() )
	{
		// You can not activate assets when in thumbnail edit mode because double clicking may happen inadvertently while adjusting thumbnails.
		return;
	}

	if ( AssetItem->IsTemporary() )
	{
		// You may not activate temporary items, they are just for display.
		return;
	}

	if (OnItemsActivated.IsBound())
	{
		OnItemsActivated.Execute(MakeArrayView(&AssetItem->GetItem(), 1), EAssetTypeActivationMethod::DoubleClicked);
	}
}

FReply SAssetView::OnDraggingAssetItem( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if (bAllowDragging)
	{
		// Use the custom drag handler?
		if (FEditorDelegates::OnAssetDragStarted.IsBound())
		{
			TArray<FAssetData> SelectedAssets = GetSelectedAssets();
			SelectedAssets.RemoveAll([](const FAssetData& InAssetData)
			{
				return InAssetData.IsRedirector();
			});

			if (SelectedAssets.Num() > 0)
			{
				FEditorDelegates::OnAssetDragStarted.Broadcast(SelectedAssets, nullptr);
				return FReply::Handled();
			}
		}

		// Use the standard drag handler?
		if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
		{
			TArray<FContentBrowserItem> SelectedItems = GetSelectedItems();
			SelectedItems.RemoveAll([](const FContentBrowserItem& InItem)
			{
				return InItem.IsFolder() && EnumHasAnyFlags(InItem.GetItemCategory(), EContentBrowserItemFlags::Category_Collection);
			});

			if (TSharedPtr<FDragDropOperation> DragDropOp = DragDropHandler::CreateDragOperation(SelectedItems))
			{
				return FReply::Handled().BeginDragDrop(DragDropOp.ToSharedRef());
			}
		}
	}

	return FReply::Unhandled();
}

bool SAssetView::AssetVerifyRenameCommit(const TSharedPtr<FAssetViewItem>& Item, const FText& NewName, const FSlateRect& MessageAnchor, FText& OutErrorMessage)
{
	const FString& NewItemName = NewName.ToString();

	if (DeferredItemToCreate.IsValid() && DeferredItemToCreate->bWasAddedToView)
	{
		checkf(FContentBrowserItemKey(Item->GetItem()) == FContentBrowserItemKey(DeferredItemToCreate->ItemContext.GetItem()), TEXT("DeferredItemToCreate was still set when attempting to rename a different item!"));

		return DeferredItemToCreate->ItemContext.ValidateItem(NewItemName, &OutErrorMessage);
	}
	else if (!Item->GetItem().GetItemName().ToString().Equals(NewItemName))
	{
		return Item->GetItem().CanRename(&NewItemName, &OutErrorMessage);
	}

	return true;
}

void SAssetView::AssetRenameBegin(const TSharedPtr<FAssetViewItem>& Item, const FString& NewName, const FSlateRect& MessageAnchor)
{
	check(!RenamingAsset.IsValid());
	RenamingAsset = Item;
}

void SAssetView::AssetRenameCommit(const TSharedPtr<FAssetViewItem>& Item, const FString& NewName, const FSlateRect& MessageAnchor, const ETextCommit::Type CommitType)
{
	bool bSuccess = false;
	FText ErrorMessage;
	TSharedPtr<FAssetViewItem> UpdatedItem;

	UE_LOG(LogContentBrowser, Log, TEXT("Attempting asset rename: %s -> %s"), *Item->GetItem().GetItemName().ToString(), *NewName);

	if (DeferredItemToCreate.IsValid() && DeferredItemToCreate->bWasAddedToView)
	{
		const bool bFinalize = CommitType != ETextCommit::OnCleared; // Clearing the rename box on a newly created item cancels the entire creation process

		FContentBrowserItem NewItem = EndCreateDeferredItem(Item, NewName, bFinalize, ErrorMessage);
		if (NewItem.IsValid())
		{
			bSuccess = true;

			// Add result to view
			UpdatedItem = AvailableBackendItems.Add(FContentBrowserItemKey(NewItem), MakeShared<FAssetViewItem>(NewItem));
			FilteredAssetItems.Add(UpdatedItem);
		}
	}
	else if (CommitType != ETextCommit::OnCleared && !Item->GetItem().GetItemName().ToString().Equals(NewName))
	{
		FContentBrowserItem NewItem;
		if (Item->GetItem().CanRename(&NewName, &ErrorMessage) && Item->GetItem().Rename(NewName, &NewItem))
		{
			bSuccess = true;

			// Add result to view (the old item will be removed via the notifications, as not all data sources may have been able to perform the rename)
			UpdatedItem = AvailableBackendItems.Add(FContentBrowserItemKey(NewItem), MakeShared<FAssetViewItem>(NewItem));
			FilteredAssetItems.Add(UpdatedItem);
		}
	}
	
	if (bSuccess)
	{
		if (UpdatedItem)
		{
			// Sort in the new item
			bPendingSortFilteredItems = true;

			if (UpdatedItem->IsFile())
			{
				// Refresh the thumbnail
				if (TSharedPtr<FAssetThumbnail> AssetThumbnail = RelevantThumbnails.FindRef(Item))
				{
					if (UpdatedItem != Item)
					{
						// This item was newly created - move the thumbnail over from the temporary item
						RelevantThumbnails.Remove(Item);
						RelevantThumbnails.Add(UpdatedItem, AssetThumbnail);
						UpdatedItem->GetItem().UpdateThumbnail(*AssetThumbnail);
					}
					if (AssetThumbnail->GetAssetData().IsValid())
					{
						AssetThumbnailPool->RefreshThumbnail(AssetThumbnail);
					}
				}
			}
			
			// Sync the view
			{
				TArray<FContentBrowserItem> ItemsToSync;
				ItemsToSync.Add(UpdatedItem->GetItem());

				if (OnItemRenameCommitted.IsBound() && !bUserSearching)
				{
					// If our parent wants to potentially handle the sync, let it, but only if we're not currently searching (or it would cancel the search)
					OnItemRenameCommitted.Execute(ItemsToSync);
				}
				else
				{
					// Otherwise, sync just the view
					SyncToItems(ItemsToSync);
				}
			}
		}
	}
	else if (!ErrorMessage.IsEmpty())
	{
		// Prompt the user with the reason the rename/creation failed
		ContentBrowserUtils::DisplayMessage(ErrorMessage, MessageAnchor, SharedThis(this));
	}

	RenamingAsset.Reset();
}

bool SAssetView::IsRenamingAsset() const
{
	return RenamingAsset.IsValid();
}

bool SAssetView::ShouldAllowToolTips() const
{
	bool bIsRightClickScrolling = false;
	switch( CurrentViewType )
	{
		case EAssetViewType::List:
			bIsRightClickScrolling = ListView->IsRightClickScrolling();
			break;

		case EAssetViewType::Tile:
			bIsRightClickScrolling = TileView->IsRightClickScrolling();
			break;

		case EAssetViewType::Column:
			bIsRightClickScrolling = ColumnView->IsRightClickScrolling();
			break;

		default:
			bIsRightClickScrolling = false;
			break;
	}

	return !bIsRightClickScrolling && !IsThumbnailEditMode() && !IsRenamingAsset();
}

bool SAssetView::IsThumbnailEditMode() const
{
	return IsThumbnailEditModeAllowed() && bThumbnailEditMode;
}

bool SAssetView::IsThumbnailEditModeAllowed() const
{
	return bAllowThumbnailEditMode && GetCurrentViewType() != EAssetViewType::Column;
}

FReply SAssetView::EndThumbnailEditModeClicked()
{
	bThumbnailEditMode = false;

	return FReply::Handled();
}

FText SAssetView::GetAssetCountText() const
{
	const int32 NumAssets = FilteredAssetItems.Num();
	const int32 NumSelectedAssets = GetSelectedViewItems().Num();

	FText AssetCount = FText::GetEmpty();
	if ( NumSelectedAssets == 0 )
	{
		if ( NumAssets == 1 )
		{
			AssetCount = LOCTEXT("AssetCountLabelSingular", "1 item");
		}
		else
		{
			AssetCount = FText::Format( LOCTEXT("AssetCountLabelPlural", "{0} items"), FText::AsNumber(NumAssets) );
		}
	}
	else
	{
		if ( NumAssets == 1 )
		{
			AssetCount = FText::Format( LOCTEXT("AssetCountLabelSingularPlusSelection", "1 item ({0} selected)"), FText::AsNumber(NumSelectedAssets) );
		}
		else
		{
			AssetCount = FText::Format( LOCTEXT("AssetCountLabelPluralPlusSelection", "{0} items ({1} selected)"), FText::AsNumber(NumAssets), FText::AsNumber(NumSelectedAssets) );
		}
	}

	return AssetCount;
}

EVisibility SAssetView::GetEditModeLabelVisibility() const
{
	return IsThumbnailEditMode() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SAssetView::GetListViewVisibility() const
{
	return GetCurrentViewType() == EAssetViewType::List ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SAssetView::GetTileViewVisibility() const
{
	return GetCurrentViewType() == EAssetViewType::Tile ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SAssetView::GetColumnViewVisibility() const
{
	return GetCurrentViewType() == EAssetViewType::Column ? EVisibility::Visible : EVisibility::Collapsed;
}

void SAssetView::ToggleThumbnailEditMode()
{
	bThumbnailEditMode = !bThumbnailEditMode;
}

float SAssetView::GetThumbnailScale() const
{
	return ThumbnailScaleSliderValue.Get();
}

void SAssetView::SetThumbnailScale( float NewValue )
{
	ThumbnailScaleSliderValue = NewValue;
	RefreshList();
}

bool SAssetView::IsThumbnailScalingLocked() const
{
	return GetCurrentViewType() == EAssetViewType::Column;
}

float SAssetView::GetListViewItemHeight() const
{
	return (ListViewThumbnailSize + ListViewThumbnailPadding * 2) * FMath::Lerp(MinThumbnailScale, MaxThumbnailScale, GetThumbnailScale());
}

float SAssetView::GetTileViewItemHeight() const
{
	return TileViewNameHeight + GetTileViewItemBaseHeight() * FillScale;
}

float SAssetView::GetTileViewItemBaseHeight() const
{
	return (TileViewThumbnailSize + TileViewThumbnailPadding * 2) * FMath::Lerp(MinThumbnailScale, MaxThumbnailScale, GetThumbnailScale());
}

float SAssetView::GetTileViewItemWidth() const
{
	return GetTileViewItemBaseWidth() * FillScale;
}

float SAssetView::GetTileViewItemBaseWidth() const //-V524
{
	return ( TileViewThumbnailSize + TileViewThumbnailPadding * 2 ) * FMath::Lerp( MinThumbnailScale, MaxThumbnailScale, GetThumbnailScale() );
}

EColumnSortMode::Type SAssetView::GetColumnSortMode(const FName ColumnId) const
{
	for (int32 PriorityIdx = 0; PriorityIdx < EColumnSortPriority::Max; PriorityIdx++)
	{
		const EColumnSortPriority::Type SortPriority = static_cast<EColumnSortPriority::Type>(PriorityIdx);
		if (ColumnId == SortManager.GetSortColumnId(SortPriority))
		{
			return SortManager.GetSortMode(SortPriority);
		}
	}
	return EColumnSortMode::None;
}

EColumnSortPriority::Type SAssetView::GetColumnSortPriority(const FName ColumnId) const
{
	for (int32 PriorityIdx = 0; PriorityIdx < EColumnSortPriority::Max; PriorityIdx++)
	{
		const EColumnSortPriority::Type SortPriority = static_cast<EColumnSortPriority::Type>(PriorityIdx);
		if (ColumnId == SortManager.GetSortColumnId(SortPriority))
		{
			return SortPriority;
		}
	}
	return EColumnSortPriority::Primary;
}

void SAssetView::OnSortColumnHeader(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type NewSortMode)
{
	SortManager.SetSortColumnId(SortPriority, ColumnId);
	SortManager.SetSortMode(SortPriority, NewSortMode);
	SortList();
}

EVisibility SAssetView::IsAssetShowWarningTextVisible() const
{
	return (FilteredAssetItems.Num() > 0 || bQuickFrontendListRefreshRequested) ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
}

FText SAssetView::GetAssetShowWarningText() const
{
	if (AssetShowWarningText.IsSet())
	{
		return AssetShowWarningText.Get();
	}
	
	FText NothingToShowText, DropText;
	if (ShouldFilterRecursively())
	{
		NothingToShowText = LOCTEXT( "NothingToShowCheckFilter", "No results, check your filter." );
	}

	if ( SourcesData.HasCollections() && !SourcesData.IsDynamicCollection() )
	{
		if (SourcesData.Collections[0].Name.IsNone())
		{
			DropText = LOCTEXT("NoCollectionSelected", "No collection selected.");
		}
		else
		{
			DropText = LOCTEXT("DragAssetsHere", "Drag and drop assets here to add them to the collection.");
		}
	}
	else if ( OnGetItemContextMenu.IsBound() )
	{
		DropText = LOCTEXT( "DropFilesOrRightClick", "Drop files here or right click to create content." );
	}
	
	return NothingToShowText.IsEmpty() ? DropText : FText::Format(LOCTEXT("NothingToShowPattern", "{0}\n\n{1}"), NothingToShowText, DropText);
}

bool SAssetView::HasSingleCollectionSource() const
{
	return ( SourcesData.Collections.Num() == 1 && SourcesData.VirtualPaths.Num() == 0 );
}

void SAssetView::SetUserSearching(bool bInSearching)
{
	if(bUserSearching != bInSearching)
	{
		RequestSlowFullListRefresh();
	}
	bUserSearching = bInSearching;
}

void SAssetView::HandleSettingChanged(FName PropertyName)
{
	if ((PropertyName == GET_MEMBER_NAME_CHECKED(UContentBrowserSettings, DisplayFolders)) ||
		(PropertyName == GET_MEMBER_NAME_CHECKED(UContentBrowserSettings, DisplayEmptyFolders)) ||
		(PropertyName == "DisplayDevelopersFolder") ||
		(PropertyName == "DisplayEngineFolder") ||
		(PropertyName == NAME_None))	// @todo: Needed if PostEditChange was called manually, for now
	{
		RequestSlowFullListRefresh();
	}
}

FText SAssetView::GetQuickJumpTerm() const
{
	return FText::FromString(QuickJumpData.JumpTerm);
}

EVisibility SAssetView::IsQuickJumpVisible() const
{
	return (QuickJumpData.JumpTerm.IsEmpty()) ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
}

FSlateColor SAssetView::GetQuickJumpColor() const
{
	return FEditorStyle::GetColor((QuickJumpData.bHasValidMatch) ? "InfoReporting.BackgroundColor" : "ErrorReporting.BackgroundColor");
}

void SAssetView::ResetQuickJump()
{
	QuickJumpData.JumpTerm.Empty();
	QuickJumpData.bIsJumping = false;
	QuickJumpData.bHasChangedSinceLastTick = false;
	QuickJumpData.bHasValidMatch = false;
}

FReply SAssetView::HandleQuickJumpKeyDown(const TCHAR InCharacter, const bool bIsControlDown, const bool bIsAltDown, const bool bTestOnly)
{
	// Check for special characters
	if(bIsControlDown || bIsAltDown)
	{
		return FReply::Unhandled();
	}

	// Check for invalid characters
	for(int InvalidCharIndex = 0; InvalidCharIndex < UE_ARRAY_COUNT(INVALID_OBJECTNAME_CHARACTERS) - 1; ++InvalidCharIndex)
	{
		if(InCharacter == INVALID_OBJECTNAME_CHARACTERS[InvalidCharIndex])
		{
			return FReply::Unhandled();
		}
	}

	switch(InCharacter)
	{
	// Ignore some other special characters that we don't want to be entered into the buffer
	case 0:		// Any non-character key press, e.g. f1-f12, Delete, Pause/Break, etc.
				// These should be explicitly not handled so that their input bindings are handled higher up the chain.

	case 8:		// Backspace
	case 13:	// Enter
	case 27:	// Esc
		return FReply::Unhandled();

	default:
		break;
	}

	// Any other character!
	if(!bTestOnly)
	{
		QuickJumpData.JumpTerm.AppendChar(InCharacter);
		QuickJumpData.bHasChangedSinceLastTick = true;
	}

	return FReply::Handled();
}

bool SAssetView::PerformQuickJump(const bool bWasJumping)
{
	auto JumpToNextMatch = [this](const int StartIndex, const int EndIndex) -> bool
	{
		check(StartIndex >= 0);
		check(EndIndex <= FilteredAssetItems.Num());

		for(int NewSelectedItemIndex = StartIndex; NewSelectedItemIndex < EndIndex; ++NewSelectedItemIndex)
		{
			TSharedPtr<FAssetViewItem>& NewSelectedItem = FilteredAssetItems[NewSelectedItemIndex];
			const FString& NewSelectedItemName = NewSelectedItem->GetItem().GetDisplayName().ToString();
			if(NewSelectedItemName.StartsWith(QuickJumpData.JumpTerm, ESearchCase::IgnoreCase))
			{
				SetSelection(NewSelectedItem);
				RequestScrollIntoView(NewSelectedItem);
				return true;
			}
		}

		return false;
	};

	TArray<TSharedPtr<FAssetViewItem>> SelectedItems = GetSelectedViewItems();
	TSharedPtr<FAssetViewItem> SelectedItem = (SelectedItems.Num()) ? SelectedItems[0] : nullptr;

	// If we have a selection, and we were already jumping, first check to see whether 
	// the current selection still matches the quick-jump term; if it does, we do nothing
	if(bWasJumping && SelectedItem.IsValid())
	{
		const FString& SelectedItemName = SelectedItem->GetItem().GetDisplayName().ToString();
		if(SelectedItemName.StartsWith(QuickJumpData.JumpTerm, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	// We need to move on to the next match in FilteredAssetItems that starts with the given quick-jump term
	const int SelectedItemIndex = (SelectedItem.IsValid()) ? FilteredAssetItems.Find(SelectedItem) : INDEX_NONE;
	const int StartIndex = (SelectedItemIndex == INDEX_NONE) ? 0 : SelectedItemIndex + 1;
	
	bool ValidMatch = JumpToNextMatch(StartIndex, FilteredAssetItems.Num());
	if(!ValidMatch && StartIndex > 0)
	{
		// If we didn't find a match, we need to loop around and look again from the start (assuming we weren't already)
		return JumpToNextMatch(0, StartIndex);
	}

	return ValidMatch;
}

void SAssetView::FillToggleColumnsMenu(FMenuBuilder& MenuBuilder)
{
	// Column view may not be valid if we toggled off columns view while the columns menu was open
	if(ColumnView.IsValid())
	{
		const TIndirectArray<SHeaderRow::FColumn> Columns = ColumnView->GetHeaderRow()->GetColumns();

		for (int32 ColumnIndex = 0; ColumnIndex < Columns.Num(); ++ColumnIndex)
		{
			const FString ColumnName = Columns[ColumnIndex].ColumnId.ToString();

			MenuBuilder.AddMenuEntry(
				Columns[ColumnIndex].DefaultText,
				LOCTEXT("ShowHideColumnTooltip", "Show or hide column"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &SAssetView::ToggleColumn, ColumnName),
					FCanExecuteAction::CreateSP(this, &SAssetView::CanToggleColumn, ColumnName),
					FIsActionChecked::CreateSP(this, &SAssetView::IsColumnVisible, ColumnName),
					EUIActionRepeatMode::RepeatEnabled
				),
				NAME_None,
				EUserInterfaceActionType::Check
			);
		}
	}
}

void SAssetView::ResetColumns()
{
	HiddenColumnNames.Empty();
	NumVisibleColumns = ColumnView->GetHeaderRow()->GetColumns().Num();
	ColumnView->GetHeaderRow()->RefreshColumns();
	ColumnView->RebuildList();
}

void SAssetView::ExportColumns()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

	const void* ParentWindowWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

	const FText Title = LOCTEXT("ExportToCSV", "Export columns as CSV...");
	const FString FileTypes = TEXT("Data Table CSV (*.csv)|*.csv");

	TArray<FString> OutFilenames;
	DesktopPlatform->SaveFileDialog(
		ParentWindowWindowHandle,
		Title.ToString(),
		TEXT(""),
		TEXT("Report.csv"),
		FileTypes,
		EFileDialogFlags::None,
		OutFilenames
	);

	if (OutFilenames.Num() > 0)
	{
		const TIndirectArray<SHeaderRow::FColumn>& Columns = ColumnView->GetHeaderRow()->GetColumns();

		TArray<FName> ColumnNames;
		for (const SHeaderRow::FColumn& Column : Columns)
		{
			ColumnNames.Add(Column.ColumnId);
		}

		FString SaveString;
		SortManager.ExportColumnsToCSV(FilteredAssetItems, ColumnNames, CustomColumns, SaveString);

		FFileHelper::SaveStringToFile(SaveString, *OutFilenames[0]);
	}
}

void SAssetView::ToggleColumn(const FString ColumnName)
{
	SetColumnVisibility(ColumnName, HiddenColumnNames.Contains(ColumnName));
}

void SAssetView::SetColumnVisibility(const FString ColumnName, const bool bShow)
{
	if (!bShow)
	{
		--NumVisibleColumns;
		HiddenColumnNames.Add(ColumnName);
	}
	else
	{
		++NumVisibleColumns;
		check(HiddenColumnNames.Contains(ColumnName));
		HiddenColumnNames.Remove(ColumnName);
	}

	ColumnView->GetHeaderRow()->RefreshColumns();
	ColumnView->RebuildList();
}

bool SAssetView::CanToggleColumn(const FString ColumnName) const
{
	return (HiddenColumnNames.Contains(ColumnName) || NumVisibleColumns > 1);
}

bool SAssetView::IsColumnVisible(const FString ColumnName) const
{
	return !HiddenColumnNames.Contains(ColumnName);
}

bool SAssetView::ShouldColumnGenerateWidget(const FString ColumnName) const
{
	return !HiddenColumnNames.Contains(ColumnName);
}

TSharedRef<SWidget> SAssetView::CreateRowHeaderMenuContent(const FString ColumnName)
{
	FMenuBuilder MenuBuilder(true, NULL);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("HideColumn", "Hide Column"),
		LOCTEXT("HideColumnToolTip", "Hides this column."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SAssetView::SetColumnVisibility, ColumnName, false), FCanExecuteAction::CreateSP(this, &SAssetView::CanToggleColumn, ColumnName)),
		NAME_None,
		EUserInterfaceActionType::Button);

	return MenuBuilder.MakeWidget();
}

void SAssetView::ForceShowPluginFolder(bool bEnginePlugin)
{
	if (bEnginePlugin && !IsShowingEngineContent())
	{
		ToggleShowEngineContent();
	}

	if (!IsShowingPluginContent())
	{
		ToggleShowPluginContent();
	}
}

void SAssetView::HandleItemDataUpdated(TArrayView<const FContentBrowserItemDataUpdate> InUpdatedItems)
{
	if (InUpdatedItems.Num() == 0)
	{
		return;
	}

	const double HandleItemDataUpdatedStartTime = FPlatformTime::Seconds();

	UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();

	TArray<FContentBrowserDataCompiledFilter> CompiledDataFilters;
	{
		const FContentBrowserDataFilter DataFilter = CreateBackendDataFilter();

		static const FName RootPath = "/";
		const TArrayView<const FName> DataSourcePaths = SourcesData.HasVirtualPaths() ? MakeArrayView(SourcesData.VirtualPaths) : MakeArrayView(&RootPath, 1);
		for (const FName& DataSourcePath : DataSourcePaths)
		{
			FContentBrowserDataCompiledFilter& CompiledDataFilter = CompiledDataFilters.AddDefaulted_GetRef();
			ContentBrowserData->CompileFilter(DataSourcePath, DataFilter, CompiledDataFilter);
		}
	}

	bool bRefreshView = false;
	TSet<TSharedPtr<FAssetViewItem>> ItemsPendingInplaceFrontendFilter;

	auto AddItem = [this, &ItemsPendingInplaceFrontendFilter](const FContentBrowserItemKey& InItemDataKey, const FContentBrowserItemData& InItemData)
	{
		TSharedPtr<FAssetViewItem>& ItemToUpdate = AvailableBackendItems.FindOrAdd(InItemDataKey);
		if (ItemToUpdate)
		{
			// Update the item
			ItemToUpdate->AppendItemData(InItemData);

			// Update the custom column data
			ItemToUpdate->CacheCustomColumns(CustomColumns, true, true, true);

			// This item was modified, so put it in the list of items to be in-place re-tested against the active frontend filter (this can avoid a costly re-sort of the view)
			// If the item can't be queried in-place (because the item isn't in the view) then it will be added to ItemsPendingPriorityFilter instead
			ItemsPendingInplaceFrontendFilter.Add(ItemToUpdate);
		}
		else
		{
			ItemToUpdate = MakeShared<FAssetViewItem>(InItemData);

			// This item is new so put it in the pending set to be processed over time
			ItemsPendingFrontendFilter.Add(ItemToUpdate);
		}
	};

	auto RemoveItem = [this, &bRefreshView, &ItemsPendingInplaceFrontendFilter](const FContentBrowserItemKey& InItemDataKey, const FContentBrowserItemData& InItemData)
	{
		const uint32 ItemDataKeyHash = GetTypeHash(InItemDataKey);

		if (const TSharedPtr<FAssetViewItem>* ItemToRemovePtr = AvailableBackendItems.FindByHash(ItemDataKeyHash, InItemDataKey))
		{
			TSharedPtr<FAssetViewItem> ItemToRemove = *ItemToRemovePtr;
			check(ItemToRemove);

			// Only fully remove this item if every sub-item is removed (items become invalid when empty)
			ItemToRemove->RemoveItemData(InItemData);
			if (ItemToRemove->GetItem().IsValid())
			{
				return;
			}

			AvailableBackendItems.RemoveByHash(ItemDataKeyHash, InItemDataKey);

			const uint32 ItemToRemoveHash = GetTypeHash(ItemToRemove);

			// Also ensure this item has been removed from the pending filter lists and the current list view data
			FilteredAssetItems.RemoveSingle(ItemToRemove);
			ItemsPendingPriorityFilter.RemoveByHash(ItemToRemoveHash, ItemToRemove);
			ItemsPendingFrontendFilter.RemoveByHash(ItemToRemoveHash, ItemToRemove);
			ItemsPendingInplaceFrontendFilter.RemoveByHash(ItemToRemoveHash, ItemToRemove);

			// Need to refresh manually after removing items, as adding relies on the pending filter lists to trigger this
			bRefreshView = true;
		}
	};

	auto DoesItemPassBackendFilter = [this, &CompiledDataFilters](const FContentBrowserItemData& InItemData)
	{
		UContentBrowserDataSource* ItemDataSource = InItemData.GetOwnerDataSource();
		for (const FContentBrowserDataCompiledFilter& DataFilter : CompiledDataFilters)
		{
			if (ItemDataSource->DoesItemPassFilter(InItemData, DataFilter))
			{
				return true;
			}
		}
		return false;
	};

	// Process the main set of updates
	for (const FContentBrowserItemDataUpdate& ItemDataUpdate : InUpdatedItems)
	{
		const FContentBrowserItemData& ItemData = ItemDataUpdate.GetItemData();
		const FContentBrowserItemKey ItemDataKey(ItemData);

		switch (ItemDataUpdate.GetUpdateType())
		{
		case EContentBrowserItemUpdateType::Added:
		case EContentBrowserItemUpdateType::Modified:
			if (DoesItemPassBackendFilter(ItemData))
			{
				AddItem(ItemDataKey, ItemData);
			}
			else
			{
				RemoveItem(ItemDataKey, ItemData);
			}
			break;

		case EContentBrowserItemUpdateType::Moved:
			{
				const FContentBrowserItemData OldMinimalItemData(ItemData.GetOwnerDataSource(), ItemData.GetItemType(), ItemDataUpdate.GetPreviousVirtualPath(), NAME_None, FText(), nullptr);
				const FContentBrowserItemKey OldItemDataKey(OldMinimalItemData);
				RemoveItem(OldItemDataKey, OldMinimalItemData);

				if (DoesItemPassBackendFilter(ItemData))
				{
					AddItem(ItemDataKey, ItemData);
				}
				else
				{
					checkAssetList(!AvailableBackendItems.Contains(ItemDataKey));
				}
			}
			break;

		case EContentBrowserItemUpdateType::Removed:
			RemoveItem(ItemDataKey, ItemData);
			break;

		default:
			checkf(false, TEXT("Unexpected EContentBrowserItemUpdateType!"));
			break;
		}
	}

	// Now patch in the in-place frontend filter requests (if possible)
	if (ItemsPendingInplaceFrontendFilter.Num() > 0)
	{
		FAssetViewFrontendFilterHelper FrontendFilterHelper(this);
		const bool bRunQueryFilter = OnShouldFilterAsset.IsBound();

		for (auto It = FilteredAssetItems.CreateIterator(); It && ItemsPendingInplaceFrontendFilter.Num() > 0; ++It)
		{
			const TSharedPtr<FAssetViewItem> ItemToFilter = *It;

			if (ItemsPendingInplaceFrontendFilter.Remove(ItemToFilter) > 0)
			{
				bool bRemoveItem = false;

				// Run the query filter if required
				if (bRunQueryFilter)
				{
					const bool bPassedBackendFilter = FrontendFilterHelper.DoesItemPassQueryFilter(ItemToFilter);
					if (!bPassedBackendFilter)
					{
						bRemoveItem = true;
						AvailableBackendItems.Remove(FContentBrowserItemKey(ItemToFilter->GetItem()));
					}
				}

				// Run the frontend filter
				if (!bRemoveItem)
				{
					const bool bPassedFrontendFilter = FrontendFilterHelper.DoesItemPassFrontendFilter(ItemToFilter);
					if (!bPassedFrontendFilter)
					{
						bRemoveItem = true;
					}
				}

				// Remove this item?
				if (bRemoveItem)
				{
					bRefreshView = true;
					It.RemoveCurrent();
				}
			}
		}

		// Do we still have items that could not be in-place filtered?
		// If so, add them to ItemsPendingPriorityFilter so they are processed into the view ASAP
		if (ItemsPendingInplaceFrontendFilter.Num() > 0)
		{
			ItemsPendingPriorityFilter.Append(MoveTemp(ItemsPendingInplaceFrontendFilter));
			ItemsPendingInplaceFrontendFilter.Reset();
		}
	}

	if (bRefreshView)
	{
		RefreshList();
	}

	UE_LOG(LogContentBrowser, VeryVerbose, TEXT("AssetView - HandleItemDataUpdated completed in %0.4f seconds for %d items (%d available items)"), FPlatformTime::Seconds() - HandleItemDataUpdatedStartTime, InUpdatedItems.Num(), AvailableBackendItems.Num());
}

void SAssetView::HandleItemDataDiscoveryComplete()
{
	if (bPendingSortFilteredItems)
	{
		// If we have a sort pending, then force this to happen next frame now that discovery has finished
		LastSortTime = 0;
	}
}

#undef checkAssetList
#undef ASSET_VIEW_PARANOIA_LIST_CHECKS

#undef LOCTEXT_NAMESPACE
