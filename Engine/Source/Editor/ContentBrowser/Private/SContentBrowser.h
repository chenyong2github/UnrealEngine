// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "AssetData.h"
#include "SAssetSearchBox.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "ContentBrowserDataMenuContexts.h"
#include "CollectionManagerTypes.h"
#include "IContentBrowserSingleton.h"
#include "Editor/ContentBrowser/Private/HistoryManager.h"

class FAssetContextMenu;
class FFrontendFilter_Text;
class FSourcesSearch;
class FPathContextMenu;
class FTabManager;
class FUICommandList;
class SAssetView;
class SCollectionView;
class SComboButton;
class SFilterList;
class SPathView;
class SSplitter;
class UFactory;
class UToolMenu;

struct FToolMenuContext;

enum class EContentBrowserViewContext : uint8
{
	AssetView,
	PathView,
	FavoriteView,
};

/**
 * A widget to display and work with all game and engine content
 */
class SContentBrowser
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS( SContentBrowser )
		: _ContainingTab()
		, _InitiallyLocked(false)
	{}
		/** The tab in which the content browser resides */
		SLATE_ARGUMENT( TSharedPtr<SDockTab>, ContainingTab )

		/** If true, this content browser will not sync from external sources. */
		SLATE_ARGUMENT( bool, InitiallyLocked )

	SLATE_END_ARGS()

	~SContentBrowser();

	/** Constructs this widget with InArgs */
	void Construct( const FArguments& InArgs, const FName& InstanceName, const FContentBrowserConfig* Config );

	/** Sets up an inline-name for the creation of a new asset using the specified path and the specified class and/or factory */
	void CreateNewAsset(const FString& DefaultAssetName, const FString& PackagePath, UClass* AssetClass, UFactory* Factory);

	/**
	 * Changes sources to show the specified assets and selects them in the asset view
	 *
	 *	@param AssetDataList		- A list of assets to sync the view to
	 * 
	 *	@param bAllowImplicitSync	- true to allow the view to sync to parent folders if they are already selected,
	 *								  false to force the view to select the explicit Parent folders of each asset 
	 */
	void SyncToAssets( TArrayView<const FAssetData> AssetDataList, const bool bAllowImplicitSync = false, const bool bDisableFiltersThatHideAssets = true );

	/**
	 * Changes sources to show the specified folders and selects them in the asset view
	 *
	 *	@param FolderList			- A list of folders to sync the view to
	 * 
	 *	@param bAllowImplicitSync	- true to allow the view to sync to parent folders if they are already selected,
	 *								  false to force the view to select the explicit Parent folders of each asset 
	 */
	void SyncToFolders( TArrayView<const FString> FolderList, const bool bAllowImplicitSync = false );

	/**
	 * Changes sources to show the specified items and selects them in the asset view
	 *
	 *	@param ItemsToSync			- A list of items to sync the view to
	 *
	 *	@param bAllowImplicitSync	- true to allow the view to sync to parent folders if they are already selected,
	 *								  false to force the view to select the explicit Parent folders of each asset
	 */
	void SyncToItems( TArrayView<const FContentBrowserItem> ItemsToSync, const bool bAllowImplicitSync = false, const bool bDisableFiltersThatHideAssets = true );

	/**
	 * Changes sources to show the specified items and selects them in the asset view
	 *
	 *	@param VirtualPathsToSync	- A list of virtual paths to sync the view to
	 *
	 *	@param bAllowImplicitSync	- true to allow the view to sync to parent folders if they are already selected,
	 *								  false to force the view to select the explicit Parent folders of each asset
	 */
	void SyncToVirtualPaths( TArrayView<const FName> VirtualPathsToSync, const bool bAllowImplicitSync = false, const bool bDisableFiltersThatHideAssets = true );

	/**
	 * Changes sources to show the specified assets and folders and selects them in the asset view
	 *
	 *	@param AssetDataList		- A list of assets to sync the view to
	 *	@param FolderList			- A list of folders to sync the view to
	 *
	 *	@param bAllowImplicitSync	- true to allow the view to sync to parent folders if they are already selected,
	 *								  false to force the view to select the explicit Parent folders of each asset
	 */
	void SyncToLegacy( TArrayView<const FAssetData> AssetDataList, TArrayView<const FString> FolderList, const bool bAllowImplicitSync = false, const bool bDisableFiltersThatHideAssets = true );

	/**
	 * Changes sources to show the specified items and selects them in the asset view
	 *
	 *	@param AssetDataList		- A list of assets to sync the view to
	 * 
	 *	@param bAllowImplicitSync	- true to allow the view to sync to parent folders if they are already selected,
	 *								  false to force the view to select the explicit Parent folders of each asset 
	 */
	void SyncTo( const FContentBrowserSelection& ItemSelection, const bool bAllowImplicitSync = false, const bool bDisableFiltersThatHideAssets = true );

	/** Sets this content browser as the primary browser. The primary browser is the target for asset syncs and contributes to the global selection set. */
	void SetIsPrimaryContentBrowser(bool NewIsPrimary);

	/** Returns if this browser can be used as the primary browser. */
	bool CanSetAsPrimaryContentBrowser() const;

	/** Gets the tab manager for the tab containing this browser */
	TSharedPtr<FTabManager> GetTabManager() const;

	/** Loads all selected assets if unloaded */
	void LoadSelectedObjectsIfNeeded();

	/** Returns all the assets that are selected in the asset view */
	void GetSelectedAssets(TArray<FAssetData>& SelectedAssets);

	/** Returns all the folders that are selected in the asset view */
	void GetSelectedFolders(TArray<FString>& SelectedFolders);

	/** Returns the folders that are selected in the path view */
	TArray<FString> GetSelectedPathViewFolders();

	/** Saves all persistent settings to config and returns a string identifier */
	void SaveSettings() const;

	/** Sets the content browser to show the specified paths */
	void SetSelectedPaths(const TArray<FString>& FolderPaths, bool bNeedsRefresh = false);

	/** Gets the current path if one exists, otherwise returns empty string. */
	FString GetCurrentPath() const;

	/**
	 * Forces the content browser to show plugin content
	 *
	 * @param bEnginePlugin		If true, the content browser will also be forced to show engine content
	 */
	void ForceShowPluginContent(bool bEnginePlugin);

	/** Get the unique name of this content browser's in */
	const FName GetInstanceName() const;

	/** Returns true if this content browser does not accept syncing from an external source */
	bool IsLocked() const;

	/** Gives keyboard focus to the asset search box */
	void SetKeyboardFocusOnSearch() const;

	virtual FReply OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent ) override;
	virtual FReply OnMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override;
	virtual FReply OnPreviewMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override;
	virtual FReply OnMouseButtonDoubleClick( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent ) override;

