// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SequencerNodeTree.h"
#include "MovieSceneBinding.h"
#include "GameFramework/Actor.h"
#include "MovieScene.h"
#include "DisplayNodes/SequencerDisplayNode.h"
#include "DisplayNodes/SequencerFolderNode.h"
#include "DisplayNodes/SequencerObjectBindingNode.h"
#include "ISequencerSection.h"
#include "DisplayNodes/SequencerTrackNode.h"
#include "MovieSceneSequence.h"
#include "Tracks/MovieSceneCinematicShotTrack.h"
#include "Sequencer.h"
#include "MovieSceneFolder.h"
#include "ISequencerTrackEditor.h"
#include "DisplayNodes/SequencerRootNode.h"
#include "Widgets/Views/STableRow.h"
#include "CurveEditor.h"
#include "SequencerNodeSortingMethods.h"


FSequencerNodeTree::FSequencerNodeTree(FSequencer& InSequencer)
	: RootNode(MakeShared<FSequencerRootNode>(*this))
	, SerialNumber(0)
	, Sequencer(InSequencer)
{}

TSharedPtr<FSequencerObjectBindingNode> FSequencerNodeTree::FindObjectBindingNode(const FGuid& BindingID) const
{
	return ObjectBindingToNode.FindRef(BindingID);
}

void FSequencerNodeTree::RefreshNodes(UMovieScene* MovieScene)
{
	check(MovieScene);

	++SerialNumber;

	TSortedMap<FGuid, FGuid> ChildToParentBinding;
	TSortedMap<FGuid, const FMovieSceneBinding*> AllBindings;

	// Gather all object bindings in the sequence
	for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
	{
		AllBindings.Add(Binding.GetObjectGuid(), &Binding);
	}

	// Populate the binding hierarchy
	for (int32 PossessableIndex = 0; PossessableIndex < MovieScene->GetPossessableCount(); ++PossessableIndex)
	{
		const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(PossessableIndex);

		FGuid ThisID   = Possessable.GetGuid();
		FGuid ParentID = Possessable.GetParent();

		if (ParentID.IsValid())
		{
			ChildToParentBinding.Add(ThisID, ParentID);
		}
	}

	// Folders may also create hierarchy items for tracks and object bindings
	{
		for (UMovieSceneFolder* Folder : MovieScene->GetRootFolders())
		{
			if (ensureAlwaysMsgf(Folder, TEXT("MovieScene data contains a null folder. This should never happen.")))
			{
				TSharedRef<FSequencerFolderNode> RootFolderNode = CreateOrUpdateFolder(Folder, AllBindings, ChildToParentBinding, MovieScene);
				RootFolderNode->SetParent(RootNode);
			}
		}
	}

	// Object Bindings
	{
		for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
		{
			TSharedPtr<FSequencerObjectBindingNode> ObjectBindingNode = CreateOrUpdateObjectBinding(Binding.GetObjectGuid(), AllBindings, ChildToParentBinding, MovieScene);
			if (!ObjectBindingNode.IsValid())
			{
				continue;
			}

			// Ensure it has a parent
			if (!ObjectBindingNode->IsParentStillRelevant(SerialNumber))
			{
				ObjectBindingNode->SetParent(RootNode);
			}

			// Create nodes for the object binding's tracks
			for (UMovieSceneTrack* Track : Binding.GetTracks())
			{
				if (ensureAlwaysMsgf(Track, TEXT("MovieScene binding '%s' data contains a null track. This should never happen."), *Binding.GetName()))
				{
					TSharedPtr<FSequencerTrackNode> TrackNode = CreateOrUpdateTrack(Track, ETrackType::Object);
					if (TrackNode.IsValid())
					{
						TrackNode->SetParent(ObjectBindingNode);
					}
				}
			}
		}
	}

	// Master tracks
	{
		UMovieSceneTrack* CameraCutTrack = MovieScene->GetCameraCutTrack();
		if (CameraCutTrack)
		{
			TSharedPtr<FSequencerTrackNode> TrackNode = CreateOrUpdateTrack(CameraCutTrack, ETrackType::Master);
			if (TrackNode.IsValid() && !TrackNode->IsParentStillRelevant(SerialNumber))
			{
				TrackNode->SetParent(RootNode);
			}
		}

		// Iterate all master tracks and generate nodes if necessary
		for (UMovieSceneTrack* Track : MovieScene->GetMasterTracks())
		{
			if (ensureAlwaysMsgf(Track, TEXT("MovieScene data contains a null master track. This should never happen.")))
			{
				TSharedPtr<FSequencerTrackNode> TrackNode = CreateOrUpdateTrack(Track, ETrackType::Master);
				if (TrackNode.IsValid() && !TrackNode->IsParentStillRelevant(SerialNumber))
				{
					TrackNode->SetParent(RootNode);
				}
			}
		}
	}

	// Remove anything that is no longer relevant (ie serial number is out of date)
	for (auto It = FolderToNode.CreateIterator(); It; ++It)
	{
		if (It->Value->TreeSerialNumber != SerialNumber)
		{
			It->Value->SetParent(nullptr);
			It.RemoveCurrent();
		}
	}
	for (auto It = TrackToNode.CreateIterator(); It; ++It)
	{
		if (It->Value->TreeSerialNumber != SerialNumber)
		{
			It->Value->SetParent(nullptr);
			It.RemoveCurrent();
		}
	}
	for (auto It = ObjectBindingToNode.CreateIterator(); It; ++It)
	{
		if (It->Value->TreeSerialNumber != SerialNumber)
		{
			It->Value->SetParent(nullptr);
			It.RemoveCurrent();
		}
	}
}

