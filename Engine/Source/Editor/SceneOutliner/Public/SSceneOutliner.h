// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Views/ITypedTableView.h"
#include "Input/Reply.h"
#include "Layout/Visibility.h"
#include "Misc/Attribute.h"
#include "Misc/TextFilter.h"
#include "SlateFwd.h"
#include "Styling/SlateColor.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"

#include "ICustomSceneOutliner.h"
#include "ISceneOutliner.h"
#include "SOutlinerTreeView.h"
#include "SceneOutlinerPublicTypes.h"
#include "SceneOutlinerStandaloneTypes.h"

class FMenuBuilder;
class UToolMenu;
class ISceneOutlinerColumn;
class SComboButton;
class USceneOutlinerSettings;

template<typename ItemType> class STreeView;

/**
 * Scene Outliner definition
 * Note the Scene Outliner is also called the World Outliner
 */
namespace SceneOutliner
{
	typedef TTextFilter< const ITreeItem& > TreeItemTextFilter;

	/** Structure that defines an operation that should be applied to the tree */
	struct FPendingTreeOperation
	{
		enum EType { Added, Removed, Moved };
		FPendingTreeOperation(EType InType, TSharedRef<ITreeItem> InItem) : Type(InType), Item(InItem) { }

		/** The type of operation that is to be applied */
		EType Type;

		/** The tree item to which this operation relates */
		FTreeItemRef Item;
	};

	/** Set of actions to apply to new tree items */
	namespace ENewItemAction
	{
		enum Type
		{
			/** Select the item when it is created */
			Select			= 1 << 0,
			/** Scroll the item into view when it is created */
			ScrollIntoView	= 1 << 1,
			/** Interactively rename the item when it is created (implies the above) */
			Rename			= 1 << 2,
		};
	}

	/** Get a description of a world to display in the scene outliner */
	FText GetWorldDescription(UWorld* World);

	/**
	 * Scene Outliner widget
	 */
	class SSceneOutliner : public ICustomSceneOutliner, public FEditorUndoClient, public FGCObject
	{

	public:

		SLATE_BEGIN_ARGS( SSceneOutliner ) {}
			SLATE_ARGUMENT( FOnSceneOutlinerItemPicked, OnItemPickedDelegate )
		SLATE_END_ARGS()

		/**
		 * Construct this widget.  Called by the SNew() Slate macro.
		 *
		 * @param	InArgs		Declaration used by the SNew() macro to construct this widget
		 * @param	InitOptions	Programmer-driven initialization options for this widget
		 */
		void Construct( const FArguments& InArgs, const FInitializationOptions& InitOptions );

		/** Default constructor - initializes data that is shared between all tree items */
		SSceneOutliner() : SharedData(MakeShareable(new FSharedOutlinerData)) {}

		/** SSceneOutliner destructor */
		~SSceneOutliner();

		/** SWidget interface */
		virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;
		virtual bool SupportsKeyboardFocus() const override;
		virtual FReply OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent ) override;

		/** Sends a requests to the Scene Outliner to refresh itself the next chance it gets */
		virtual void Refresh() override;

