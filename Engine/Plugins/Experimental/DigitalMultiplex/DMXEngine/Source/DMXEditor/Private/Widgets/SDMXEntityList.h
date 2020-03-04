// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Library/DMXEntity.h"
#include "Widgets/SDMXEntityDropdownMenu.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

#include "EditorUndoClient.h"

class FDMXEditor;
class SDMXEntityList;
class UDMXLibrary;
class SDMXEntityEditor;
class UDMXEntity;
class UDMXEntityFixtureType;

class FScopedTransaction;
class FUICommandList;
class FMenuBuilder;

class SSearchBox;
class SInlineEditableTextBlock;
class SDockTab;
class SComboButton;

///////////////////////////////////////////////////////////////////////////////
// FDMXEntityListTreeNode

class FDMXTreeNodeBase
	: public TSharedFromThis<FDMXTreeNodeBase>
{
public:
	/** Delegate for when the context menu requests a rename */
	DECLARE_DELEGATE(FOnRenameRequested);

	enum class ENodeType : uint8
	{
		CategoryNode,
		EntityNode
	};

	enum class ECategoryType : uint8
	{
		DeviceProtocol,
		DMXCategory,
		FixtureAssignmentState,
		UniverseID,
		NONE
	};

	/**  Constructs an empty tree node. */
	FDMXTreeNodeBase(FDMXTreeNodeBase::ENodeType InNodeType);
	virtual ~FDMXTreeNodeBase() {}

	/**  @return The string to be used in the tree display. */
	virtual FString GetDisplayString() const;
	/**  @return The name of this node in text. */
	virtual FText GetDisplayName() const;
	/**  @return The SCS node that is represented by this object, or NULL if there is no SCS node associated with the component template. */
	virtual UDMXEntity* GetEntity() const;
	/**  @return The type of this node. */
	virtual ENodeType GetNodeType() const;

	/** Add a child node to this node */
	virtual void AddChild(TSharedPtr<FDMXTreeNodeBase> InChildPtr);
	/** Remove a child node from this node */
	virtual void RemoveChild(TSharedPtr<FDMXTreeNodeBase> InChildPtr);
	/** Remove this node from its parent one */
	virtual void RemoveFromParent();
	/**  @return Child nodes for this object. */
	virtual const TArray<TSharedPtr<FDMXTreeNodeBase>>& GetChildren() const;
	/**  Remove all child nodes from this node. */
	virtual void ClearChildren();
	/**  Sort children by name */
	virtual void SortChildren();
	/**  Sort children using custom predicate */
	virtual void SortChildren(TFunction<bool (const TSharedPtr<FDMXTreeNodeBase>&, const TSharedPtr<FDMXTreeNodeBase>&)> Predicate);
	/**  @return This object's parent node (or an invalid reference if no parent is assigned). */
	virtual TWeakPtr<FDMXTreeNodeBase> GetParent() const { return ParentNodePtr; }

	/**  @return Whether or not this node can be deleted from the entities tree */
	virtual bool CanDelete() const { return false; }

	/**  @return Whether or not this object represents a node that can be renamed from the entities tree. */
	virtual bool CanRename() const { return false; }

	/** To be overridden by a subclass that represents an Entity node */
	virtual bool IsEntityNode() const { return false; }

	/** If this node is a category node, represents the type of category. Otherwise, ECategoryType::NONE */
	virtual ECategoryType GetCategoryType() const { return ECategoryType::NONE; }

	/**
	 * Accessor to the node's RenameRequestEvent (for binding purposes). Do not
	 * Execute() the delegate from this function, instead call
	 * BroadcastRenameRequest() on the node.
	 *
	 * @return The node's internal RenameRequestEvent.
	 */
	FOnRenameRequested& OnRenameRequest() { return RenameRequestEvent; }

	/**
	 * Executes the node's RenameRequestEvent if it is bound. Otherwise, it will
	 * mark the node as having a pending rename request.
	 *
	 * @return True if the broadcast went through, false if the "pending rename request" flag was set.
	 */
	bool BroadcastRenameRequest();

	/**
	 * Sometimes a call to BroadcastRenameRequest() is made before the
	 * RenameRequestEvent has been bound. When that happens, this node is
	 * marked with a pending rename request. This method determines if that is
	 * the case for this node.
	 *
	 * @return True if a call to BroadcastRenameRequest() was made without a valid RenameRequestEvent.
	 */
	bool IsRenameRequestPending() const;

	/**
	 * Attempts to find a reference to the child node that matches the given component template.
	 *
	 * @param InComponentTemplate The component template instance to match.
	 * @param bRecursiveSearch Whether or not to recursively search child nodes (default == false).
	 * @param OutDepth If non-NULL, the depth of the child node will be returned in this parameter on success (default == NULL).
	 * @return The child node with a component template that matches the given component template instance, or an invalid node reference if no match was found.
	 */
	TSharedPtr<FDMXTreeNodeBase> FindChild(const UDMXEntity* InEntity, bool bRecursiveSearch = false, uint32* OutDepth = NULL) const;

	/** Query that determines if this item should be filtered out or not */
	virtual bool IsFlaggedForFiltration() const
	{
		// if Unknown, throws an error message
		return ensureMsgf(FilterFlags != (uint8)EFilteredState::Unknown, TEXT("Querying a bad filtration state.")) ?
			(FilterFlags & (uint8)EFilteredState::FilteredInMask) == 0 : false;
	}

	/** Refreshes this item's filtration state. Use bUpdateParent to make sure the parent's EFilteredState::ChildMatches flag is properly updated based off the new state */
	void UpdateCachedFilterState(bool bMatchesFilter, bool bUpdateParent);

	/** Update this node's desired expansion state for when there're no filters */
	void SetExpansionState(bool bNewExpansionState);
	/** This node's desired expansion state for when there're no filters */
	bool GetExpansionState() const { return bShouldBeExpanded; }

	/** If the warning tool tip is not empty, the node will display a warning icon with said tool tip. */
	void SetWarningStatus(const FText& InWarningToolTip);
	const FText& GetWarningStatus() const { return WarningToolTip; }
	/** If the error tool tip is not empty, the node will display an error icon with said tool tip. */
	void SetErrorStatus(const FText& InErrorToolTip);
	const FText& GetErrorStatus() const { return ErrorToolTip; }

	/** Operator used when sorting categories by name/number */
	bool operator<(const FDMXTreeNodeBase& Other) const;

protected:
	/** Updates the EFilteredState::ChildMatches flag, based off of children's current state */
	void RefreshCachedChildFilterState(bool bUpdateParent);
	/** Used to update the EFilteredState::ChildMatches flag for parent nodes, when this item's filtration state has changed */
	void ApplyFilteredStateToParent();

	/** DMX Entity represented by this node, if it's an entity node, otherwise invalid */
	TWeakObjectPtr<UDMXEntity> DMXEntity;

	FText WarningToolTip;
	FText ErrorToolTip;

private:
	ENodeType NodeType;

	// Actual tree structure
	TWeakPtr<FDMXTreeNodeBase> ParentNodePtr;
	TArray<TSharedPtr<FDMXTreeNodeBase>> Children;

	/** When the item is first created, a rename request may occur before everything is setup for it. This toggles to true in those cases */
	bool bPendingRenameRequest;
	/** Delegate to trigger when a rename was requested on this node */
	FOnRenameRequested RenameRequestEvent;

	/** Register whether the node should be expanded when there's no search filter text */
	bool bShouldBeExpanded;

	enum EFilteredState
	{
		FilteredOut = 0x00,
		MatchesFilter = (1 << 0),
		ChildMatches = (1 << 1),

		FilteredInMask = (MatchesFilter | ChildMatches),
		Unknown = 0xFC // ~FilteredInMask
	};
	uint8 FilterFlags;
};

