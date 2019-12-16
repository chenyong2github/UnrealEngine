// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Styling/SlateColor.h"
#include "Widgets/SWidget.h"
#include "Misc/FrameRate.h"
#include "Tree/ICurveEditorTreeItem.h"
#include "CurveEditorTypes.h"

class FMenuBuilder;
class FSequencer;
class FSequencerNodeTree;
class FSequencerDisplayNodeDragDropOp;
class FSequencerObjectBindingNode;
class IKeyArea;
class ISequencerTrackEditor;
class SSequencerTreeViewRow;
class UMovieSceneTrack;
class UMovieSceneSection;
struct FSlateBrush;
enum class EItemDropZone;

/**
 * Structure used to define padding for a particular node.
 */
struct FNodePadding
{
	FNodePadding(float InUniform) : Top(InUniform), Bottom(InUniform) { }
	FNodePadding(float InTop, float InBottom) : Top(InTop), Bottom(InBottom) { }

	/** @return The sum total of the separate padding values */
	float Combined() const
	{
		return Top + Bottom;
	}

	/** Padding to be applied to the top of the node */
	float Top;

	/** Padding to be applied to the bottom of the node */
	float Bottom;
};

namespace ESequencerNode
{
	enum Type
	{
		/* Top level object binding node */
		Object,
		/* Area for tracks */
		Track,
		/* Area for keys inside of a section */
		KeyArea,
		/* Displays a category */
		Category,
		/* Symbolic root node */
		Root,
		/* Folder node */
		Folder
	};
}

enum class EDisplayNodeSortType : uint8
{
	Folders,
	Tracks,
	ObjectBindings,
	CameraCuts,
	Shots,
	Undefined,

	NUM,
};

/**
 * Base Sequencer layout node.
 */