TSharedPtr<FSequencerTrackNode> FSequencerNodeTree::CreateOrUpdateTrack(UMovieSceneTrack* Track, ETrackType TrackType)
{
	check(Track);

	FObjectKey TrackKey(Track);
	TSharedPtr<FSequencerTrackNode> TrackNode = TrackToNode.FindRef(TrackKey);
	if (TrackNode.IsValid())
	{
		// Should be implemented as a filter
		if (!Sequencer.IsTrackVisible(Track))
		{
			TrackNode->SetParent(nullptr);
			TrackToNode.Remove(TrackKey);
			return nullptr;
		}
	}
	else
	{
		const bool bIsDraggable = TrackType == ETrackType::Master;
		TrackNode = MakeShared<FSequencerTrackNode>(Track, *FindOrAddTypeEditor(Track), bIsDraggable, *this);
		TrackToNode.Add(TrackKey, TrackNode);
	}

	// Assign the serial number for this node to indicate that it is still relevant
	TrackNode->TreeSerialNumber = SerialNumber;
	TrackNode->UpdateInnerHierarchy();
	return TrackNode;
}

TSharedRef<FSequencerFolderNode> FSequencerNodeTree::CreateOrUpdateFolder(UMovieSceneFolder* Folder, const TSortedMap<FGuid, const FMovieSceneBinding*>& AllBindings, const TSortedMap<FGuid, FGuid>& ChildToParentBinding, const UMovieScene* InMovieScene)
{
	check(Folder);

	FObjectKey FolderKey(Folder);

	TSharedPtr<FSequencerFolderNode> FolderNode = FolderToNode.FindRef(FolderKey);
	if (!FolderNode.IsValid())
	{
		FolderNode = MakeShared<FSequencerFolderNode>(*Folder, *this);
		FolderToNode.Add(FolderKey, FolderNode.ToSharedRef());
	}

	// Assign the serial number for this node to indicate that it is still relevant
	FolderNode->TreeSerialNumber = SerialNumber;

	// Create the hierarchy for any child bindings
	for (const FGuid& ID : Folder->GetChildObjectBindings())
	{
		TSharedPtr<FSequencerObjectBindingNode> Binding = CreateOrUpdateObjectBinding(ID, AllBindings, ChildToParentBinding, InMovieScene);
		if (Binding.IsValid())
		{
			Binding->SetParent(FolderNode);
		}
	}

	// Create the hierarchy for any master tracks
	for (UMovieSceneTrack* Track : Folder->GetChildMasterTracks())
	{
		if (ensureAlwaysMsgf(Track, TEXT("MovieScene folder '%s' data contains a null track. This should never happen."), *Folder->GetName()))
		{
			TSharedPtr<FSequencerTrackNode> TrackNode = CreateOrUpdateTrack(Track, ETrackType::Master);
			if (TrackNode.IsValid())
			{
				TrackNode->SetParent(FolderNode);
			}
		}
	}

	// Add child folders
	for (UMovieSceneFolder* ChildFolder : Folder->GetChildFolders())
	{
		if (ensureAlwaysMsgf(ChildFolder, TEXT("MovieScene folder '%s' data contains a null child folder. This should never happen."), *Folder->GetName()))
		{
			TSharedRef<FSequencerFolderNode> ChildFolderNode = CreateOrUpdateFolder(ChildFolder, AllBindings, ChildToParentBinding, InMovieScene);
			ChildFolderNode->SetParent(FolderNode);
		}
	}

	return FolderNode.ToSharedRef();
}