class FDMXCategoryTreeNode
	: public FDMXTreeNodeBase
{
public:
	FDMXCategoryTreeNode(ECategoryType InCategoryType, FText InCategoryName, const void* InCategoryValuePtr, const FText& ToolTip = FText::GetEmpty());
	
	// ~ start FDMXTreeNodeBase interface
	virtual FString GetDisplayString() const override;
	virtual FText GetDisplayName() const override;
	virtual ECategoryType GetCategoryType() const override;
	// ~ end FDMXTreeNodeBase interface

	const FText& GetToolTip() const { return ToolTip; }

	bool IsCategoryValueValid() const { return CategoryValuePtr != nullptr; }
	template<typename ValType>
	const ValType* GetCategoryValue() const
	{
		return CategoryValuePtr == nullptr ? nullptr : (ValType*)(CategoryValuePtr);
	}

protected:
	/** This node's category type */
	ECategoryType CategoryType;
	const void* CategoryValuePtr;

	FText CategoryName;
	FText ToolTip;
};

class FDMXEntityBaseTreeNode : public FDMXTreeNodeBase
{
public:
	FDMXEntityBaseTreeNode(UDMXEntity* InEntity);

	// ~ start FDMXTreeNodeBase interface
	virtual bool IsEntityNode() const override;
	virtual bool CanDelete() const override { return true; }
	virtual bool CanRename() const override { return true; }
	// ~ end FDMXTreeNodeBase interface
};