class FSequencerDisplayNode
	: public TSharedFromThis<FSequencerDisplayNode>
	, public ICurveEditorTreeItem
{
public:

	/** The serial number taken from the tree last time this node was encountered during a refresh.
	 * When != FSequencerNodeTree::SerialNumber this node should be removed from all relevant structures */
	uint32 TreeSerialNumber;

	/**
	 * Create and initialize a new instance.
	 * 
	 * @param InNodeName	The name identifier of then node
	 * @param InParentTree	The tree this node is in
	 */
	FSequencerDisplayNode( FName InNodeName, FSequencerNodeTree& InParentTree);

	/** Virtual destructor. */
	virtual ~FSequencerDisplayNode(){}

public:

	/** 
	 * Finds any parent object binding node above this node in the hierarchy
	 *
	 * @return the parent node, or nullptr if no object binding is found
	 */
	TSharedPtr<FSequencerObjectBindingNode> FindParentObjectBindingNode() const;

	/** 
	 * Finds this display node's closest parent object binding GUID, or an empty FGuid if it there is none
	 */
	FGuid GetObjectGuid() const;

	/**
	 * Check whether this node is displayed at the root of the tree on the UI (ie, is part of the FSequencerNodeTree::GetRootNodes array)
	 * @note: this is not the same as the node being the symbolic root node (FSequencerNodeTree::GetRootNode), of which there is only one.
	 */
	bool IsRootNode() const;

	/**
	 * Checks whether this node's parent is still relevant to the specified serial number
	 */
	bool IsParentStillRelevant(uint32 SerialNumber) const;

	/**
	 * Retrieve the sort type of this node
	 */
	EDisplayNodeSortType GetSortType() const
	{
		return SortType;
	}

	/**
	 * @return The type of node this is
	 */
	virtual ESequencerNode::Type GetType() const = 0;

	/** @return Whether or not this node can be selected */
	virtual bool IsSelectable() const
	{
		return true;
	}

	/**
	 * @return The desired height of the node when displayed
	 */
	virtual float GetNodeHeight() const = 0;
	
	/**
	 * @return The desired padding of the node when displayed
	 */
	virtual FNodePadding GetNodePadding() const = 0;

	/**
	 * Whether the node can be renamed.
	 *
	 * @return true if this node can be renamed, false otherwise.
	 */
	virtual bool CanRenameNode() const = 0;

	/**
	 * @return The localized display name of this node
	 */
	virtual FText GetDisplayName() const = 0;

	/**
	* @return the color used to draw the display name.
	*/
	virtual FLinearColor GetDisplayNameColor() const;

	/**
	 * @return the text to display for the tool tip for the display name. 
	 */
	virtual FText GetDisplayNameToolTipText() const;

	/**
	 * Return whether the new display name is valid for this node.
	 */
	virtual bool ValidateDisplayName(const FText& NewDisplayName, FText& OutErrorMessage) const;

	/**
	 * Set the node's display name.
	 *
	 * @param NewDisplayName the display name to set.
	 */
	virtual void SetDisplayName(const FText& NewDisplayName) = 0;

	/**
	 * @return Whether this track should be drawn as dim 
	 */
	virtual bool IsDimmed() const;

	/**
	 * @return Whether this node handles resize events
	 */
	virtual bool IsResizable() const
	{
		return false;
	}

	/**
	 * Resize this node
	 */
	virtual void Resize(float NewSize)
	{
		
	}

	/**
	 * @return What is the sorting order of this node relative to it's siblings in the node tree
	 */
	virtual int32 GetSortingOrder() const
	{
		return 0;
	}

	/**
	 * Sets the node's sorting order relative to it's siblings in the node tree. Does not take effect until node tree is refreshed.
	 * @param InSortingOrder The resulting sorting order the object should have relative to it's siblings in the Sequencer tree view.
	 */
	virtual void SetSortingOrder(const int32 InSortingOrder)
	{

	}

	/**
	 * Calls Modify on the underlying data structure before calling SetSortingOrder.
	 */
	virtual void ModifyAndSetSortingOrder(const int32 InSortingOrder)
	{

	}

	/**
	 * Generates a container widget for tree display in the animation outliner portion of the track area
	 * 
	 * @return Generated outliner container widget
	 */
	virtual TSharedRef<SWidget> GenerateContainerWidgetForOutliner(const TSharedRef<SSequencerTreeViewRow>& InRow);

	/**
	 * Customizes an outliner widget that is to represent this node
	 * 
	 * @return Content to display on the outliner node
	 */
	virtual TSharedRef<SWidget> GetCustomOutlinerContent();

	/**
	 * Creates an additional label widget to appear immediately beside this node's label on the tree
	 * 
	 * @return Content to display on the outliner node
	 */
	virtual TSharedPtr<SWidget> GetAdditionalOutlinerLabel() { return nullptr; }

	/**
	 * Generates a widget for display in the section area portion of the track area
	 * 
	 * @param ViewRange	The range of time in the sequencer that we are displaying
	 * @return Generated outliner widget
	 */
	virtual TSharedRef<SWidget> GenerateWidgetForSectionArea( const TAttribute<TRange<double>>& ViewRange );

	/**
	 * Gets an icon that represents this sequencer display node
	 * 
	 * @return This node's representative icon
	 */
	virtual const FSlateBrush* GetIconBrush() const;

	/**
	 * Get a brush to overlay on top of the icon for this node
	 * 
	 * @return An overlay brush, or nullptr
	 */
	virtual const FSlateBrush* GetIconOverlayBrush() const;

	/**
	 * Gets the color for the icon brush
	 * 
	 * @return This node's representative color
	 */
	virtual FSlateColor GetIconColor() const;

	/**
	 * Get the tooltip text to display for this node's icon
	 * 
	 * @return Text to display on the icon
	 */
	virtual FText GetIconToolTipText() const;

	// ICurveEditorTreeItem interface
	virtual TSharedPtr<SWidget> GenerateCurveEditorTreeWidget(const FName& InColumnName, TWeakPtr<FCurveEditor> InCurveEditor, FCurveEditorTreeItemID InTreeItemID, const TSharedRef<ITableRow>& InTableRow) override;
	virtual void CreateCurveModels(TArray<TUniquePtr<FCurveModel>>& OutCurveModels) override;
	virtual bool PassesFilter(const FCurveEditorTreeFilter* InFilter) const override;

	/**
	 * Get the display node that is ultimately responsible for constructing a section area widget for this node.
	 * Could return this node itself, or a parent node
	 */
	TSharedPtr<FSequencerDisplayNode> GetSectionAreaAuthority() const;

	FFrameRate GetTickResolution() const;

	/**
	 * @return the path to this node starting with the outermost parent
	 */
	FString GetPathName() const;

	/** Summon context menu */
	TSharedPtr<SWidget> OnSummonContextMenu();

	/** What sort of context menu this node summons */
	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder);

	/** Can this node show the add object bindings menu? */
	virtual bool CanAddObjectBindingsMenu() const { return false; }

	/** Can this node show the add tracks menu? */
	virtual bool CanAddTracksMenu() const { return false; }

	/**
	 * @return The name of the node (for identification purposes)
	 */
	FName GetNodeName() const
	{
		return NodeName;
	}

	/**
	 * @return The number of child nodes belonging to this node
	 */
	uint32 GetNumChildren() const
	{
		return ChildNodes.Num();
	}

	/**
	 * @return A List of all Child nodes belonging to this node
	 */
	const TArray<TSharedRef<FSequencerDisplayNode>>& GetChildNodes() const
	{
		return ChildNodes;
	}

	/**
	 * Iterate this entire node tree, child first.
	 *
	 * @param 	InPredicate			Predicate to call for each node, returning whether to continue iteration or not
	 * @param 	bIncludeThisNode	Whether to include this node in the iteration, or just children
	 * @return  true where the client prematurely exited the iteration, false otherwise
	 */
	bool Traverse_ChildFirst(const TFunctionRef<bool(FSequencerDisplayNode&)>& InPredicate, bool bIncludeThisNode = true);

	/**
	 * Iterate this entire node tree, parent first.
	 *
	 * @param 	InPredicate			Predicate to call for each node, returning whether to continue iteration or not
	 * @param 	bIncludeThisNode	Whether to include this node in the iteration, or just children
	 * @return  true where the client prematurely exited the iteration, false otherwise
	 */
	bool Traverse_ParentFirst(const TFunctionRef<bool(FSequencerDisplayNode&)>& InPredicate, bool bIncludeThisNode = true);

	/**
	 * Iterate any visible portions of this node's sub-tree, child first.
	 *
	 * @param 	InPredicate			Predicate to call for each node, returning whether to continue iteration or not
	 * @param 	bIncludeThisNode	Whether to include this node in the iteration, or just children
	 * @return  true where the client prematurely exited the iteration, false otherwise
	 */
	bool TraverseVisible_ChildFirst(const TFunctionRef<bool(FSequencerDisplayNode&)>& InPredicate, bool bIncludeThisNode = true);

	/**
	 * Iterate any visible portions of this node's sub-tree, parent first.
	 *
	 * @param 	InPredicate			Predicate to call for each node, returning whether to continue iteration or not
	 * @param 	bIncludeThisNode	Whether to include this node in the iteration, or just children
	 * @return  true where the client prematurely exited the iteration, false otherwise
	 */
	bool TraverseVisible_ParentFirst(const TFunctionRef<bool(FSequencerDisplayNode&)>& InPredicate, bool bIncludeThisNode = true);

	/**
	 * Sort this node's immediate children using the persistent user-specifid reordering if possible
	 */
	void SortImmediateChildren();

	/**
	 * Resort this node's immediate children, resetting any persistent user-specified reordering
	 */
	void ResortImmediateChildren();

	/**
	 * @return The parent of this node. Will return null if this node is part of the FSequencerNodeTree::GetRootNodes array.
	 */
	TSharedPtr<FSequencerDisplayNode> GetParent() const
	{
		TSharedPtr<FSequencerDisplayNode> Pinned = ParentNode.Pin();
		return (Pinned && Pinned->GetType() != ESequencerNode::Root) ? Pinned : nullptr;
	}

	/**
	 * @return The parent of this node, or the symbolic root node if this node is part of the FSequencerNodeTree::GetRootNodes array.
	 */
	TSharedPtr<FSequencerDisplayNode> GetParentOrRoot() const
	{
		return ParentNode.Pin();
	}

	/**
	 * @return The outermost parent of this node, ignoring the symbolic root node (ie, will never return FSequencerNodeTree::GetRootNode()
	 */
	TSharedRef<FSequencerDisplayNode> GetOutermostParent()
	{
		TSharedPtr<FSequencerDisplayNode> Parent = GetParent();
		return Parent.IsValid() ? Parent->GetOutermostParent() : AsShared();
	}
	
	/** Gets the sequencer that owns this node */
	FSequencer& GetSequencer() const;
	
	/** Gets the parent tree that this node is in */
	FSequencerNodeTree& GetParentTree() const
	{
		return ParentTree;
	}

	/** Gets all the key area nodes recursively, including this node if applicable */
	virtual void GetChildKeyAreaNodesRecursively(TArray<TSharedRef<class FSequencerSectionKeyAreaNode>>& OutNodes) const;

	/**
	 * @return The base node this node belongs to, for collections of tracks that are part of an object
	 */
	FSequencerDisplayNode* GetBaseNode() const;

	/**
	 * Set whether this node is expanded or not
	 */
	void SetExpansionState(bool bInExpanded);

	/**
	 * @return Whether or not this node is expanded
	 */
	bool IsExpanded() const;

	/**
	 * Called by FSequencer to update the cached pinned state
	 */
	void UpdateCachedPinnedState(bool bParentIsPinned = false);

	/**
	 * @return Whether or not this node is pinned
	 */
	bool IsPinned() const;

	/**
	 * Toggle whether or not this node is pinned
	 */
	void TogglePinned();

	/**
	 * If this node is pinned, unpin it.
	 */
	void Unpin();

	/**
	 * @return Whether this node is explicitly hidden from the view or not
	 */
	bool IsHidden() const;

	/**
	 * @return Whether this node should be displayed on the tree view
	 */
	bool IsVisible() const;

	/**
	 * Check whether the node's tree view or track area widgets are hovered by the user's mouse.
	 *
	 * @return true if hovered, false otherwise. */
	bool IsHovered() const;

	/**
	 * Called when the tree has been refreshed and this node has its new position in the hierarchy assigned
	 */
	void OnTreeRefreshed(float InVirtualTop, float InVirtualBottom);

	/** @return this node's virtual offset from the top of the tree, irrespective of expansion states */
	float GetVirtualTop() const
	{
		return VirtualTop;
	}
	
	/** @return this node's virtual offset plus its virtual height, irrespective of expansion states */
	float GetVirtualBottom() const
	{
		return VirtualBottom;
	}

	DECLARE_EVENT(FSequencerDisplayNode, FRequestRenameEvent);
	FRequestRenameEvent& OnRenameRequested() { return RenameRequestedEvent; }

	/** 
	 * Returns whether or not this node can be dragged. 
	 */
	virtual bool CanDrag() const { return false; }

	/**
	 * Determines if there is a valid drop zone based on the current drag drop operation and the zone the items were dragged onto.
	 */
	virtual TOptional<EItemDropZone> CanDrop( FSequencerDisplayNodeDragDropOp& DragDropOp, EItemDropZone ItemDropZone ) const { return TOptional<EItemDropZone>(); }

	/**
	 * Handles a drop of items onto this display node.
	 */
	virtual void Drop( const TArray<TSharedRef<FSequencerDisplayNode>>& DraggedNodes, EItemDropZone DropZone ) { }