TSharedPtr<FSequencerObjectBindingNode> FSequencerNodeTree::CreateOrUpdateObjectBinding(const FGuid& BindingID, const TSortedMap<FGuid, const FMovieSceneBinding*>& AllBindings, const TSortedMap<FGuid, FGuid>& ChildToParentBinding, const UMovieScene* InMovieScene)
{
	if (!ensureAlwaysMsgf(AllBindings.Contains(BindingID), TEXT("Attempting to add a binding that does not exist.")))
	{
		return nullptr;
	}

	TSharedPtr<FSequencerObjectBindingNode> ObjectBindingNode = ObjectBindingToNode.FindRef(BindingID);
	if (!ObjectBindingNode.IsValid())
	{
		// The node name is the object guid
		FName ObjectNodeName = *BindingID.ToString();

		ObjectBindingNode = MakeShared<FSequencerObjectBindingNode>(ObjectNodeName, BindingID, *this);
		ObjectBindingToNode.Add(BindingID, ObjectBindingNode);
	}

	// Assign the serial number for this node to indicate that it is still relevant
	ObjectBindingNode->TreeSerialNumber = SerialNumber;

	// Create its parent and make the association
	if (const FGuid* ParentGuid = ChildToParentBinding.Find(BindingID))
	{
		TSharedPtr<FSequencerObjectBindingNode> ParentBinding = CreateOrUpdateObjectBinding(*ParentGuid, AllBindings, ChildToParentBinding, InMovieScene);
		if (ParentBinding.IsValid())
		{
			ObjectBindingNode->SetParent(ParentBinding);
		}
	}

	return ObjectBindingNode;
}

void FSequencerNodeTree::Update()
{
	Sequencer.GetSelection().EmptySelectedOutlinerNodes();

	EditorMap.Empty();
	FilteredNodes.Empty();
	SectionToHandle.Empty();
	HoveredNode = nullptr;

	UMovieScene* MovieScene = Sequencer.GetFocusedMovieSceneSequence()->GetMovieScene();
	RefreshNodes(MovieScene);

	// Re-filter the tree after updating 
	// @todo sequencer: Newly added sections may need to be visible even when there is a filter
	FilterNodes( FilterString );

	// Sort root nodes
	RootNode->SortImmediateChildren();

	// Set up virtual offsets, expansion states, and tints
	float VerticalOffset = 0.f;

	auto Traverse_OnTreeRefreshed = [this, &VerticalOffset](FSequencerDisplayNode& InNode)
	{
		// Set up the virtual node position
		float VerticalTop = VerticalOffset;
		VerticalOffset += InNode.GetNodeHeight() + InNode.GetNodePadding().Combined();
		InNode.OnTreeRefreshed(VerticalTop, VerticalOffset);

		if (InNode.GetType() == ESequencerNode::Track)
		{
			this->UpdateSectionHandles(StaticCastSharedRef<FSequencerTrackNode>(InNode.AsShared()));
		}
		return true;
	};

	const bool bIncludeRootNode = false;
	RootNode->Traverse_ParentFirst(Traverse_OnTreeRefreshed, bIncludeRootNode);

	// Ensure that the curve editor tree is up to date for our tree layout
	UpdateCurveEditorTree();

	OnUpdatedDelegate.Broadcast();
}


TSharedRef<ISequencerTrackEditor> FSequencerNodeTree::FindOrAddTypeEditor( UMovieSceneTrack* InTrack )
{
	TSharedPtr<ISequencerTrackEditor> Editor = EditorMap.FindRef( InTrack );

	if( !Editor.IsValid() )
	{
		const TArray<TSharedPtr<ISequencerTrackEditor>>& TrackEditors = Sequencer.GetTrackEditors();

		// Get a tool for each track
		// @todo sequencer: Should probably only need to get this once and it shouldn't be done here. It depends on when movie scene tool modules are loaded
		TSharedPtr<ISequencerTrackEditor> SupportedTool;

		for (const auto& TrackEditor : TrackEditors)
		{
			if (TrackEditor->SupportsType(InTrack->GetClass()))
			{
				EditorMap.Add(InTrack, TrackEditor);
				Editor = TrackEditor;

				break;
			}
		}
	}

	return Editor.ToSharedRef();
}