private:

	/** Called prior to syncing the selection in this Content Browser */
	void PrepareToSyncItems(TArrayView<const FContentBrowserItem> ItemsToSync, const bool bDisableFiltersThatHideAssets);

	/** Called prior to syncing the selection in this Content Browser */
	void PrepareToSyncVirtualPaths(TArrayView<const FName> VirtualPathsToSync, const bool bDisableFiltersThatHideAssets);

	/** Called prior to syncing the selection in this Content Browser */
	void PrepareToSyncLegacy(TArrayView<const FAssetData> AssetDataList, TArrayView<const FString> FolderPaths, const bool bDisableFiltersThatHideAssets);

	/** Called to retrieve the text that should be highlighted on assets */
	FText GetHighlightedText() const;

	/** Called when a containing tab is closing, if there is one */
	void OnContainingTabSavingVisualState() const;

	/** Called when a containing tab is closed, if there is one */
	void OnContainingTabClosed(TSharedRef<SDockTab> DockTab);

	/** Called when a containing tab is activated, if there is one */
	void OnContainingTabActivated(TSharedRef<SDockTab> DockTab, ETabActivationCause InActivationCause);

	/** Loads settings from config based on the browser's InstanceName*/
	void LoadSettings(const FName& InstanceName);

	/** Handler for when the sources were changed */
	void SourcesChanged(const TArray<FString>& SelectedPaths, const TArray<FCollectionNameType>& SelectedCollections);

	/** Handler for when a folder has been entered in the path view */
	void FolderEntered(const FString& FolderPath);

	/** Handler for when a path has been selected in the path view */
	void PathSelected(const FString& FolderPath);

	/** Handler for when a path has been selected in the favorite path view */
	void FavoritePathSelected(const FString& FolderPath);

	/** Get the asset tree context menu */
	TSharedRef<FExtender> GetPathContextMenuExtender(const TArray<FString>& SelectedPaths) const;

	/** Handler for when a collection has been selected in the collection view */
	void CollectionSelected(const FCollectionNameType& SelectedCollection);

	/** Handler for when the sources were changed by the path picker */
	void PathPickerPathSelected(const FString& FolderPath);

	/** Handler for when the sources were changed by the path picker collection view */
	void PathPickerCollectionSelected(const FCollectionNameType& SelectedCollection);

	/** Sets the state of the browser to the one described by the supplied history data */
	void OnApplyHistoryData(const FHistoryData& History);

	/** Updates the supplied history data with current information */
	void OnUpdateHistoryData(FHistoryData& History) const;

	/** Handler for when the path context menu requests a folder creation */
	void NewFolderRequested(const FString& SelectedPath);

	/** Handler for when a data source requests file item creation */
	void NewFileItemRequested(const FContentBrowserItemDataTemporaryContext& NewItemContext);

	/** Called when the editable text needs to be set or cleared */
	void SetSearchBoxText(const FText& InSearchText);

	/** Called by the editable text control when the search text is changed by the user */
	void OnSearchBoxChanged(const FText& InSearchText);

	/** Called by the editable text control when the user commits a text change */
	void OnSearchBoxCommitted(const FText& InSearchText, ETextCommit::Type CommitInfo);

	/** Should the "Save Search" button be enabled? */
	bool IsSaveSearchButtonEnabled() const;

	/** Open the menu to let you save the current search text as a dynamic collection */
	FReply OnSaveSearchButtonClicked();

	/** Called when a crumb in the path breadcrumb trail or menu is clicked */
	void OnPathClicked(const FString& CrumbData);

	/** Called when item in the path delimiter arrow menu is clicked */
	void OnPathMenuItemClicked(FString ClickedPath);

	/** Called to query whether the crumb menu would contain any content (see also OnGetCrumbDelimiterContent) */
	bool OnHasCrumbDelimiterContent(const FString& CrumbData) const;

	/** 
	 * Populates the delimiter arrow with a menu of directories under the current directory that can be navigated to
	 * 
	 * @param CrumbData	The current directory path
	 * @return The directory menu
	 */
	TSharedRef<SWidget> OnGetCrumbDelimiterContent(const FString& CrumbData) const;

	/** Gets the content for the path picker combo button */
	TSharedRef<SWidget> GetPathPickerContent();

	/** Register the context objects needed for the "Add New" menu */
	void AppendNewMenuContextObjects(const EContentBrowserDataMenuContext_AddNewMenuDomain InDomain, const TArray<FName>& InSelectedPaths, FToolMenuContext& InOutMenuContext);

	/** Handle creating a context menu for the "Add New" button */
	TSharedRef<SWidget> MakeAddNewContextMenu(const EContentBrowserDataMenuContext_AddNewMenuDomain InDomain);

	/** Handle populating a context menu for the "Add New" button */
	void PopulateAddNewContextMenu(class UToolMenu* Menu);

	/** Called to work out whether the import button should be enabled */
	bool IsAddNewEnabled() const;

	/** Gets the tool tip for the "Add New" button */
	FText GetAddNewToolTipText() const;

	/** Makes the filters menu */
	TSharedRef<SWidget> MakeAddFilterMenu();
	
	/** Builds the context menu for the filter list area. */
	TSharedPtr<SWidget> GetFilterContextMenu();

	/** Saves dirty content. */
	FReply OnSaveClicked();

	/** Opens the add content dialog. */
	void OnAddContentRequested();

	/** Handler for when a new item is requested in the asset view */
	void OnNewItemRequested(const FContentBrowserItem& NewItem);

	/** Handler for when the selection set in any view has changed. */
	void OnItemSelectionChanged(const FContentBrowserItem& SelectedItem, ESelectInfo::Type SelectInfo, EContentBrowserViewContext ViewContext);

	/** Handler for when the user double clicks, presses enter, or presses space on a Content Browser item */
	void OnItemsActivated(TArrayView<const FContentBrowserItem> ActivatedItems, EAssetTypeActivationMethod::Type ActivationMethod);

	/** Handler for clicking the lock button */
	FReply ToggleLockClicked();

	/** Gets the brush used on the lock button */
	const FSlateBrush* GetToggleLockImage() const;

	/** Gets the visibility state of the asset tree */
	EVisibility GetSourcesViewVisibility() const;

	/** Gets the brush used to on the sources toggle button */
	const FSlateBrush* GetSourcesToggleImage() const;

	/** Handler for clicking the tree expand/collapse button */
	FReply SourcesViewExpandClicked();

	/** Gets the visibility of the path expander button */
	EVisibility GetPathExpanderVisibility() const;

	/** Gets the visibility of the source switch button */
	EVisibility GetSourcesSwitcherVisibility() const;

	/** Gets the icon used on the source switch button */
	const FSlateBrush* GetSourcesSwitcherIcon() const;

	/** Gets the tooltip text used on the source switch button */
	FText GetSourcesSwitcherToolTipText() const;

	/** Handler for clicking the source switch button */
	FReply OnSourcesSwitcherClicked();

	/** Gets the source search hint text */
	FText GetSourcesSearchHintText() const;

	/** Called to handle the Content Browser settings changing */
	void OnContentBrowserSettingsChanged(FName PropertyName);

	/** Handler for clicking the history back button */
	FReply BackClicked();

	/** Handler for clicking the history forward button */
	FReply ForwardClicked();

	/** Handler to check to see if a rename command is allowed */
	bool HandleRenameCommandCanExecute() const;

	/** Handler for Rename */
	void HandleRenameCommand();

	/** Handler to check to see if a save asset command is allowed */
	bool HandleSaveAssetCommandCanExecute() const;

	/** Handler for Rename */
	void HandleSaveAssetCommand();

	/** Handler for SaveAll in folder */
	void HandleSaveAllCurrentFolderCommand() const;

	/** Handler for Resave on a folder */
	void HandleResaveAllCurrentFolderCommand() const;

	/** Handler to check to see if a delete command is allowed */
	bool HandleDeleteCommandCanExecute() const;

	/** Handler for Delete */
	void HandleDeleteCommandExecute();

	/** Handler for opening assets or folders */
	void HandleOpenAssetsOrFoldersCommandExecute();

	/** Handler for previewing assets */
	void HandlePreviewAssetsCommandExecute();

	/** Handler for creating new folder */
	void HandleCreateNewFolderCommandExecute();

	/** True if the user may use the history back button */
	bool IsBackEnabled() const;

	/** True if the user may use the history forward button */
	bool IsForwardEnabled() const;

	/** Gets the tool tip text for the history back button */
	FText GetHistoryBackTooltip() const;

	/** Gets the tool tip text for the history forward button */
	FText GetHistoryForwardTooltip() const;

	/** Sets the global selection set to the asset view's selected items */
	void SyncGlobalSelectionSet();

	/** Updates the breadcrumb trail to the current path */
	void UpdatePath();

	/** Handler for when a filter in the filter list has changed */
	void OnFilterChanged();

	/** Gets the text for the path label */
	FText GetPathText() const;

	/** Returns true if currently filtering by a source */
	bool IsFilteredBySource() const;

	/** Handler for when the context menu or asset view requests to find items in the paths view */
	void OnShowInPathsViewRequested(TArrayView<const FContentBrowserItem> ItemsToFind);

	/** Handler for when the user has committed a rename of an item */
	void OnItemRenameCommitted(TArrayView<const FContentBrowserItem> Items);

	/** Handler for when the asset context menu requests to rename an item */
	void OnRenameRequested(const FContentBrowserItem& Item, EContentBrowserViewContext ViewContext);

	/** Handler for when the path context menu has successfully deleted a folder */
	void OnOpenedFolderDeleted();

	/** Handler for when the asset context menu requests to duplicate an item */
	void OnDuplicateRequested(TArrayView<const FContentBrowserItem> OriginalItems);

	/** Handler for when the asset context menu requests to edit an item */
	void OnEditRequested(TArrayView<const FContentBrowserItem> Items);

	/** Handler for when the asset context menu requests to refresh the asset view */
	void OnAssetViewRefreshRequested();

	/** Handles an on collection destroyed event */
	void HandleCollectionRemoved(const FCollectionNameType& Collection);

	/** Handles an on collection renamed event */
	void HandleCollectionRenamed(const FCollectionNameType& OriginalCollection, const FCollectionNameType& NewCollection);

	/** Handles an on collection updated event */
	void HandleCollectionUpdated(const FCollectionNameType& Collection);

	/** Handles a path removed event */
	void HandlePathRemoved(const FName Path);

	/** Handles content items being updated */
	void HandleItemDataUpdated(TArrayView<const FContentBrowserItemDataUpdate> InUpdatedItems);

	/** Gets all suggestions for the asset search box */
	void OnAssetSearchSuggestionFilter(const FText& SearchText, TArray<FAssetSearchBoxSuggestion>& PossibleSuggestions, FText& SuggestionHighlightText) const;

	/** Combines the chosen suggestion with the active search text */
	FText OnAssetSearchSuggestionChosen(const FText& SearchText, const FString& Suggestion) const;

	/** Gets the dynamic hint text for the "Search Assets" search text box */
	FText GetSearchAssetsHintText() const;

	/** Delegate called when generating the context menu for an item */
	TSharedPtr<SWidget> GetItemContextMenu(TArrayView<const FContentBrowserItem> SelectedItems, EContentBrowserViewContext ViewContext);

	/** Populate the context menu for a folder */
	void PopulateFolderContextMenu(UToolMenu* Menu);

	/** Delegate called to get the current selection state */
	void GetSelectionState(TArray<FAssetData>& SelectedAssets, TArray<FString>& SelectedPaths);

	/** Sets up an inline-name for the creation of a default-named folder the specified path */
	void CreateNewFolder(FString FolderPath, FOnCreateNewFolder OnCreateNewFolder);

	/** Handler for when "Open in new Content Browser" is selected */
	void OpenNewContentBrowser();

	/** Bind our UI commands */
	void BindCommands();

	/** Gets the visibility of the favorites view */
	EVisibility GetFavoriteFolderVisibility() const;

	/** Get the visibility of the docked collections view */
	EVisibility GetDockedCollectionsVisibility() const;

	/** Toggles the favorite status of an array of folders*/
	void ToggleFolderFavorite(const TArray<FString>& FolderPaths);

	/** Called when Asset View Options "Search" options change */
	void HandleAssetViewSearchOptionsChanged();

	/** Register menu for filtering path view */
	static void RegisterPathViewFiltersMenu();

	/** Fill menu for filtering path view with items */
	void PopulatePathViewFiltersMenu(UToolMenu* Menu);

	/** Add data so that menus can access content browser */
	void ExtendAssetViewButtonMenuContext(FToolMenuContext& InMenuContext);