public:

	/**
	 * Assigns the parent of this node and adds it the the parent's child node list, removing it from its current parent's children if necessary.
	 * 
	 * @param InParent           This node's new parent node
	 * @param DesiredChildIndex  (optional) An optional index at which this node should be inserted to its new parent node's children array or INDEX_NONE to add it to the end
	 */
	void SetParent(TSharedPtr<FSequencerDisplayNode> InParent, int32 DesiredChildIndex = INDEX_NONE);

	/** Directly assigns the parent of this node without performing any other operation. Should only be used with care when child/parent relationships can be guaranteed. */
	void SetParentDirectly(TSharedPtr<FSequencerDisplayNode> InParent);

	/**
	 * Move a child of this node from one index to another - does not re-arrange any other children
	 * 
	 * @param InChildIndex       The index of the child node to move from
	 * @param InDesiredNewIndex  The index to move to - must be a valid index within this node's child array.
	 */
	void MoveChild(int32 InChildIndex, int32 InDesiredNewIndex);

	/** Request that this node be reinitialized when the tree next refreshes. Currently Initialization only affects default expansion states. */
	void RequestReinitialize()
	{
		bHasBeenInitialized = false;
	}

private:

	/** Callback for executing a "Rename Node" context menu action. */
	void HandleContextMenuRenameNodeExecute();

	/** Callback for determining whether a "Rename Node" context menu action can execute. */
	bool HandleContextMenuRenameNodeCanExecute() const;

protected:

	/** The virtual offset of this item from the top of the tree, irrespective of expansion states. */
	float VirtualTop;

	/** The virtual offset + virtual height of this item, irrespective of expansion states. */
	float VirtualBottom;

protected:

	/** The parent of this node*/
	TWeakPtr<FSequencerDisplayNode> ParentNode;

	/** List of children belonging to this node */
	TArray<TSharedRef<FSequencerDisplayNode>> ChildNodes;

	/** Parent tree that this node is in */
	FSequencerNodeTree& ParentTree;

	/** The name identifier of this node */
	FName NodeName;

	/** Whether or not the node is expanded */
	bool bExpanded;

	/** Whether or not the node is pinned */
	bool bPinned;

	/** Cached value of whether this node or one of it's parents is pinned */
	bool bInPinnedBranch;

	/** Event that is triggered when rename is requested */
	FRequestRenameEvent RenameRequestedEvent;

	/** The kind of thing that this node represents for sorting purposes */
	EDisplayNodeSortType SortType;

private:

	/** Set to true when this node has been completely initialized for the first time */
	bool bHasBeenInitialized;
};