TSharedRef<FSequencerDisplayNode> FSequencerNodeTree::GetRootNode() const
{
	return RootNode;
}

const TArray<TSharedRef<FSequencerDisplayNode>>& FSequencerNodeTree::GetRootNodes() const
{
	return RootNode->GetChildNodes();
}

void FSequencerNodeTree::MoveDisplayNodeToRoot(TSharedRef<FSequencerDisplayNode>& Node)
{
	// Objects that exist at the root level in a sequence are just removed from the folder they reside in.
	// When the treeview is refreshed this will cause the regenerated nodes to show up at the root level.
	TSharedPtr<FSequencerDisplayNode> ParentSeqNode = Node->GetParent();
	switch (Node->GetType())
	{
		case ESequencerNode::Folder:
		{
			TSharedRef<FSequencerFolderNode> FolderNode = StaticCastSharedRef<FSequencerFolderNode>(Node);
			UMovieScene* FocusedMovieScene = GetSequencer().GetFocusedMovieSceneSequence()->GetMovieScene();

			if (ParentSeqNode.IsValid())
			{
				checkf(ParentSeqNode->GetType() == ESequencerNode::Folder, TEXT("Can not remove from unsupported parent node."));
				TSharedPtr<FSequencerFolderNode> ParentFolder = StaticCastSharedPtr<FSequencerFolderNode>(ParentSeqNode);
				ParentFolder->GetFolder().Modify();
				ParentFolder->GetFolder().RemoveChildFolder(&FolderNode->GetFolder());
			}
			else
			{
				FocusedMovieScene->GetRootFolders().Remove(&FolderNode->GetFolder());
			}

			FocusedMovieScene->GetRootFolders().Add(&FolderNode->GetFolder());
			break;
		}
		case ESequencerNode::Track:
		{
			TSharedRef<FSequencerTrackNode> DraggedTrackNode = StaticCastSharedRef<FSequencerTrackNode>(Node);
			UMovieScene* FocusedMovieScene = GetSequencer().GetFocusedMovieSceneSequence()->GetMovieScene();

			if (ParentSeqNode.IsValid())
			{
				checkf(ParentSeqNode->GetType() == ESequencerNode::Folder, TEXT("Can not remove from unsupported parent node."));
				TSharedPtr<FSequencerFolderNode> ParentFolder = StaticCastSharedPtr<FSequencerFolderNode>(ParentSeqNode);
				ParentFolder->GetFolder().Modify();
				ParentFolder->GetFolder().RemoveChildMasterTrack(DraggedTrackNode->GetTrack());
			}
			break;
		}
		case ESequencerNode::Object:
		{
			TSharedRef<FSequencerObjectBindingNode> DraggedObjectBindingNode = StaticCastSharedRef<FSequencerObjectBindingNode>(Node);
			UMovieScene* FocusedMovieScene = GetSequencer().GetFocusedMovieSceneSequence()->GetMovieScene();

			if (ParentSeqNode.IsValid())
			{
				checkf(ParentSeqNode->GetType() == ESequencerNode::Folder, TEXT("Can not remove from unsupported parent node."));
				TSharedPtr<FSequencerFolderNode> ParentFolder = StaticCastSharedPtr<FSequencerFolderNode>(ParentSeqNode);
				ParentFolder->GetFolder().Modify();
				ParentFolder->GetFolder().RemoveChildObjectBinding(DraggedObjectBindingNode->GetObjectBinding());
			}
			break;
		}
	}

	// Clear the node's parent so that subsequent calls for GetNodePath correctly indicate that they no longer have a parent.
	Node->SetParent(nullptr);

	// Our children have changed parents which means that on subsequent creation they will retrieve their expansion state
	// from the map using their new path. If the new path already exists the object goes to the state stored at that path.
	// If the new path does not exist, the object returns to default state and not what is currently displayed. Either way 
	// causes unexpected user behavior as nodes appear to randomly change expansion state as they are moved around the sequencer.

	// To solve this, we update a node's parent when the node is moved, and then we update their expansion state here
	// while we still have the current expansion state and the new node path. When the UI is regenerated on the subsequent
	// refresh call, it will now retrieve the state the node was just in, instead of the state the node was in the last time it
	// was in that location. This is done recursively as children store absolute paths so they need to be updated too.
	Node->Traverse_ParentFirst([](FSequencerDisplayNode& TraversalNode)
	{
		TraversalNode.GetParentTree().SaveExpansionState(TraversalNode, TraversalNode.IsExpanded());
		return true;
	}, true);
}