///////////////////////////////////////////////////////////////////////////////
// FDMXEntityDragDropOperation

class FDMXEntityDragDropOperation
	: public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FDMXEntityDragDropOperation, FDragDropOperation)

	FDMXEntityDragDropOperation(UDMXLibrary* InLibrary, TWeakPtr<SDMXEntityList> EntityList, TArray<TSharedPtr<FDMXEntityBaseTreeNode>>&& InEntities);

	void SetHoveredEntity(TSharedPtr<FDMXEntityBaseTreeNode> InEntityNode, UDMXLibrary* InLibrary, TSubclassOf<UDMXEntity> TabEntitiesType);
	void SetHoveredCategory(TSharedPtr<FDMXCategoryTreeNode> InCategory, UDMXLibrary* InLibrary, TSubclassOf<UDMXEntity> TabEntitiesType);

	void DroppedOnEntity(TSharedRef<FDMXEntityBaseTreeNode> InEntity, UDMXLibrary* InLibrary, TSubclassOf<UDMXEntity> TabEntitiesType);
	void DroppedOnCategory(TSharedRef<FDMXCategoryTreeNode> InCategory, UDMXLibrary* InLibrary, TSubclassOf<UDMXEntity> TabEntitiesType);

protected:
	/** Constructs the tooltip widget that follows the mouse */
	virtual void Construct() override;

private:
	void HoverTargetChanged();

	// Common after-dropping actions
	/** Move all dragged entities to NewIndex */
	void ReorderEntities(int32 NewIndex);
	/** Set the required property to move the dragged entities into a specific category from the list */
	void SetPropertyForNewCategory();

	void SetDraggingFromMultipleCategories();
	bool GetCategoryPropertyNameFromTabType(FText& PropertyName, FText& CategoryPropertyValue);
	bool IsDraggingToDifferentCategory() const;
	TSharedPtr<FDMXCategoryTreeNode> GetTargetCategory() const;

	void SetFeedbackMessageError(const FText& Message);
	void SetFeedbackMessageOK(const FText& Message);
	void SetFeedbackMessage(const FSlateBrush* Icon, const FText& Message);

private:
	UDMXLibrary* DraggedFromLibrary;
	TArray<TSharedPtr<FDMXEntityBaseTreeNode>> DraggedEntities;
	TWeakPtr<SDMXEntityList> EntityList;

	TSharedPtr<FDMXEntityBaseTreeNode> HoveredEntity;
	TSharedPtr<FDMXCategoryTreeNode> HoveredCategory;
	TSubclassOf<UDMXEntity> HoveredTabType;
	UDMXLibrary* HoveredLibrary;

	bool bValidDropTarget;
	bool bDraggingFromMultipleCategories;

	/** Name of the entity being dragged or entities type for several ones */
	FText DraggedLabel;
};

///////////////////////////////////////////////////////////////////////////////
// Row widgets

typedef STableRow<TSharedPtr<FDMXTreeNodeBase>> SDMXTableRowType;

class SDMXCategoryRow
	: public SDMXTableRowType
{
public:

	SLATE_BEGIN_ARGS(SDMXCategoryRow)
		{}
		
		SLATE_DEFAULT_SLOT(typename SDMXCategoryRow::FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TSharedPtr<FDMXTreeNodeBase> InNodePtr, bool bInIsRootCategory, TWeakPtr<SDMXEntityList> InEditorList);

	virtual void SetContent(TSharedRef< SWidget > InContent) override;
	virtual void SetRowContent(TSharedRef< SWidget > InContent) override;

	virtual const FSlateBrush* GetBorder() const { return nullptr; }
	/* Get the node used by the row Widget */
	virtual TSharedPtr<FDMXCategoryTreeNode> GetNode() const { return TreeNodePtr.Pin(); };

protected:
	//~ SWidget interface begin
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	//~ SWidget interface end

protected:
	/** Pointer to node we represent */
	TWeakPtr<FDMXCategoryTreeNode> TreeNodePtr;
	TWeakPtr<SDMXEntityList> EditorListPtr;

private:
	const FSlateBrush* GetBackgroundImage() const;

	TSharedPtr<SBorder> ContentBorder;
};

