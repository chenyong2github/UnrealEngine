// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "UObject/ObjectKey.h"
#include "Tree/CurveEditorTree.h"
#include "DisplayNodes/SequencerDisplayNode.h"
#include "SectionHandle.h"
#include "MovieSceneSequence.h"

class FSequencer;
class FCurveEditor;
class FSequencerDisplayNode;
class FSequencerObjectBindingNode;
class FSequencerSectionKeyAreaNode;
class FSequencerTrackNode;
class FSequencerFolderNode;
class ISequencerTrackEditor;
class UMovieSceneFolder;
class UMovieSceneTrack;
class UMovieScene;
struct FMovieSceneBinding;
struct FCurveEditorTreeItemID;
class FSequencerTrackFilter;
class FSequencerTrackFilterCollection;
class FSequencerTrackFilter_LevelFilter;

/**
 * Represents a tree of sequencer display nodes, used to populate the Sequencer UI with MovieScene data
 */
class FSequencerNodeTree : public TSharedFromThis<FSequencerNodeTree>
{
public:
	DECLARE_MULTICAST_DELEGATE(FOnUpdated);

public:

	FSequencerNodeTree(FSequencer& InSequencer);
	
	~FSequencerNodeTree();

	/**
	 * Updates the tree with sections from a MovieScene
	 */
	void Update();

	/**
	 * Access this tree's symbolic root node
	 */
	TSharedRef<FSequencerDisplayNode> GetRootNode() const;

	/**
	 * @return The root nodes of the tree
	 */
	const TArray< TSharedRef<FSequencerDisplayNode> >& GetRootNodes() const;

	/** @return Whether or not there is an active filter */
	bool HasActiveFilter() const;

	/** 
	 * Checks if filters should be updated on track value changes, and if so updates them.
	 * @return Whether the filtered node list was modified
	 */
	bool UpdateFiltersOnTrackValueChanged();

	/**
	 * Returns whether or not a node is filtered
	 *
	 * @param Node	The node to check if it is filtered
	 */
	bool IsNodeFiltered( const TSharedRef<const FSequencerDisplayNode> Node ) const;

	/**
	 * Schedules an update of all filters
	 */
	void RequestFilterUpdate() { bFilterUpdateRequested = true; }

	/**
	 * @return Whether there is a filter update scheduled
	 */
	bool NeedsFilterUpdate() const { return bFilterUpdateRequested; }

	/**
	 * Filters the nodes based on the passed in filter terms
	 *
	 * @param InFilter	The filter terms
	 */
	void FilterNodes( const FString& InFilter );

	/** Called when the active MovieScene's node group colletion has been modifed */
	void NodeGroupsCollectionChanged();

	/**
	 * Unpins any pinned nodes in this tree
	 */
	void UnpinAllNodes();

	/**
	 * @return All nodes in a flat array
	 */
	TArray< TSharedRef<FSequencerDisplayNode> > GetAllNodes() const;

	/** Gets the parent sequencer of this tree */
	FSequencer& GetSequencer() {return Sequencer;}

	/**
	 * Saves the expansion state of a display node
	 *
	 * @param Node		The node whose expansion state should be saved
	 * @param bExpanded	The new expansion state of the node
	 */
	void SaveExpansionState( const FSequencerDisplayNode& Node, bool bExpanded );

	/**
	 * Gets the saved expansion state of a display node
	 *
	 * @param Node	The node whose expansion state may have been saved
	 * @return true if the node should be expanded, false otherwise	
	 */
	bool GetSavedExpansionState( const FSequencerDisplayNode& Node ) const;

	/**
	 * Get the default expansion state for the specified node, where its state has not yet been saved
	 *
	 * @return true if the node is to be expanded, false otherwise
	 */
	bool GetDefaultExpansionState( const FSequencerDisplayNode& Node ) const;

	/**
	 * Saves the pinned state of a display node
	 *
	 * @param Node		The node whose pinned state should be saved
	 * @param bPinned	The new pinned state of the node
	 */
	void SavePinnedState( const FSequencerDisplayNode& Node, bool bPinned );

	/**
	 * Gets the saved pinned state of a display node
	 *
	 * @param Node	The node whose pinned state may have been saved
	 * @return true if the node should be pinned, false otherwise	
	 */
	bool GetSavedPinnedState( const FSequencerDisplayNode& Node ) const;

	/**
	 * Set the single hovered node in the tree
	 */
	void SetHoveredNode(const TSharedPtr<FSequencerDisplayNode>& InHoveredNode);

	/**
	 * Get the single hovered node in the tree, possibly nullptr
	 */
	const TSharedPtr<FSequencerDisplayNode>& GetHoveredNode() const;

	/*
	 * Find the object binding node with the specified GUID
	 */
	TSharedPtr<FSequencerObjectBindingNode> FindObjectBindingNode(const FGuid& BindingID) const;