void FSequencerNodeTree::SortAllNodesAndDescendants()
{
	auto Traverse_ResetSortOrder = [](FSequencerDisplayNode& Node)
	{
		Node.ResortImmediateChildren();
		return true;
	};

	const bool bIncludeRootNode = true;
	RootNode->Traverse_ParentFirst(Traverse_ResetSortOrder, bIncludeRootNode);

	// Refresh the tree so that our changes are visible.
	// @todo: Is this necessary any more?
	GetSequencer().RefreshTree();
}

void FSequencerNodeTree::SaveExpansionState(const FSequencerDisplayNode& Node, bool bExpanded)
{	
	// @todo Sequencer - This should be moved to the sequence level
	UMovieScene* MovieScene = Sequencer.GetFocusedMovieSceneSequence()->GetMovieScene();
	FMovieSceneEditorData& EditorData = MovieScene->GetEditorData();

	EditorData.ExpansionStates.Add(Node.GetPathName(), FMovieSceneExpansionState(bExpanded));
}


bool FSequencerNodeTree::GetSavedExpansionState(const FSequencerDisplayNode& Node) const
{
	// @todo Sequencer - This should be moved to the sequence level
	UMovieScene* MovieScene = Sequencer.GetFocusedMovieSceneSequence()->GetMovieScene();
	FMovieSceneEditorData& EditorData = MovieScene->GetEditorData();
	FMovieSceneExpansionState* ExpansionState = EditorData.ExpansionStates.Find( Node.GetPathName() );

	return ExpansionState ? ExpansionState->bExpanded : GetDefaultExpansionState(Node);
}


bool FSequencerNodeTree::GetDefaultExpansionState( const FSequencerDisplayNode& Node ) const
{
	// Object nodes, and track nodes that are parent tracks are expanded by default.
	if (Node.GetType() == ESequencerNode::Object)
	{
		return true;
	}

	else if (Node.GetType() == ESequencerNode::Track)
	{
		const FSequencerTrackNode& TrackNode = static_cast<const FSequencerTrackNode&>(Node);

		if (TrackNode.GetSubTrackMode() == FSequencerTrackNode::ESubTrackMode::ParentTrack)
		{
			return true;
		}

		if (TrackNode.GetTrackEditor().GetDefaultExpansionState(TrackNode.GetTrack()))
		{
			return true;
		}
	}

	return false;
}


bool FSequencerNodeTree::IsNodeFiltered( const TSharedRef<const FSequencerDisplayNode> Node ) const
{
	return FilteredNodes.Contains( Node );
}

void FSequencerNodeTree::SetHoveredNode(const TSharedPtr<FSequencerDisplayNode>& InHoveredNode)
{
	if (InHoveredNode != HoveredNode)
	{
		HoveredNode = InHoveredNode;
	}
}

const TSharedPtr<FSequencerDisplayNode>& FSequencerNodeTree::GetHoveredNode() const
{
	return HoveredNode;
}

void FSequencerNodeTree::UpdateSectionHandles(TSharedRef<FSequencerTrackNode> TrackNode)
{
	const TArray<TSharedRef<ISequencerSection>>& Sections = TrackNode->GetSections();
	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		UMovieSceneSection* Section = Sections[SectionIndex]->GetSectionObject();
		if (Section)
		{
			SectionToHandle.Add(Section, FSectionHandle(TrackNode, SectionIndex));
		}
	}
}

TOptional<FSectionHandle> FSequencerNodeTree::GetSectionHandle(const UMovieSceneSection* Section) const
{
	const FSectionHandle* Found = SectionToHandle.Find(Section);
	if (Found)
	{
		return *Found;
	}

	return TOptional<FSectionHandle>();
}

static void AddChildNodes(const TSharedRef<FSequencerDisplayNode>& StartNode, TSet<TSharedRef<const FSequencerDisplayNode>>& OutFilteredNodes)
{
	OutFilteredNodes.Add(StartNode);

	for (TSharedRef<FSequencerDisplayNode> ChildNode : StartNode->GetChildNodes())
	{
		AddChildNodes(ChildNode, OutFilteredNodes);
	}
}