class SDMXEntityRow : public SDMXTableRowType
{
public:
	DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnEntityDragged, TSharedPtr<FDMXTreeNodeBase>, const FPointerEvent&);
	DECLARE_DELEGATE_RetVal(FText, FOnGetFilterText);
	DECLARE_DELEGATE_OneParam(FOnAutoAssignChannelStateChanged, bool);

	SLATE_BEGIN_ARGS(SDMXEntityRow)
		: _OnEntityDragged()
		, _OnGetFilterText()
		, _OnAutoAssignChannelStateChanged()
		{}

		SLATE_EVENT(FOnEntityDragged, OnEntityDragged)
		SLATE_EVENT(FOnGetFilterText, OnGetFilterText)
		SLATE_EVENT(FOnAutoAssignChannelStateChanged, OnAutoAssignChannelStateChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FDMXTreeNodeBase> InNodePtr, TSharedPtr<STableViewBase> InOwnerTableView, TWeakPtr<SDMXEntityList> InEditorList);

	/* Get the node used by the row Widget */
	virtual TSharedPtr<FDMXEntityBaseTreeNode> GetNode() const { return TreeNodePtr.Pin(); };

	FOnAutoAssignChannelStateChanged& GetOnAutoAssignChannelStateChanged() { return OnAutoAssignChannelStateChanged; }

protected:
	/** Data accessors */
	FText GetDisplayText() const;
	FText GetStartingChannelLabel() const;
	FText GetEndingChannelLabel() const;

	/**  Called when the auto-assign channel check-box state is changed */
	void OnAutoAssignChannelBoxStateChanged(ECheckBoxState NewState);

	//~ SWidget interface begin
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	//~ SWidget interface end

protected:
	/** Pointer to node we represent */
	TWeakPtr<FDMXEntityBaseTreeNode> TreeNodePtr;

	TWeakPtr<SDMXEntityList> EditorListPtr;

private:
	/** Verifies the name of the component when changing it */
	bool OnNameTextVerifyChanged(const FText& InNewText, FText& OutErrorMessage);

	/** Commits the new name of the component */
	void OnNameTextCommit(const FText& InNewName, ETextCommit::Type InTextCommit);

	/** Returns the tooltip text for this row */
	FText GetToolTipText() const;

	/** Drag-drop handlers */
	FReply HandleOnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

	/** Get the current filter text from the search box */
	FText GetFilterText() const;

	/** Get the icon for the Entity usability status. If it's all good, it's an emtpy image. */
	const FSlateBrush* GetStatusIcon() const;
	/** Get the tool tip text for the status icon */
	FText GetStatusToolTip() const;

private:
	FOnEntityDragged OnEntityDragged;
	FOnGetFilterText OnGetFilterText;
	FText StatusIconToolTip;

	FOnAutoAssignChannelStateChanged OnAutoAssignChannelStateChanged;

	TSharedPtr<SInlineEditableTextBlock> InlineRenameWidget;
};

//////////////////////////////////////////////////////////////////////////
// SDMXEntityListBase