	/*
	 * Gets a multicast delegate which is called whenever the node tree has been updated.
	 */
	FOnUpdated& OnUpdated() { return OnUpdatedDelegate; }

	/** Make the contents of the given node have the root as their parent again instead of their current parent. */
	void MoveDisplayNodeToRoot(TSharedRef<FSequencerDisplayNode>& Node);

	/** Sorts all nodes and their descendants by category then alphabetically.*/
	void SortAllNodesAndDescendants();

	/**
	 * Attempt to find a curve editor tree item ID for the specified display node
	 *
	 * @param DisplayNode The node to find a curve editor tree item for
	 * @return The ID of the tree item for this display node in the curve editor, or FCurveEditorTreeItemID::Invalid() if one was not found.
	 */
	FCurveEditorTreeItemID FindCurveEditorTreeItem(TSharedRef<FSequencerDisplayNode> DisplayNode) const;

	/**
	 * Attempt to get a node at the specified path
	 *
	 * @param NodePath The path of the node to search for
	 * @return The node located at NodePath, or nullptr if not found
	 */
	FSequencerDisplayNode* GetNodeAtPath(const FString& NodePath) const;

public:

	/**
	 * Finds the section handle relating to the specified section object, or Null if one was not found (perhaps, if it's filtered away)
	 */
	TOptional<FSectionHandle> GetSectionHandle(const UMovieSceneSection* Section) const;

	void AddFilter(TSharedRef<FSequencerTrackFilter> TrackFilter);
	int32 RemoveFilter(TSharedRef<FSequencerTrackFilter> TrackFilter);
	void RemoveAllFilters();
	bool IsTrackFilterActive(TSharedRef<FSequencerTrackFilter> TrackFilter) const;

	void AddLevelFilter(const FString& LevelName);
	void RemoveLevelFilter(const FString& LevelName);
	bool IsTrackLevelFilterActive(const FString& LevelName) const;

	void ToggleSelectedNodesSolo();
	bool IsNodeSolo(const FSequencerDisplayNode* Node) const;
	
	/** Returns whether any of the nodes in this tree are marked solo */
	bool HasSoloNodes() const;

	void ToggleSelectedNodesMute();
	bool IsNodeMute(const FSequencerDisplayNode* Node) const;

	/** Returns whether any of the currently selected nodes are marked solo */
	bool IsSelectedNodesSolo() const;
	
	/** Returns whether any of the currently selected nodes are muted */
	bool IsSelectedNodesMute() const;

private:

	/** Returns whether this NodeTree should only display selected nodes */
	bool ShowSelectedNodesOnly() const;

	/** Population algorithm utilities */
	void RefreshNodes(UMovieScene* MovieScene);

	enum class ETrackType { Master, Object };

	/**
	 * Create or update a track node for the specified track object, updating its serial number.
	 *
	 * @param Track                   Pointer to the track to create or update a node for
	 * @param TrackType               Whether this track is a master track or contained within an object
	 * @return A shared pointer to a track node with its serial number updated. Will return nullptr for tracks that are forcibly hidden through FSequencer::IsTrackVisible.
	 */
	TSharedPtr<FSequencerTrackNode> CreateOrUpdateTrack(UMovieSceneTrack* Track, ETrackType TrackType);

	/**
	 * Create or update a folder node for the specified folder and all its decendent child folders and object bindings, updating their serial numbers in the process
	 *
	 * @param Folder                  Pointer to the movie scene folder to create a node for
	 * @param AllBindings             A map from guid to binding pointer for all the bindings in the sequence
	 * @param ChildToParentBinding    Child to parent GUID map used for creating parent items
	 * @param OutChildToParentMap     Pointer to a map that should be populated with child->parent relationships to be set up after creation of all nodes
	 * @return A shared reference to a folder node. The resulting node's serial number will always be up-to-date, as will all its child folders, and any immediate child object bindings.
	 */
	TSharedRef<FSequencerFolderNode> CreateOrUpdateFolder(UMovieSceneFolder* Folder, const TSortedMap<FGuid, const FMovieSceneBinding*>& AllBindings, const TSortedMap<FGuid, FGuid>& ChildToParentBinding, TMap<FSequencerDisplayNode*, TSharedPtr<FSequencerDisplayNode>>* OutChildToParentMap);