/*
 * Add node as filtered and include any parent folders
 */
static void AddFilteredNode(const TSharedRef<FSequencerDisplayNode>& StartNode, TSet<TSharedRef<const FSequencerDisplayNode>>& OutFilteredNodes)
{
	AddChildNodes(StartNode, OutFilteredNodes);

	// Gather parent folders up the chain
	TSharedPtr<FSequencerDisplayNode> ParentNode = StartNode->GetParent();
	while (ParentNode.IsValid() && ParentNode.Get()->GetType() == ESequencerNode::Folder)
	{
		OutFilteredNodes.Add(ParentNode.ToSharedRef());
		ParentNode = ParentNode->GetParent();
	}
}

static void AddParentNodes(const TSharedRef<FSequencerDisplayNode>& StartNode, TSet<TSharedRef<const FSequencerDisplayNode>>& OutFilteredNodes)
{
	TSharedPtr<FSequencerDisplayNode> ParentNode = StartNode->GetParent();
	if (ParentNode.IsValid())
	{
		OutFilteredNodes.Add(ParentNode.ToSharedRef());
		AddParentNodes(ParentNode.ToSharedRef(), OutFilteredNodes);
	}
}

/**
 * Recursively filters nodes
 *
 * @param StartNode			The node to start from
 * @param FilterStrings		The filter strings which need to be matched
 * @param OutFilteredNodes	The list of all filtered nodes
 * @return Whether the text filter was passed
 */
static bool FilterNodesRecursive( FSequencer& Sequencer, const TSharedRef<FSequencerDisplayNode>& StartNode, const TArray<FString>& FilterStrings, TSet<TSharedRef<const FSequencerDisplayNode>>& OutFilteredNodes )
{
	// check labels - only one of the labels needs to match
	bool bMatchedLabel = false;
	bool bObjectHasLabels = false;
	for (const FString& String : FilterStrings)
	{
		if (String.StartsWith(TEXT("label:")) && String.Len() > 6)
		{
			if (StartNode->GetType() == ESequencerNode::Object)
			{
				bObjectHasLabels = true;
				auto ObjectBindingNode = StaticCastSharedRef<FSequencerObjectBindingNode>(StartNode);
				auto Labels = Sequencer.GetLabelManager().GetObjectLabels(ObjectBindingNode->GetObjectBinding());

				if (Labels != nullptr && Labels->Strings.Contains(String.RightChop(6)))
				{
					bMatchedLabel = true;
					break;
				}
			}
			else if (!StartNode->GetParent().IsValid())
			{
				return false;
			}
		}
	}

	if (bObjectHasLabels && !bMatchedLabel)
	{
		return false;
	}

	// assume the filter is acceptable
	bool bPassedTextFilter = true;

	// check each string in the filter strings list against 
	for (const FString& String : FilterStrings)
	{
		if (!String.StartsWith(TEXT("label:")) && !StartNode->GetDisplayName().ToString().Contains(String)) 
		{
			bPassedTextFilter = false;
			break;
		}
	}

	// whether or the start node is in the filter
	bool bInFilter = false;

	if (bPassedTextFilter)
	{
		// This node is now filtered
		AddFilteredNode(StartNode, OutFilteredNodes);

		bInFilter = true;
	}

	// check each child node to determine if it is filtered
	if (StartNode->GetType() != ESequencerNode::Folder)
	{
		const TArray<TSharedRef<FSequencerDisplayNode>>& ChildNodes = StartNode->GetChildNodes();

		for (const auto& Node : ChildNodes)
		{
			// Mark the parent as filtered if any child node was filtered
			bPassedTextFilter |= FilterNodesRecursive(Sequencer, Node, FilterStrings, OutFilteredNodes);

			if (bPassedTextFilter && !bInFilter)
			{
				AddParentNodes(Node, OutFilteredNodes);

				bInFilter = true;
			}
		}
	}

	return bPassedTextFilter;
}