private:

	/** The tab that contains this browser */
	TWeakPtr<SDockTab> ContainingTab;

	/** The manager that keeps track of history data for this browser */
	FHistoryManager HistoryManager;

	/** A helper class to manage asset context menu options */
	TSharedPtr<class FAssetContextMenu> AssetContextMenu;

	/** The context menu manager for the path view */
	TSharedPtr<class FPathContextMenu> PathContextMenu;

	/** The sources search, shared between the paths and collections views */
	TSharedPtr<FSourcesSearch> SourcesSearch;

	/** The asset tree widget */
	TSharedPtr<SPathView> PathViewPtr;

	/** The favorites tree widget */
	TSharedPtr<class SFavoritePathView> FavoritePathViewPtr;

	/** The collection widget */
	TSharedPtr<SCollectionView> CollectionViewPtr;

	/** The asset view widget */
	TSharedPtr<SAssetView> AssetViewPtr;

	/** The breadcrumb trail representing the current path */
	TSharedPtr< SBreadcrumbTrail<FString> > PathBreadcrumbTrail;

	/** The text box used to search for assets */
	TSharedPtr<SAssetSearchBox> SearchBoxPtr;

	/** The filter list */
	TSharedPtr<SFilterList> FilterListPtr;

	/** The path picker */
	TSharedPtr<SComboButton> PathPickerButton;

	/** Index of the active sources widget */
	int32 ActiveSourcesWidgetIndex = 0;

	/** The expanded state of the asset tree */
	bool bSourcesViewExpanded;

	/** True if this browser is the primary content browser */
	bool bIsPrimaryBrowser;

	/** True if this content browser can be set to the primary browser. */
	bool bCanSetAsPrimaryBrowser;

	/** Unique name for this Content Browser. */
	FName InstanceName;

	/** True if source should not be changed from an outside source */
	bool bIsLocked;

	/** The list of FrontendFilters currently applied to the asset view */
	TSharedPtr<FAssetFilterCollectionType> FrontendFilters;

	/** The text filter to use on the assets */
	TSharedPtr< FFrontendFilter_Text > TextFilter;

	/** Commands handled by this widget */
	TSharedPtr< FUICommandList > Commands;

	/** Delegate used to create a new folder */
	FOnCreateNewFolder OnCreateNewFolder;

	/** Switcher between the different sources views */
	TSharedPtr<SWidgetSwitcher> SourcesWidgetSwitcher;

	/** The splitter between the path & asset view */
	TSharedPtr<SSplitter> PathAssetSplitterPtr;

	/** The splitter between the path & favorite view */
	TSharedPtr<SSplitter> PathFavoriteSplitterPtr;

	/** The list of plugin filters currently applied to the path view */
	TSharedPtr<FPluginFilterCollectionType> PluginPathFilters;

	/** When viewing a dynamic collection, the active search query will be stashed in this variable so that it can be restored again later */
	TOptional<FText> StashedSearchBoxText;

public: 

	/** The section of EditorPerProjectUserSettings in which to save content browser settings */
	static const FString SettingsIniSection;
};