	/**
	 * Find an existing object binding node (or create a new one) for the specified binding ID without updating its tree serial number, creating any parent object binding nodes in the process.
	 * @note: Will only update FSequencerDisplayNode::TreeSerialNumber for object bindings that have a known and valid parent object binding.
	 *
	 * @param BindingID               The Guid of the object binding within UMovieScene::GetBindings
	 * @param AllBindings             A map from guid to binding pointer for all the bindings in the sequence
	 * @param ChildToParentBinding    Child to parent GUID map used for creating parent items
	 * @param OutChildToParentMap     Pointer to a map that should be populated with child->parent relationships to be set up after creation of all nodes
	 * @return A shared pointer to an object binding node or nullptr if the supplied BindingID was not valid for this sequence. The resulting node's serial number will be up-to-date if it is a child of another binding, or it was previously added to a folder.
	 */
	TSharedPtr<FSequencerObjectBindingNode> FindOrCreateObjectBinding(const FGuid& BindingID, const TSortedMap<FGuid, const FMovieSceneBinding*>& AllBindings, const TSortedMap<FGuid, FGuid>& ChildToParentBinding, TMap<FSequencerDisplayNode*, TSharedPtr<FSequencerDisplayNode>>* OutChildToParentMap);

	/**
	 * Creates section handles for all the sections contained in the specified track
	 */
	void UpdateSectionHandles(TSharedRef<FSequencerTrackNode> TrackNode);

private:

	/**
	 * Update the list of filters nodes based on current filter settings, if an update is scheduled
	 * This is called by Update();
	 * 
	 * @return Whether the list of filtered nodes changed
	 */
	bool UpdateFilters();

	/**
	 * Finds or adds a type editor for the track
	 *
	 * @param Track	The type to find an editor for
	 * @return The editor for the type
	 */
	TSharedRef<ISequencerTrackEditor> FindOrAddTypeEditor( UMovieSceneTrack* Track );

	/**
	 * Update the curve editor tree to include anything of relevance from this tree
	 */
	void UpdateCurveEditorTree();

	/**
	 * Add the specified node to the curve editor, including all its parents if necessary
	 */
	FCurveEditorTreeItemID AddToCurveEditor(TSharedRef<FSequencerDisplayNode> Node, FCurveEditor* CurveEditor);

	/**
	 * Checks whether the specified key area node contains key areas that can create curve models
	 */
	bool KeyAreaHasCurves(const FSequencerSectionKeyAreaNode& KeyAreaNode) const;

	/**
	 * Destroys all nodes contained within this tree.
	 * @note: Does not broadcast update notifications
	 */
	void DestroyAllNodes();

public:
	int32 GetTotalDisplayNodeCount() const;
	int32 GetFilteredDisplayNodeCount() const;

private:

	/** Symbolic root node that contains the actual displayed root nodes as children */
	TSharedRef<FSequencerDisplayNode> RootNode;

	TSharedRef<FSequencerDisplayNode> BottomSpacerNode;

	/** A serially incrementing integer that is increased each time the tree is refreshed to track node relevance */
	uint32 SerialNumber;
	/** Map from FMovieSceneBinding::GetObjectGuid to display node */
	TMap<FGuid,      TSharedPtr<FSequencerObjectBindingNode>> ObjectBindingToNode;
	/** Map from UMovieSceneTrack object key to display node for any track (object tracks or master tracks) */
	TMap<FObjectKey, TSharedPtr<FSequencerTrackNode>>         TrackToNode;
	/** Map from UMovieSceneFolder object key to display node for any folder (root or child folder) */
	TMap<FObjectKey, TSharedPtr<FSequencerFolderNode>>        FolderToNode;

	/** Map from UMovieSceneSection* to its UI handle */
	TMap<FObjectKey, FSectionHandle> SectionToHandle;

	/** Tools for building movie scene section layouts.  One tool for each track */
	TMap< UMovieSceneTrack*, TSharedPtr<ISequencerTrackEditor> > EditorMap;
	/** Set of all filtered nodes */
	TSet< TSharedRef<FSequencerDisplayNode> > FilteredNodes;
	/** Cardinal hovered node */
	TSharedPtr<FSequencerDisplayNode> HoveredNode;
	/** Active filter string if any */
	FString FilterString;
	/** Sequencer interface */
	FSequencer& Sequencer;
	/** Display node -> curve editor tree item ID mapping */
	TMap<TWeakPtr<FSequencerDisplayNode>, FCurveEditorTreeItemID> CurveEditorTreeItemIDs;
	/** A multicast delegate which is called whenever the node tree has been updated. */
	FOnUpdated OnUpdatedDelegate;

	/** Active track filters */
	TSharedPtr<FSequencerTrackFilterCollection> TrackFilters;
	
	/** Level based track filtering */
	TSharedPtr<FSequencerTrackFilter_LevelFilter> TrackFilterLevelFilter;

	TWeakObjectPtr<UMovieSceneSequence> WeakCurrentSequence;

	/** The total number of DisplayNodes in the tree, both displayed and hidden */
	uint32 DisplayNodeCount;

	bool bFilterUpdateRequested;
	bool bFilteringOnNodeGroups;

	/** Cached value of whether we have any nodes that should be treated as soloing */
	bool bHasSoloNodes;
};