void FSequencerNodeTree::FilterNodes(const FString& InFilter)
{
	FilteredNodes.Empty();

	if (InFilter.IsEmpty())
	{
		// No filter
		FilterString.Empty();
	}
	else
	{
		// Build a list of strings that must be matched
		TArray<FString> FilterStrings;

		FilterString = InFilter;
		// Remove whitespace from the front and back of the string
		FilterString.TrimStartAndEndInline();
		FilterString.ParseIntoArray(FilterStrings, TEXT(" "), true /*bCullEmpty*/);

		for (auto It = GetRootNodes().CreateConstIterator(); It; ++It)
		{
			// Recursively filter all nodes, matching them against the list of filter strings.  All filter strings must be matched
			FilterNodesRecursive(Sequencer, *It, FilterStrings, FilteredNodes);
		}
	}
}

TArray< TSharedRef<FSequencerDisplayNode> > FSequencerNodeTree::GetAllNodes() const
{
	TArray< TSharedRef<FSequencerDisplayNode> > AllNodes;

	const bool bIncludeRootNode = false;
	RootNode->Traverse_ParentFirst([&AllNodes](FSequencerDisplayNode& InNode) 
	{
		AllNodes.Add(InNode.AsShared());
		return true;
	}, bIncludeRootNode);

	return AllNodes;
}

void FSequencerNodeTree::UpdateCurveEditorTree()
{
	FCurveEditor* CurveEditor = Sequencer.GetCurveEditor().Get();

	// Guard against multiple broadcasts here and defer them until the end of this function
	FScopedCurveEditorTreeUpdateGuard ScopedUpdateGuard = CurveEditor->GetTree()->ScopedUpdateGuard();

	auto Traverse_AddToCurveEditor = [this, CurveEditor](FSequencerDisplayNode& InNode)
	{
		if (InNode.GetType() == ESequencerNode::Track)
		{
			// Track nodes with top level key area's must be added
			TSharedPtr<FSequencerSectionKeyAreaNode> TopLevelKeyArea = static_cast<const FSequencerTrackNode&>(InNode).GetTopLevelKeyNode();
			if (TopLevelKeyArea.IsValid() && KeyAreaHasCurves(*TopLevelKeyArea))
			{
				this->AddToCurveEditor(InNode.AsShared(), CurveEditor);
			}
		}
		else if (InNode.GetType() == ESequencerNode::KeyArea && KeyAreaHasCurves(static_cast<const FSequencerSectionKeyAreaNode&>(InNode)))
		{
			// Key area nodes are always added
			this->AddToCurveEditor(InNode.AsShared(), CurveEditor);
		}
		return true;
	};

	static const bool bIncludeThisNode = false;
	RootNode->Traverse_ChildFirst(Traverse_AddToCurveEditor, bIncludeThisNode);

	// Remove no longer valid elements from the curve editor tree
	for (auto It = CurveEditorTreeItemIDs.CreateIterator(); It; ++It)
	{
		TSharedPtr<FSequencerDisplayNode> Node = It->Key.Pin();
		if (!Node.IsValid() || Node->TreeSerialNumber != SerialNumber)
		{
			CurveEditor->RemoveTreeItem(It->Value);
			It.RemoveCurrent();
		}
	}
}

bool FSequencerNodeTree::KeyAreaHasCurves(const FSequencerSectionKeyAreaNode& KeyAreaNode) const
{
	for (const TSharedRef<IKeyArea>& KeyArea : KeyAreaNode.GetAllKeyAreas())
	{
		const ISequencerChannelInterface* EditorInterface = KeyArea->FindChannelEditorInterface();
		if (EditorInterface && EditorInterface->SupportsCurveEditorModels_Raw(KeyArea->GetChannel()))
		{
			return true;
		}
	}
	return false;
}

FCurveEditorTreeItemID FSequencerNodeTree::AddToCurveEditor(TSharedRef<FSequencerDisplayNode> DisplayNode, FCurveEditor* CurveEditor)
{
	if (FCurveEditorTreeItemID* Existing = CurveEditorTreeItemIDs.Find(DisplayNode))
	{
		return *Existing;
	}

	TSharedPtr<FSequencerDisplayNode> Parent = DisplayNode->GetParent();

	FCurveEditorTreeItemID ParentID = Parent.IsValid() ? AddToCurveEditor(Parent.ToSharedRef(), CurveEditor) : FCurveEditorTreeItemID::Invalid();

	FCurveEditorTreeItem* NewItem = CurveEditor->AddTreeItem(ParentID);
	NewItem->SetWeakItem(StaticCastSharedRef<ICurveEditorTreeItem>(DisplayNode));

	CurveEditorTreeItemIDs.Add(DisplayNode, NewItem->GetID());
	return NewItem->GetID();
}