		//~ Begin FEditorUndoClient Interface
		virtual void PostUndo(bool bSuccess) override;
		virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }
		// End of FEditorUndoClient

		/** @return Returns the common data for this outliner */
		virtual const FSharedOutlinerData& GetSharedData() const override
		{
			return *SharedData;
		}

		/** Get a const reference to the actual tree hierarchy */
		virtual const STreeView<FTreeItemPtr>& GetTree() const override
		{
			return *OutlinerTreeView;
		}

		/** @return Returns a string to use for highlighting results in the outliner list */
		virtual TAttribute<FText> GetFilterHighlightText() const override;

		/** Set the keyboard focus to the outliner */
		virtual void SetKeyboardFocus() override;

		/** Gets the cached icon for this class name */
		virtual const FSlateBrush* GetCachedIconForClass(FName InClassName) const override;

		/** Sets the cached icon for this class name */
		virtual void CacheIconForClass(FName InClassName, const FSlateBrush* InSlateBrush) override;

		/** Adds a new item for the specified type and refreshes the tree, provided it matches the filter terms */
		template<typename TreeItemType, typename DataType>
		void ConstructItemFor(const DataType& Data)
		{
			// We test the filters with a temporary so we don't allocate on the heap unnecessarily
			const TreeItemType Temporary(Data);
			if (Filters->PassesAllFilters(Temporary) && SearchBoxFilter->PassesFilter(Temporary))
			{
				FTreeItemRef NewItem = MakeShareable(new TreeItemType(Data));
				PendingOperations.Emplace(FPendingTreeOperation::Added, NewItem);
				PendingTreeItemMap.Add(NewItem->GetID(), NewItem);
				ConstructSubComponentItems(NewItem);
				Refresh();
			}
		}

		/** Should the scene outliner accept a request to rename a object */
		virtual bool CanExecuteRenameRequest(const FTreeItemPtr& ItemPtr) const override;

		/**
		 * Add a filter to the scene outliner
		 * @param Filter The filter to apply to the scene outliner
		 * @return The index of the filter.
		 */
		virtual int32 AddFilter(const TSharedRef<SceneOutliner::FOutlinerFilter>& Filter) override;

		/**
		 * Remove a filter from the scene outliner
		 * @param Filter The Filter to remove
		 * @return True if the filter was removed.
		 */
		virtual bool RemoveFilter(const TSharedRef<SceneOutliner::FOutlinerFilter>& Filter) override;

		/**
		 * Retrieve the filter at the specified index
		 * @param Index The index of the filter to retrive
		 * @return A valid poiter to a filter if the index was valid
		 */
		virtual TSharedPtr<SceneOutliner::FOutlinerFilter> GetFilterAtIndex(int32 Index) override;

		/** Get number of filters applied to the scene outliner */
		virtual int32 GetFilterCount() const override;

		/**
		 * Add or replace a column of the scene outliner
		 * Note: The column id must match the id of the column returned by the factory
		 * @param ColumnId The id of the column to add
		 * @param ColumInfo The struct that contains the information on how to present and retrieve the column
		 */
		virtual void AddColumn(FName ColumId, const SceneOutliner::FColumnInfo& ColumInfo) override;

		/**
		 * Remove a column of the scene outliner
		 * @param ColumnId The name of the column to remove
		 */
		virtual void RemoveColumn(FName ColumId) override;

		/** Return the name/Id of the columns of the scene outliner */
		virtual TArray<FName> GetColumnIds() const override;

		/** @return Returns the current sort mode of the specified column */
		virtual EColumnSortMode::Type GetColumnSortMode(const FName ColumnId) const;

		/** Request that the tree be sorted at a convenient time */
		virtual void RequestSort();

		/** Returns true if edit delete can be executed */
		virtual bool Delete_CanExecute();

		/** Returns true if edit rename can be executed */
		virtual bool Rename_CanExecute();

		/** Executes rename. */
		virtual void Rename_Execute();

		/** Returns true if edit cut can be executed */
		virtual bool Cut_CanExecute();

		/** Returns true if edit copy can be executed */
		virtual bool Copy_CanExecute();

		/** Returns true if edit paste can be executed */
		virtual bool Paste_CanExecute();

		/** Returns true if clipboard contains folders only */
		bool CanPasteFoldersOnlyFromClipboard();

		/** Can the scene outliner rows generated on drag event */
		bool CanSupportDragAndDrop() const;

		/** Tells the scene outliner that it should do a full refresh, which will clear the entire tree and rebuild it from scratch. */
		virtual void FullRefresh() override;

	public:
		/** Methods for the custom scene outliner interface */

		/**
		 * Set the selection mode of the scene outliner.
		 * @param SelectionMode The new selection mode
		 */
		virtual ICustomSceneOutliner& SetSelectionMode(ESelectionMode::Type InSelectionMode) override;

		/**
		 * Tell the scene outliner to use this visitor before accepting a rename request from a actor or from the prebuild column Item Label
		 * @param CanRenameItem The visitor that will be used to validate that a item can be renamed (return true to rename)
		 */
		virtual ICustomSceneOutliner& SetCanRenameItem(TUniquePtr<TTreeItemGetter<bool>>&& CanRenameItem) override;

		/**
		 * Tell the scene outliner to use this visitor to dertimine if a newly added item should be selected
		 * @param ShouldSelectItemWhenAdded The visitor be used to select a new item (return true if the item should be selected)
		 */
		virtual ICustomSceneOutliner& SetShouldSelectItemWhenAdded(TUniquePtr<TTreeItemGetter<bool>>&& ShouldSelectItemWhenAdded) override;

		/**
		 * Set the behavior for when a item is dragged
		 * Note: to avoid having to different user experience from the world outliner. The callback is only called from a left click drag.
		 * @param Callback The function that will be called when a drag from a item row is detected
		 */
		virtual ICustomSceneOutliner& SetOnItemDragDetected(TUniqueFunction<FReply (const SceneOutliner::ITreeItem&)> Callback) override;

		/**
		 * Set the behavior for when a drag pass over a Item of the scene outliner
		 * @param Callback The function that will be called at each update when there is a drag over a item
		 */
		virtual ICustomSceneOutliner& SetOnDragOverItem(TUniqueFunction<FReply (const FDragDropEvent&, const SceneOutliner::ITreeItem&)> Callback) override;

		/**
		 * Set the behavior for when a drag and drop is dropped on the scene outliner
		 * @param Callback The function that will be called
		 */
		virtual ICustomSceneOutliner& SetOnDropOnItem(TUniqueFunction<FReply (const FDragDropEvent&, const SceneOutliner::ITreeItem&)> Callback) override;


		/**
		 * Set the behavior for when a drag and drop enter the zone of a item
		 * @param Callback The function that will be called
		 */
		virtual ICustomSceneOutliner& SetOnDragEnterItem(TUniqueFunction<void (const FDragDropEvent&, const SceneOutliner::ITreeItem&)> Callback) override;

		/**
		 * Set the behavior for when a drag and drop leave the zone of a item
		 * @param Callback The function that will be called
		 */
		virtual ICustomSceneOutliner& SetOnDragLeaveItem(TUniqueFunction<void (const FDragDropEvent&, const SceneOutliner::ITreeItem&)> Callback) override;

		const TUniqueFunction<FReply (const SceneOutliner::ITreeItem&)>& GetOnItemDragDetected() const;
		const TUniqueFunction<FReply (const FDragDropEvent&, const SceneOutliner::ITreeItem&)>& GetOnDragOverItem() const;
		const TUniqueFunction<FReply (const FDragDropEvent&, const SceneOutliner::ITreeItem&)>& GetOnDropOnItem() const;
		const TUniqueFunction<void (const FDragDropEvent&, const SceneOutliner::ITreeItem&)>& GetOnDragEnterItem() const;
		const TUniqueFunction<void (const FDragDropEvent&, const SceneOutliner::ITreeItem&)>& GetOnDragLeaveItem() const;

		/**
		 * Tell this scene outliner to use the shared setting or not.
		 * The shared settings are those used by the world ouliner tab in the level editor
		 * Note: by default it does not use the shared settings
		 */
		virtual ICustomSceneOutliner& SetUseSharedSceneOutlinerSettings(bool bUseSharedSettings) override;

		/**
		 * Is the scene outliner using the shared settings? (The same as the world outliner)
		 * @return True if the scene outliner use the shared settings
		 */
		virtual bool IsUsingSharedSceneOutlinerSettings() const override;

			/** Set the hide temporary actors filter */
		virtual ICustomSceneOutliner& SetHideTemporaryActors(bool bHideTemporaryActors) override;

		/** Set the show only in current level setting  */
		virtual ICustomSceneOutliner& SetShowOnlyCurrentLevel(bool bShowOnlyCurrentLevel) override;

		/** Set the show only selected setting */
		virtual ICustomSceneOutliner& SetShownOnlySelected(bool bShownOnlySelected) override;

		/** Set the show actor components setting */
		virtual ICustomSceneOutliner& SetShowActorComponents(bool bShowActorComponents) override;

		/** Event to react to a user double click on a item */
		virtual FTreeItemPtrEvent& GetDoubleClickEvent() override { return OnDoubleClickOnTreeEvent; }

		/**
		 * Allow the system that use the scene outliner to react when it's selection is changed
		 * Note: This event will only be broadcast on a user input.
		 */
		virtual FOnItemSelectionChanged& GetOnItemSelectionChanged() override { return OnItemSelectionChanged; }

		/**
		 * Set the selection of the scene outliner
		 * The items that return true will be the ones selected
		 * @param ItemSelector A visitor that will be used set the selection.
		 */
		virtual void SetSelection(const SceneOutliner::TTreeItemGetter<bool>& ItemSelector) override;

		/**
		 * Add some items to selection of the scene outliner
		 * The items that return true will be the ones added to the selection
		 * @param ItemSelector A visitor that will be used to add some items to the selection.
		 */
		virtual void AddToSelection(const SceneOutliner::TTreeItemGetter<bool>& ItemSelector) override;

		/**
		 * Remove some items from selection of the scene outliner
		 * The items that return true will be the ones removed from the selection
		 * @param ItemSelector A visitor that will be used to remove some items from the selection.
		 */
		virtual void RemoveFromSelection(const SceneOutliner::TTreeItemGetter<bool>& ItemSelector) override;

		/**
		 * Add a object to the selection of the scene outliner
		 * @param Object The Object that will be added to the selection
		 */
		virtual void AddObjectToSelection(const UObject* Object) override;

		/**
		 * Remove a object from the selection of the scene outliner
		 * @param Object The Object that will be removed from the selection
		 */
		virtual void RemoveObjectFromSelection(const UObject* Object) override;

		/**
		 * Add a folder to the selection of the scene outliner
		 * @param FolderName The name of the folder to add to selection
		 */
		virtual void AddFolderToSelection(const FName& FolderName) override;

		/**
		 * Remove a folder from the selection of the scene outliner
		 * @param FolderName The name of the folder to remove from the selection
		 */
		virtual void RemoveFolderFromSelection(const FName& FolderName) override;

		/** Deselect all selected items */
		virtual void ClearSelection() override;

	private:
		/** Methods that implement structural modification logic for the tree */

		/** Empty all the tree item containers maintained by this outliner */
		void EmptyTreeItems();

		/** Apply incremental changes to, or a complete repopulation of the tree  */
		void Populate();

		/** Repopulates the entire tree */
		void RepopulateEntireTree();

		/** Tells the scene outliner that there was a change in the level actor list. */
		void OnLevelActorListChanged();

		/** Attempts to add an item to the tree. Will add any parents if required. */
		bool AddItemToTree(FTreeItemRef InItem);

		/** Add an item to the tree, even if it doesn't match the filter terms. Used to add parent's that would otherwise be filtered out */
		void AddUnfilteredItemToTree(FTreeItemRef Item);

		/** Ensure that the specified item's parent is added to the tree, if applicable */
		FTreeItemPtr EnsureParentForItem(FTreeItemRef Item);

		/** Remove the specified item from the tree */
		void RemoveItemFromTree(FTreeItemRef InItem);

		/** Called when a child has been removed from the specified parent. Will potentially remove the parent from the tree */
		void OnChildRemovedFromParent(ITreeItem& Parent);

		/** Called when a child has been moved in the tree hierarchy */
		void OnItemMoved(const FTreeItemRef& Item);

		void ConstructSubComponentItems(FTreeItemRef Item)
		{
			for (FTreeItemRef SubItem : Item->GetSubComponentItems())
			{
				PendingOperations.Emplace(FPendingTreeOperation::Added, SubItem);
				PendingTreeItemMap.Add(SubItem->GetID(), SubItem);
			}
		}

		/** Visitor that is used to validate if the item should added to the tree */
		struct FValidateItemBeforeAddingToTree : TTreeItemGetter<bool>
		{
			/** Override to extract the data from specific tree item types */
			virtual bool Get(const FActorTreeItem& ActorItem) const { return ActorItem.Actor.IsValid(); }
			virtual bool Get(const FWorldTreeItem& WorldItem) const { return true; }
			virtual bool Get(const FFolderTreeItem& FolderItem) const { return true; }
			virtual bool Get(const FComponentTreeItem& ComponentFunction) const { return ComponentFunction.Component.IsValid(); }
			virtual bool Get(const FSubComponentTreeItem& CustomFunction) const { return CustomFunction.ParentComponent.IsValid(); }
		};

		void RegisterDefaultContextMenu();

		/** Visitor that is used to set up type-specific data after tree items are added to the tree */
		struct FOnItemAddedToTree : IMutableTreeItemVisitor
		{
			SSceneOutliner& Outliner;
			FOnItemAddedToTree(SSceneOutliner& InOutliner) : Outliner(InOutliner) {}

			virtual void Visit(FActorTreeItem& Actor) const override;
			virtual void Visit(FFolderTreeItem& Folder) const override;
		};

		/** Friendship required so the visitor can access our guts */
		friend FOnItemAddedToTree;

	public:

		/** Instruct the outliner to perform an action on the specified item when it is created */
		void OnItemAdded(const FTreeItemID& ItemID, uint8 ActionMask);

		/** Get the columns to be displayed in this outliner */
		const TMap<FName, TSharedPtr<ISceneOutlinerColumn>>& GetColumns() const
		{
			return Columns;
		}

	private:

		/** Map of columns that are shown on this outliner. */
		TMap<FName, TSharedPtr<ISceneOutlinerColumn>> Columns;

		/** Set up the columns required for this outliner */
		void SetupColumns(SHeaderRow& HeaderRow);

		/** Refresh the scene outliner for when a colum was added or removed */
		void RefreshColums();

		/** Populates OutSearchStrings with the strings associated with TreeItem that should be used in searching */
		void PopulateSearchStrings( const ITreeItem& TreeItem, OUT TArray< FString >& OutSearchStrings ) const;


	public:
		/** Miscellaneous helper functions */

		/** Scroll the specified item into view */
		void ScrollItemIntoView(FTreeItemPtr Item);

	private:

		/** Synchronize the current actor selection in the world, to the tree */
		void SynchronizeActorSelection();

		/** Component has has an selection change that we need to Synchronize with */
		void OnComponentSelectionChanged(UActorComponent* Component);

		/** Component has has an selection change that we need to Synchronize with */
		void OnComponentsUpdated();

		/** Check that we are reflecting a valid world */
		bool CheckWorld() const { return SharedData->RepresentingWorld != nullptr; }

		/** Check whether we should be showing folders or not in this scene outliner */
		bool ShouldShowFolders() const;

		/** Get an array of selected folders */
		TArray<FFolderTreeItem*> GetSelectedFolders() const;

		/** Get an array of selected folder names */
		TArray<FName> GetSelectedFolderNames() const;

		/** Checks to see if the actor is valid for displaying in the outliner */
		bool IsActorDisplayable( const AActor* Actor ) const;

		/** @return	Returns true if the filter is currently active */
		bool IsFilterActive() const;

	private:
		/** Tree view event bindings */

		/** Called by STreeView to generate a table row for the specified item */
		TSharedRef< ITableRow > OnGenerateRowForOutlinerTree( FTreeItemPtr Item, const TSharedRef< STableViewBase >& OwnerTable );

		/** Called by STreeView to get child items for the specified parent item */
		void OnGetChildrenForOutlinerTree( FTreeItemPtr InParent, TArray< FTreeItemPtr >& OutChildren );

		/** Called by STreeView when the tree's selection has changed */
		void OnOutlinerTreeSelectionChanged( FTreeItemPtr TreeItem, ESelectInfo::Type SelectInfo );

		/** Called by STreeView when the user double-clicks on an item in the tree */
		void OnOutlinerTreeDoubleClick( FTreeItemPtr TreeItem );

		/** Called by STreeView when an item is scrolled into view */
		void OnOutlinerTreeItemScrolledIntoView( FTreeItemPtr TreeItem, const TSharedPtr<ITableRow>& Widget );

		/** Called when an item in the tree has been collapsed or expanded */
		void OnItemExpansionChanged(FTreeItemPtr TreeItem, bool bIsExpanded) const;

	private:
		/** Level, editor and other global event hooks required to keep the outliner up to date */

		/** Called by USelection::SelectionChangedEvent delegate when the level's selection changes */
		void OnLevelSelectionChanged(UObject* Obj);

		/** Called by the engine when a level is added to the world. */
		void OnLevelAdded(ULevel* InLevel, UWorld* InWorld);

		/** Called by the engine when a level is removed from the world. */
		void OnLevelRemoved(ULevel* InLevel, UWorld* InWorld);

		/** Called by the engine when an actor is added to the world. */
		void OnLevelActorsAdded(AActor* InActor);

		/** Called by the engine when an actor is remove from the world. */
		void OnLevelActorsRemoved(AActor* InActor);

		/** Called by the engine when an actor is attached in the world. */
		void OnLevelActorsAttached(AActor* InActor, const AActor* InParent);

		/** Called by the engine when an actor is dettached in the world. */
		void OnLevelActorsDetached(AActor* InActor, const AActor* InParent);

		/** Called by the engine when an actor is being requested to be renamed */
		void OnLevelActorsRequestRename(const AActor* InActor);

		/** Called by the engine when an actor's folder is changed */
		void OnLevelActorFolderChanged(const AActor* InActor, FName OldPath);

		/** Handler for when a property changes on any object */
		void OnActorLabelChanged(AActor* ChangedActor);

		/** Handler for when an asset is reloaded */
		void OnAssetReloaded(const EPackageReloadPhase InPackageReloadPhase, FPackageReloadedEvent* InPackageReloadedEvent);

		/** Called when the map has changed*/
		void OnMapChange(uint32 MapFlags);

		/** Called when the current level has changed */
		void OnNewCurrentLevel();

		/** Called when a folder is to be created */
		void OnBroadcastFolderCreate(UWorld& InWorld, FName NewPath);

		/** Called when a folder is to be moved */
		void OnBroadcastFolderMove(UWorld& InWorld, FName OldPath, FName NewPath);

		/** Called when a folder is to be deleted */
		void OnBroadcastFolderDelete(UWorld& InWorld, FName Path);


		/**
		 * All those function bellow are some callback to some editor signals/commands.
		 * They are only bind to the editors delegates when the scene outliner is in Actor Browsing mode
		 */

		/** Called by engine when edit cut actors begins */
		void OnEditCutActorsBegin();

		/** Called by engine when edit cut actors ends */
		void OnEditCutActorsEnd();

		/** Called by engine when edit copy actors begins */
		void OnEditCopyActorsBegin();

		/** Called by engine when edit copy actors ends */
		void OnEditCopyActorsEnd();

		/** Called by engine when edit paste actors begins */
		void OnEditPasteActorsBegin();

		/** Called by engine when edit paste actors ends */
		void OnEditPasteActorsEnd();

		/** Called by engine when edit duplicate actors begins */
		void OnDuplicateActorsBegin();

		/** Called by engine when edit duplicate actors ends */
		void OnDuplicateActorsEnd();

		/** Called by engine when edit delete actors begins */
		void OnDeleteActorsBegin();

		/** Called by engine when edit delete actors ends */
		void OnDeleteActorsEnd();

		// End of the editor callback


		/** Copy specified folders to clipboard, keeping current clipboard contents if they differ from previous clipboard contents (meaning actors were copied) */
		void CopyFoldersToClipboard(const TArray<FName>& InFolders, const FString& InPrevClipboardContents);

		/** Called by copy and duplicate */
		void CopyFoldersBegin();

		/** Called by copy and duplicate */
		void CopyFoldersEnd();

		/** Called by paste and duplicate */
		void PasteFoldersBegin(TArray<FFolderTreeItem*> InFolders);

		/** Called by paste and duplicate */
		void PasteFoldersBegin(TArray<FName> InFolders);

		/** Paste folders end logic */
		void PasteFoldersEnd();

		/** Called by cut and delete */
		void DeleteFoldersBegin();

		/** Called by cute and delete */
		void DeleteFoldersEnd();

		/** Get an array of folders to paste */
		TArray<FName> GetClipboardPasteFolders() const;

		/** Construct folders export string to be used in clipboard */
		FString ExportFolderList(TArray<FName> InFolders) const;

		/** Construct array of folders to be created based on input clipboard string */
		TArray<FName> ImportFolderList(const FString& InStrBuffer) const;

	public:
		/** Duplicates current folder and all descendants */
		void DuplicateFoldersHierarchy();

	private:
		/** Cache selected folders during edit delete */
		TArray<FFolderTreeItem*> CacheFoldersDelete;

		/** Cache folders for cut/copy/paste/duplicate */
		TArray<FName> CacheFoldersEdit;

		/** Cache clipboard contents for cut/copy */
		FString CacheClipboardContents;

		/** Maps pre-existing children during paste or duplicate */
		TMap<FName, TArray<FTreeItemID>> CachePasteFolderExistingChildrenMap;

	private:
		/** Miscellaneous bindings required by the UI */

		/** Called by the editable text control when the filter text is changed by the user */
		void OnFilterTextChanged( const FText& InFilterText );

		/** Called by the editable text control when a user presses enter or commits their text change */
		void OnFilterTextCommitted( const FText& InFilterText, ETextCommit::Type CommitInfo );

		/** Called by the filter button to get the image to display in the button */
		const FSlateBrush* GetFilterButtonGlyph() const;

		/** @return	The filter button tool-tip text */
		FString GetFilterButtonToolTip() const;

		/** @return	Returns whether the filter status line should be drawn */
		EVisibility GetFilterStatusVisibility() const;

		/** @return	Returns the filter status text */
		FText GetFilterStatusText() const;

		/** @return Returns color for the filter status text message, based on success of search filter */
		FSlateColor GetFilterStatusTextColor() const;

		/**	Returns the current visibility of the Empty label */
		EVisibility GetEmptyLabelVisibility() const;

		/** @return the border brush */
		const FSlateBrush* OnGetBorderBrush() const;

		/** @return the the color and opacity of the border brush; green if in PIE/SIE mode */
		FSlateColor OnGetBorderColorAndOpacity() const;

		/** @return the selection mode; disabled entirely if in PIE/SIE mode */
		ESelectionMode::Type GetSelectionMode() const;

		/** @return the content for the view button */
		TSharedRef<SWidget> GetViewButtonContent(bool bWorldPickerOnly, bool bShouldDisplayChooseWorld);

		/** Build the content for the world picker submenu */
		void BuildWorldPickerContent(FMenuBuilder& MenuBuilder);

		/** @return the foreground color for the view button */
		FSlateColor GetViewButtonForegroundColor() const;

		/** @return the foreground color for the world picker button */
		FSlateColor GetWorldPickerForegroundColor() const;

		/** The brush to use when in Editor mode */
		const FSlateBrush* NoBorder;
		/** The brush to use when in PIE mode */
		const FSlateBrush* PlayInEditorBorder;
		/** The brush to use when in SIE mode */
		const FSlateBrush* SimulateBorder;

	private:

		/** Open a context menu for this scene outliner */
		TSharedPtr<SWidget> OnOpenContextMenu();

		/** Build a context menu for right-clicking an item in the tree */
		TSharedPtr<SWidget> BuildDefaultContextMenu();
		void FillFoldersSubMenu(UToolMenu* Menu) const;
		void AddMoveToFolderOutliner(UToolMenu* Menu) const;
		void FillSelectionSubMenu(UToolMenu* Menun) const;
		TSharedRef<TSet<FName>> GatherInvalidMoveToDestinations() const;

	private:

		/** Called to select descendants of the currently selected folders */
		void SelectFoldersDescendants(bool bSelectImmediateChildrenOnly = false);

		/** Move the selected items to the specified parent */
		void MoveSelectionTo(FTreeItemRef NewParent);

		/** Moves the current selection to the specified folder path */
		void MoveSelectionTo(FName NewParent);

		/** Called when the user has clicked the button to add a new folder */
		FReply OnCreateFolderClicked();

		/** Create a new folder under the specified parent name (NAME_None for root) */
		void CreateFolder();

	private:
		/** FILTERS */

		/** Synchronize the build in filter */
		void OnSharedSettingChanged();

		/** @return whether we are displaying only selected Actors */
		virtual bool IsShowingOnlySelected() const override;
		/** Toggles whether we are displaying only selected Actors */
		void ToggleShowOnlySelected();
		/** Enables/Disables whether the SelectedActorFilter is applied */
		void ApplyShowOnlySelectedFilter(bool bShowOnlySelected);

		/** @return whether we are hiding temporary Actors */
		virtual bool IsHidingTemporaryActors() const override;
		/** Toggles whether we are hiding temporary Actors */
		void ToggleHideTemporaryActors();
		/** Enables/Disables whether the HideTemporaryActorsFilter is applied */
		void ApplyHideTemporaryActorsFilter(bool bHideTemporaryActors);

		/** @return whether we are showing only Actors that are in the Current Level */
		virtual bool IsShowingOnlyCurrentLevel() const override;
		/** Toggles whether we are hiding Actors that aren't in the current level */
		void ToggleShowOnlyCurrentLevel();
		/** Enables/Disables whether the ShowOnlyActorsInCurrentLevelFilter is applied */
		void ApplyShowOnlyCurrentLevelFilter(bool bShowOnlyActorsInCurrentLevel);

		/** @return whether we are hiding Folders with hidden actors */
		bool IsHidingFoldersContainingOnlyHiddenActors() const;
		/** Toggles whether we are hiding Folders with hidden actors */
		void ToggleHideFoldersContainingOnlyHiddenActors();

		/** @return whether we are showing the components of the Actors */
		virtual bool IsShowingActorComponents() const override;
		/** Toggles whether we are showing the components of the Actors */
		void ToggleShowActorComponents();
		/** Enables/Disables whether the HideTemporaryActorsFilter is applied */
		void ApplyShowActorComponentsFilter(bool bShowActorComponents);

		/** When applied, only selected Actors are displayed */
		TSharedPtr< FOutlinerFilter > SelectedActorFilter;

		/** When applied, temporary and run-time actors are hidden */
		TSharedPtr< FOutlinerFilter > HideTemporaryActorsFilter;

		/** When applied, only Actors that are in the current level are displayed */
		TSharedPtr< FOutlinerFilter > ShowOnlyActorsInCurrentLevelFilter;

		/** When applied, Actor components are displayed */
		TSharedPtr< FOutlinerFilter > ShowActorComponentsFilter;

	private:

		/** Context menu opening delegate provided by the client */
		FOnContextMenuOpening OnContextMenuOpening;

		/** Callback that's fired when an item is selected while in 'picking' mode */
		FOnSceneOutlinerItemPicked OnItemPicked;

		/** Shared data required by the tree and its items */
		TSharedRef<FSharedOutlinerData> SharedData;

		/** List of pending operations to be applied to the tree */
		TArray<FPendingTreeOperation> PendingOperations;

		/** Map of actions to apply to new tree items */
		TMap<FTreeItemID, uint8> NewItemActions;

		/** Our tree view */
		TSharedPtr< SOutlinerTreeView > OutlinerTreeView;

		/** A map of all items we have in the tree */
		FTreeItemMap TreeItemMap;

		/** Pending tree items that are yet to be added the tree */
		FTreeItemMap PendingTreeItemMap;

		/** Folders pending selection */
		TArray<FName> PendingFoldersSelect;

		/** Root level tree items */
		TArray<FTreeItemPtr> RootTreeItems;

		/** A set of all actors that pass the non-text filters in the representing world */
		TSet<TWeakObjectPtr<AActor>> ApplicableActors;

		/** The button that displays view options */
		TSharedPtr<SComboButton> ViewOptionsComboButton;

	private:

		/** Structure containing information relating to the expansion state of parent items in the tree */
		typedef TMap<FTreeItemID, bool> FParentsExpansionState;

		/** Cached expansion state info, in case we need to process >500 items so we don't re-fetch from the partially rebuilt tree */
		FParentsExpansionState CachedExpansionStateInfo;

		/** Gets the current expansion state of parent items */
		FParentsExpansionState GetParentsExpansionState() const;

		/** Updates the expansion state of parent items after a repopulate, according to the previous state */
		void SetParentsExpansionState(const FParentsExpansionState& ExpansionStateInfo) const;

		/** Pair of functions to Hide Folders in Outliner when HideFoldersContainingHiddenActors filter is active*/
		void HideFoldersContainingOnlyHiddenActors();
		bool HideFoldersContainingOnlyHiddenActors(FTreeItemPtr Parent, bool bIsRoot = false);

	private:

		/** Number of actors that passed the search filter */
		int32 FilteredActorCount;

		/** True if the outliner needs to be repopulated at the next appropriate opportunity, usually because our
		    actor set has changed in some way. */
		uint8 bNeedsRefresh : 1;

		/** true if the Scene Outliner should do a full refresh. */
		uint8 bFullRefresh : 1;

		/** True if the Scene Outliner is currently responding to a level visibility change */
		uint8 bDisableIntermediateSorting : 1;

		/** true when the actor selection state in the world does not match the selection state of the tree */
		uint8 bActorSelectionDirty : 1;

		uint8 bNeedsColumRefresh : 1;

		/** Reentrancy guard */
		bool bIsReentrant;

		/* Widget containing the filtering text box */
		TSharedPtr< SSearchBox > FilterTextBoxWidget;

		/** The header row of the scene outliner */
		TSharedPtr< SHeaderRow > HeaderRowWidget;

		/** A collection of filters used to filter the displayed actors and folders in the scene outliner */
		TSharedPtr< FOutlinerFilters > Filters;

		/** The TextFilter attached to the SearchBox widget of the Scene Outliner */
		TSharedPtr< TreeItemTextFilter > SearchBoxFilter;

		/** True if the search box will take keyboard focus next frame */
		bool bPendingFocusNextFrame;

		/** The tree item that is currently pending a rename */
		TWeakPtr<ITreeItem> PendingRenameItem;

		TMap<FName, const FSlateBrush*> CachedIcons;


		/** Specific for the custom mode */

		/** The current selection mode of this scene outliner */
		ESelectionMode::Type SelectionMode;

		/** A optional visitor that can be use to validate if the scene outliner should let a user rename that item */
		TUniquePtr<TTreeItemGetter<bool>> CanRenameItemVisitor;

		/** A optional visitor to select new item added to tree */
		TUniquePtr<TTreeItemGetter<bool>> ShouldSelectNewItemVisitor;

		TUniqueFunction<FReply(const SceneOutliner::ITreeItem&)> OnItemDragDetected;
		TUniqueFunction<FReply(const FDragDropEvent&, const SceneOutliner::ITreeItem&)>  OnDragOverItem;
		TUniqueFunction<FReply(const FDragDropEvent&, const SceneOutliner::ITreeItem&)> OnDropOnItem;
		TUniqueFunction<void(const FDragDropEvent&, const SceneOutliner::ITreeItem&)> OnDragEnterItem;
		TUniqueFunction<void(const FDragDropEvent&, const SceneOutliner::ITreeItem&)> OnDragLeaveItem;

		FTreeItemPtrEvent OnDoubleClickOnTreeEvent;

		FOnItemSelectionChanged OnItemSelectionChanged;

		/** Settings specifics to this scene outliner if it doesn't use the shared settings */
		USceneOutlinerSettings* SceneOutlinerSettings = nullptr;

	private:

		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

		/** Functions relating to sorting */

		/** Timer for PIE/SIE mode to sort the outliner. */
		float SortOutlinerTimer;

		/** true if the outliner currently needs to be sorted */
		bool bSortDirty;

		/** Specify which column to sort with */
		FName SortByColumn;

		/** Currently selected sorting mode */
		EColumnSortMode::Type SortMode;

		/** Handles column sorting mode change */
		void OnColumnSortModeChanged( const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type InSortMode );

		/** Sort the specified array of items based on the current sort column */
		void SortItems(TArray<FTreeItemPtr>& Items) const;

		/** Select the world we want to view */
		void OnSelectWorld(TWeakObjectPtr<UWorld> InWorld);

		/** Display a checkbox next to the world we are viewing */
		bool IsWorldChecked(TWeakObjectPtr<UWorld> InWorld);

		/** Handler for recursively expanding/collapsing items */
		void SetItemExpansionRecursive(FTreeItemPtr Model, bool bInExpansionState);
	};

}		// namespace SceneOutliner