/** DMX entities list editor widget */
class SDMXEntityList
	: public SCompoundWidget, public FEditorUndoClient
{
public:
	DECLARE_DELEGATE_OneParam(FOnSelectionUpdated, TArray<TSharedPtr<FDMXTreeNodeBase>>);
	DECLARE_DELEGATE_OneParam(FOnItemDoubleClicked, const TSharedPtr<FDMXTreeNodeBase>);

	SLATE_BEGIN_ARGS(SDMXEntityList)
		: _OnSelectionUpdated()
		{}

		SLATE_ARGUMENT(TWeakPtr<FDMXEditor>,	DMXEditor)
		SLATE_EVENT(FOnSelectionUpdated,		OnSelectionUpdated)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs, const TSubclassOf<UDMXEntity> InListType);

	~SDMXEntityList();

	/** SWidget interface */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent);

	bool IsListEmpty() const { return EntitiesCount == 0; }

	/** Cut selected node(s) */
	void OnCutSelectedNodes();
	bool CanCutNodes() const;

	/** Copy selected node(s) */
	void OnCopySelectedNodes();
	bool CanCopyNodes() const;

	/** Pastes previously copied node(s) */
	void OnPasteNodes();
	bool CanPasteNodes() const;

	/** Callbacks to duplicate the selected component */
	bool CanDuplicateNodes() const;
	void OnDuplicateNodes();

	/** Removes existing selected component nodes from the SCS */
	void OnDeleteNodes();
	bool CanDeleteNodes() const;

	/** Requests a rename on the selected Entity. */
	void OnRenameNode();
	/** Checks to see if renaming is allowed on the selected Entity */
	bool CanRenameNode() const;

	/** Get only the valid selected entities */
	TArray<UDMXEntity*> GetSelectedEntities() const;
	/** Selects an item by name */
	void SelectItemByName(const FString& ItemName, ESelectInfo::Type SelectInfo = ESelectInfo::Direct);
	/** Selects an item by Entity */
	void SelectItemByEntity(const UDMXEntity* InEntity, ESelectInfo::Type SelectInfo = ESelectInfo::Direct);
	/** Selects items by Entity */
	void SelectItemsByEntity(const TArray<UDMXEntity*>& InEntities, ESelectInfo::Type SelectInfo = ESelectInfo::Direct);
	/** Called when selection in the tree changes */
	void OnTreeSelectionChanged(TSharedPtr<FDMXTreeNodeBase> InSelectedNodePtr, ESelectInfo::Type SelectInfo);

	/** Update any associated selection from the passed in nodes */
	void UpdateSelectionFromNodes(const TArray<TSharedPtr<FDMXTreeNodeBase>>& SelectedNodes);

	/**
	 * Set the expansion state of a node
	 *
	 * @param InNodeToChange	The node to be expanded/collapsed
	 * @param bIsExpanded		True to expand the node, false to collapse it
	 */
	void SetNodeExpansionState(TSharedPtr<FDMXTreeNodeBase> InNodeToChange, const bool bIsExpanded);

	/** Refresh the tree control to reflect changes in the editor */
	void UpdateTree(bool bRegenerateTreeNodes = true);

	/** Gets current filter from the FilterBox */
	FText GetFilterText() const;

	/** Gets the DMX Library object being edited */
	UDMXLibrary* GetDMXLibrary() const;

	TSubclassOf<UDMXEntity> GetListType() const;

	//~ Begin FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }
	// End of FEditorUndoClient

protected:

	/** Called to display context menu when right clicking on an Entity */
	TSharedPtr< SWidget > OnContextMenuOpen();
	void BuildAddNewMenu(FMenuBuilder& MenuBuilder);

	/** Callback when an entity item is scrolled into view */
	void OnItemScrolledIntoView(TSharedPtr<FDMXTreeNodeBase> InItem, const TSharedPtr<ITableRow>& InWidget);

	/** Returns the set of expandable nodes that are currently collapsed in the UI */
	void GetCollapsedNodes(TSet<TSharedPtr<FDMXTreeNodeBase>>& OutCollapsedNodes, TSharedPtr<FDMXTreeNodeBase> InParentNodePtr = nullptr) const;

	/** Helper method to recursively find a tree node for the given DMX Entity starting at the given tree node */
	TSharedPtr<FDMXTreeNodeBase> FindTreeNode(const UDMXEntity* InEntity, TSharedPtr<FDMXTreeNodeBase> InStartNodePtr = nullptr) const;
	/** Helper method to recursively find a tree node with the given name starting at the given tree node */
	TSharedPtr<FDMXTreeNodeBase> FindTreeNode(const FText& InName, TSharedPtr<FDMXTreeNodeBase> InStartNodePtr = nullptr) const;

	/**
	 * Creates a new category node directly under the passed parent or just retrieves it if existent.
	 * If InParentNodePtr is null, a root category node is created/retrieved.
	 */
	TSharedPtr<FDMXTreeNodeBase> GetOrCreateCategoryNode(const FDMXTreeNodeBase::ECategoryType InCategoryType, const FText InCategoryName, void* CategoryValuePtr, TSharedPtr<FDMXTreeNodeBase> InParentNodePtr = nullptr, const FText& InToolTip = FText::GetEmpty());

	/** Called when the active tab in the editor changes */
	void OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated);

	/** Searches this widget's parents to see if it's a child of InDockTab */
	bool IsInTab(TSharedPtr<SDockTab> InDockTab) const;

protected:
	/** Pointer back to the DMXEditor tool that owns us */
	TWeakPtr<FDMXEditor> DMXEditor;

	/** Entity type we're editing. Might change the list layout */
	TSubclassOf<UDMXEntity> ListType;

	/** Tree widget */
	TSharedPtr<STreeView<TSharedPtr<FDMXTreeNodeBase>>> EntitiesTreeWidget;

	/** Command list for handling actions in the SSCSEditor */
	TSharedPtr< FUICommandList > CommandList;

	/** Delegate to invoke on selection update. */
	FOnSelectionUpdated OnSelectionUpdated;

	/**
	 * Dummy root tree node.
	 * It's not added to the tree, but the main categories and all their children
	 * (entity and sub-category nodes) belong to it to make recursive searching algorithms nicer.
	 */
	TSharedPtr<FDMXTreeNodeBase> RootNode;

private:
	/** Empty Nodes array and create a FDMXEntityListTreeNode for each relevant entity and category */
	void InitializeNodes();

	TSharedPtr<FDMXEntityBaseTreeNode> CreateEntityTreeNode(UDMXEntity* Entity);

	FReply OnAddNewClicked();

	/** Callback when the filter is changed, forces the action tree(s) to filter */
	void OnFilterTextChanged(const FText& InFilterText);

	/**
	 * Compares the filter bar's text with the item's component name. Use
	 * bRecursive to refresh the state of child nodes as well. Returns true if
	 * the node is set to be filtered out
	 */
	bool RefreshFilteredState(TSharedPtr<FDMXTreeNodeBase> TreeNode, bool bRecursive);

	/** Used by tree control - make a widget for a table row from a node */
	TSharedRef<ITableRow> MakeNodeWidget(TSharedPtr<FDMXTreeNodeBase> InNodePtr, const TSharedRef<STableViewBase>& OwnerTable);

	/** Used by tree control - get children for a specified node */
	void OnGetChildrenForTree(TSharedPtr<FDMXTreeNodeBase> InNodePtr, TArray<TSharedPtr<FDMXTreeNodeBase>>& OutChildren);

	/** Expand all categories during filtering and resets nodes expansion state after filtering is cleared */
	void UpdateNodesExpansion(TSharedRef<FDMXTreeNodeBase> InRootNode, bool bFilterIsEmpty);

	/** Handler for expanding/collapsing items */
	void OnItemExpansionChanged(TSharedPtr<FDMXTreeNodeBase> InNodePtr, bool bInExpansionState);

	/** Handler for when an entity from the list is dragged */
	FReply OnEntityDragged(TSharedPtr<FDMXTreeNodeBase> InNodePtr, const FPointerEvent& MouseEvent);

	/** Called when the user selects a Fixture Type to create a Fixture Patch from */
	void OnFixtureTypeSelected(UDMXEntity* InSelectedFixtureType);
	/** Called by the editor to set a base name for an Entity about to be created */
	void OnEditorGetBaseNameForNewFixturePatch(TSubclassOf<UDMXEntity> InEntityClass, FString& OutBaseName, UDMXEntityFixtureType* InSelectedFixtureType);
	/** Called by the editor to setup the properties of a new Entity before its selection and renaming in the editor */
	void OnEditorSetupNewFixturePatch(UDMXEntity* InNewEntity, UDMXEntityFixtureType* InSelectedFixtureType);

	/** Called when Auto Assign Channel is changed for a Patch */
	void OnAutoAssignChannelStateChanged(bool NewState, TSharedPtr<FDMXTreeNodeBase> InNodePtr);

private:
	/** The filter box that handles filtering entities */
	TSharedPtr< SSearchBox > FilterBox;

	/** Handle to the registered OnActiveTabChanged delegate */
	FDelegateHandle OnActiveTabChangedDelegateHandle;
	
	FDelegateHandle OnGetBaseNameForNewEntityHandle;
	FDelegateHandle OnSetupNewEntityHandle;

	/** Gate to prevent changing the selection while selection change is being broadcast. */
	bool bUpdatingSelection;

	/** The green Add Button. We need to reference it in Fixture Patches tab to update its list */
	TSharedPtr<SComboButton> AddComboButton;
	TSharedPtr<SDMXEntityDropdownMenu<UDMXEntityFixtureType>> AddButtonDropdownList;

	/** Keeps the current number of entities for quick checking */
	int32 EntitiesCount;
};
