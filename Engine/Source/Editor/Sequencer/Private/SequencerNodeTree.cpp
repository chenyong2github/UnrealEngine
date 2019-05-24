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
#include "SequencerSectionLayoutBuilder.h"
#include "ISequencerTrackEditor.h"
#include "DisplayNodes/SequencerSpacerNode.h"
#include "Widgets/Views/STableRow.h"
#include "SequencerNodeSortingMethods.h"
#include "SequencerTrackFilters.h"

FSequencerNodeTree::~FSequencerNodeTree()
{
	if (TrackFilters.IsValid())
	{
		TrackFilters->OnChanged().RemoveAll(this);
	}
	if (TrackFilterLevelFilter.IsValid())
	{
		TrackFilterLevelFilter->OnChanged().RemoveAll(this);
	}
}

FSequencerNodeTree::FSequencerNodeTree(class FSequencer& InSequencer) : Sequencer(InSequencer), bFilterUpdateRequested(false)
{
	TrackFilters = MakeShared<FSequencerTrackFilterCollection>();
	TrackFilters->OnChanged().AddRaw(this, &FSequencerNodeTree::RequestFilterUpdate);
	TrackFilterLevelFilter = MakeShared< FSequencerTrackFilter_LevelFilter>();
	TrackFilterLevelFilter->OnChanged().AddRaw(this, &FSequencerNodeTree::RequestFilterUpdate);
}

void FSequencerNodeTree::Empty()
{
	RootNodes.Empty();
	ObjectBindingMap.Empty();
	Sequencer.GetSelection().EmptySelectedOutlinerNodes();
	EditorMap.Empty();
	FilteredNodes.Empty();
	HoveredNode = nullptr;
}


void FSequencerNodeTree::AddObjectBindingAndTracks(const FMovieSceneBinding& Binding, TMap<FGuid, const FMovieSceneBinding*>& GuidToBindingMap, TArray< TSharedRef<FSequencerObjectBindingNode> >& OutNodeList)
{
	TSharedRef<FSequencerObjectBindingNode> ObjectBindingNode = AddObjectBinding( Binding.GetName(), Binding.GetObjectGuid(), GuidToBindingMap, OutNodeList );

	for( UMovieSceneTrack* Track : Binding.GetTracks() )
	{
		if (!Sequencer.IsTrackVisible(Track))
		{
			continue;
		}

		// Create the new track node
		TSharedRef<FSequencerTrackNode> TrackNode = MakeShared<FSequencerTrackNode>(*Track, *FindOrAddTypeEditor(*Track), false, nullptr, *this);

		// Make the sub tracks and section interfaces for this node, and add it to the object binding node
		// Note: MakeSubTracksAndSectionInterfaces may return a new parent node
		ObjectBindingNode->AddTrackNode(MakeSubTracksAndSectionInterfaces(TrackNode, ObjectBindingNode->GetObjectBinding()));
	}
}


void FSequencerNodeTree::Update()
{
	HoveredNode = nullptr;

	// @todo Sequencer - This update pass is too aggressive.  Some nodes may still be valid
	Empty();

	UMovieScene* MovieScene = Sequencer.GetFocusedMovieSceneSequence()->GetMovieScene();
	UMovieSceneCinematicShotTrack* CinematicShotTrack = MovieScene->FindMasterTrack<UMovieSceneCinematicShotTrack>();

	// Get the master tracks  so we can get sections from them
	const TArray<UMovieSceneTrack*>& MasterTracks = MovieScene->GetMasterTracks();
	TArray<TSharedRef<FSequencerTrackNode>> MasterTrackNodes;

	for (UMovieSceneTrack* Track : MasterTracks)
	{
		if (Track != CinematicShotTrack)
		{
			UMovieSceneTrack& TrackRef = *Track;

			TSharedRef<FSequencerTrackNode> NodeToAdd = MakeSubTracksAndSectionInterfaces(MakeShared<FSequencerTrackNode>(TrackRef, *FindOrAddTypeEditor(TrackRef), true, nullptr, *this));
			MasterTrackNodes.Add(NodeToAdd);
		}
	}

	const TArray<FMovieSceneBinding>& Bindings = MovieScene->GetBindings();
	TMap<FGuid, const FMovieSceneBinding*> GuidToBindingMap;

	for (const FMovieSceneBinding& Binding : Bindings)
	{
		GuidToBindingMap.Add(Binding.GetObjectGuid(), &Binding);
	}

	// Make nodes for all object bindings
	TArray<TSharedRef<FSequencerObjectBindingNode>> ObjectNodes;
	for( const FMovieSceneBinding& Binding : Bindings )
	{
		if (!Sequencer.IsBindingVisible(Binding))
		{
			continue;
		}

		AddObjectBindingAndTracks(Binding, GuidToBindingMap, ObjectNodes);
	}

	// If no bindings were added (presumably because of visibility) but there are bindings, add all regardless of visibility
	if (!ObjectNodes.Num())
	{
		for( const FMovieSceneBinding& Binding : Bindings )
		{
			AddObjectBindingAndTracks(Binding, GuidToBindingMap, ObjectNodes);
		}
	}

	// Cinematic shot track always comes first
	if (CinematicShotTrack)
	{
		TSharedRef<FSequencerTrackNode> NodeToAdd = MakeSubTracksAndSectionInterfaces(MakeShared<FSequencerTrackNode>(*CinematicShotTrack, *FindOrAddTypeEditor(*CinematicShotTrack), false, nullptr, *this));
		RootNodes.Add(NodeToAdd);
	}

	// Then comes the camera cut track
	UMovieSceneTrack* CameraCutTrack = MovieScene->GetCameraCutTrack();
	
	if (CameraCutTrack)
	{
		TSharedRef<FSequencerTrackNode> NodeToAdd = MakeSubTracksAndSectionInterfaces(MakeShared<FSequencerTrackNode>(*CameraCutTrack, *FindOrAddTypeEditor(*CameraCutTrack), false, nullptr, *this));
		RootNodes.Add(NodeToAdd);
	}

	// Add all other nodes after the camera cut track
	TArray<TSharedRef<FSequencerDisplayNode>> FolderAndObjectAndTrackNodes;
	TArray<TSharedRef<FSequencerDisplayNode>> MasterTrackNodesNotInFolders;
	CreateAndPopulateFolderNodes( MasterTrackNodes, ObjectNodes, MovieScene->GetRootFolders(), FolderAndObjectAndTrackNodes, MasterTrackNodesNotInFolders );

	// Merge the two lists together before sorting them together.
	FolderAndObjectAndTrackNodes.Append(MasterTrackNodesNotInFolders);

	// Now sort the folders, tracks and objects together based on sorting order.
	FolderAndObjectAndTrackNodes.Sort(FDisplayNodeSortingOrderSorter());

	for (TSharedRef<FSequencerDisplayNode> Node : FolderAndObjectAndTrackNodes)
	{
		// Recursively sort the children of these tracks
		Node->SortChildNodes(FDisplayNodeSortingOrderSorter());
	}

	// Now that we've sorted the children we normalize their sorting index. This doesn't call Modify (as we're not part of a transaction) but modifies the
	// in-memory sorting index of the backing data structures. This means the next time the tree is refreshed, the existing nodes will keep their sort
	// and any new nodes will get pushed to the end again. When an asset is saved it'll write the sorting index to the asset and the next time it is loaded
	// the Sort function will keep them in the same order even if they exist in a different order within the owning data structures.
	for (int32 i = 0; i < FolderAndObjectAndTrackNodes.Num(); i++)
	{
		FolderAndObjectAndTrackNodes[i]->SetSortingOrder(i);
		FolderAndObjectAndTrackNodes[i]->Traverse_ParentFirst([](FSequencerDisplayNode& TraversalNode)
		{
			int32 ChildIndex = 0;
			for (uint32 k = 0; k < TraversalNode.GetNumChildren(); k++)
			{
				// Sometimes a node can have multiple display node children because the sections within a row
				// have been re-arranged into an overlapping state. These rows are backed by the same data structure
				// as their parents, so we skip them instead of incrementing the parent's sorting order.
				if (TraversalNode.GetChildNodes()[k]->GetType() == ESequencerNode::Track)
				{
					TSharedRef<FSequencerTrackNode> FolderNode = StaticCastSharedRef<FSequencerTrackNode>(TraversalNode.GetChildNodes()[k]);
					if (FolderNode->GetSubTrackMode() == FSequencerTrackNode::ESubTrackMode::SubTrack)
					{
						continue;
					}
				}

				TraversalNode.GetChildNodes()[k]->SetSortingOrder(ChildIndex);
				ChildIndex++;
			}
			return true;
		}, true);
	}

	RootNodes.Append(FolderAndObjectAndTrackNodes);
	RootNodes.Reserve(FMath::Max(1, RootNodes.Num() - 1) * 2);
	for (int32 Index = 1; Index < RootNodes.Num(); Index += 2)
	{
		RootNodes.Insert(MakeShareable(new FSequencerSpacerNode(1.f, nullptr, *this, false)), Index);
	}

	// Always make space at the end of the tree
	RootNodes.Add(MakeShared<FSequencerSpacerNode>(20.f, nullptr, *this, true));

	// Set up virtual offsets, expansion states, and tints
	float VerticalOffset = 0.f;

	for (TSharedRef<FSequencerDisplayNode>& Node : RootNodes)
	{
		Node->Traverse_ParentFirst([&](FSequencerDisplayNode& InNode) {

			// Set up the virtual node position
			float VerticalTop = VerticalOffset;
			VerticalOffset += InNode.GetNodeHeight() + InNode.GetNodePadding().Combined();
			InNode.Initialize(VerticalTop, VerticalOffset);

			return true;
		});
	}

	// Re-filter the tree after updating 
	// @todo sequencer: Newly added sections may need to be visible even when there is a filter
	bFilterUpdateRequested = true;
	UpdateFilters();

	OnUpdatedDelegate.Broadcast();
}


TSharedRef<ISequencerTrackEditor> FSequencerNodeTree::FindOrAddTypeEditor( UMovieSceneTrack& InTrack )
{
	TSharedPtr<ISequencerTrackEditor> Editor = EditorMap.FindRef( &InTrack );

	if( !Editor.IsValid() )
	{
		const TArray<TSharedPtr<ISequencerTrackEditor>>& TrackEditors = Sequencer.GetTrackEditors();

		// Get a tool for each track
		// @todo sequencer: Should probably only need to get this once and it shouldn't be done here. It depends on when movie scene tool modules are loaded
		TSharedPtr<ISequencerTrackEditor> SupportedTool;

		for (const auto& TrackEditor : TrackEditors)
		{
			if (TrackEditor->SupportsType(InTrack.GetClass()))
			{
				EditorMap.Add(&InTrack, TrackEditor);
				Editor = TrackEditor;

				break;
			}
		}
	}

	return Editor.ToSharedRef();
}


TSharedRef<FSequencerTrackNode> FSequencerNodeTree::MakeSubTracksAndSectionInterfaces(TSharedRef<FSequencerTrackNode> TrackNode, const FGuid& ObjectBinding)
{
	using ESubTrackMode = FSequencerTrackNode::ESubTrackMode;

	UMovieSceneTrack* Track = TrackNode->GetTrack();

	check(Track);
	check(!TrackNode->GetParent().IsValid());

	TArray<UMovieSceneSection*> MovieSceneSections = Track->GetAllSections();
	if (MovieSceneSections.Num() == 0)
	{
		return TrackNode;
	}

	Algo::SortBy(MovieSceneSections, &UMovieSceneSection::GetRowIndex);

	const bool bHasMultipleRows = MovieSceneSections.Last()->GetRowIndex() != 0;

	TSharedRef<ISequencerTrackEditor> Editor = FindOrAddTypeEditor( *Track );

	TArray<TSharedRef<FSequencerTrackNode>, TInlineAllocator<4>> SubTrackNodes;

	TSharedRef<FSequencerTrackNode> ParentNode = TrackNode;
	TSharedRef<FSequencerTrackNode> CurrentTrackNode = TrackNode;

	for (int32 SectionIndex = 0; SectionIndex < MovieSceneSections.Num(); ++SectionIndex)
	{
		UMovieSceneSection* SectionObject = MovieSceneSections[SectionIndex];
		const int32 RowIndex = SectionObject->GetRowIndex();

		if (CurrentTrackNode->GetSubTrackMode() == ESubTrackMode::SubTrack && RowIndex != CurrentTrackNode->GetRowIndex())
		{
			CurrentTrackNode = MakeShared<FSequencerTrackNode>(*Track, *Editor, ParentNode->CanDrag(), ParentNode, *this);

			CurrentTrackNode->SetSubTrackMode(ESubTrackMode::SubTrack);
			CurrentTrackNode->SetRowIndex(RowIndex);
			ParentNode->AddChildTrack(CurrentTrackNode);
		}

		// Make the section interface
		TSharedRef<ISequencerSection> Section = Editor->MakeSectionInterface(*SectionObject, *Track, ObjectBinding);

		// Ask the section to generate its inner layout
		FSequencerSectionLayoutBuilder Builder(CurrentTrackNode);
		Section->GenerateSectionLayout(Builder);

		if (bHasMultipleRows && CurrentTrackNode == ParentNode)
		{
			// Create a new parent node
			ParentNode = MakeShared<FSequencerTrackNode>(*Track, *Editor, CurrentTrackNode->CanDrag(), nullptr, *this);
			ParentNode->SetSubTrackMode(ESubTrackMode::ParentTrack);

			CurrentTrackNode->SetSubTrackMode(ESubTrackMode::SubTrack);
			CurrentTrackNode->SetRowIndex(RowIndex);
			ParentNode->AddChildTrack(CurrentTrackNode);
		}

		CurrentTrackNode->AddSection(Section);
	}

	return ParentNode;
}


const TArray<TSharedRef<FSequencerDisplayNode>>& FSequencerNodeTree::GetRootNodes() const
{
	return RootNodes;
}

bool FSequencerNodeTree::HasActiveFilter() const
{
	return (!FilterString.IsEmpty() || TrackFilters->Num() > 0 || TrackFilterLevelFilter->IsActive());
}

TSharedRef<FSequencerObjectBindingNode> FSequencerNodeTree::AddObjectBinding(const FString& ObjectName, const FGuid& ObjectBinding, TMap<FGuid, const FMovieSceneBinding*>& GuidToBindingMap, TArray<TSharedRef<FSequencerObjectBindingNode>>& OutNodeList)
{
	TSharedPtr<FSequencerObjectBindingNode> ObjectNode;
	TSharedPtr<FSequencerObjectBindingNode>* FoundObjectNode = ObjectBindingMap.Find(ObjectBinding);
	if (FoundObjectNode != nullptr)
	{
		ObjectNode = *FoundObjectNode;
	}
	else
	{
		// The node name is the object guid
		FName ObjectNodeName = *ObjectBinding.ToString();

		// Try to get the parent object node if there is one.
		TSharedPtr<FSequencerObjectBindingNode> ParentNode;

		UMovieSceneSequence* Sequence = Sequencer.GetFocusedMovieSceneSequence();

		// Prefer to use the parent spawnable if possible, rather than relying on runtime object presence
		FMovieScenePossessable* Possessable = Sequence->GetMovieScene()->FindPossessable(ObjectBinding);
		if (Possessable && Possessable->GetParent().IsValid())
		{
			const FMovieSceneBinding* ParentBinding = GuidToBindingMap.FindRef(Possessable->GetParent());
			if (ParentBinding)
			{
				ParentNode = AddObjectBinding( ParentBinding->GetName(), Possessable->GetParent(), GuidToBindingMap, OutNodeList );
			}
		}

		// get human readable name of the object
		const FString& DisplayString = ObjectName;

		// Create the node.
		ObjectNode = MakeShareable(new FSequencerObjectBindingNode(ObjectNodeName, FText::FromString(DisplayString), ObjectBinding, ParentNode, *this));

		if (ParentNode.IsValid())
		{
			ParentNode->AddObjectBindingNode(ObjectNode.ToSharedRef());
		}
		else
		{
			OutNodeList.Add( ObjectNode.ToSharedRef() );
		}

		// Map the guid to the object binding node for fast lookup later
		ObjectBindingMap.Add( ObjectBinding, ObjectNode );
	}

	return ObjectNode.ToSharedRef();
}


TSharedRef<FSequencerDisplayNode> CreateFolderNode(
	UMovieSceneFolder& MovieSceneFolder, FSequencerNodeTree& NodeTree, 
	TMap<UMovieSceneTrack*, TSharedRef<FSequencerTrackNode>>& MasterTrackToDisplayNodeMap,
	TMap<FGuid, TSharedRef<FSequencerObjectBindingNode>>& ObjectGuidToDisplayNodeMap )
{
	TSharedRef<FSequencerFolderNode> FolderNode( new FSequencerFolderNode( MovieSceneFolder, TSharedPtr<FSequencerDisplayNode>(), NodeTree ) );

	for (UMovieSceneFolder* ChildFolder : MovieSceneFolder.GetChildFolders())
	{
		if (ChildFolder != nullptr)
		{
			FolderNode->AddChildNode(CreateFolderNode(*ChildFolder, NodeTree, MasterTrackToDisplayNodeMap, ObjectGuidToDisplayNodeMap));
		}
	}

	for ( UMovieSceneTrack* MasterTrack : MovieSceneFolder.GetChildMasterTracks() )
	{
		TSharedRef<FSequencerTrackNode>* TrackNodePtr = MasterTrackToDisplayNodeMap.Find( MasterTrack );
		if ( TrackNodePtr != nullptr)
		{
			// TODO: Log this.
			FolderNode->AddChildNode( *TrackNodePtr );
			MasterTrackToDisplayNodeMap.Remove( MasterTrack );
		}
	}

	for (const FGuid& ObjectGuid : MovieSceneFolder.GetChildObjectBindings() )
	{
		TSharedRef<FSequencerObjectBindingNode>* ObjectNodePtr = ObjectGuidToDisplayNodeMap.Find( ObjectGuid );
		if ( ObjectNodePtr != nullptr )
		{
			// TODO: Log this.
			FolderNode->AddChildNode( *ObjectNodePtr );
			ObjectGuidToDisplayNodeMap.Remove( ObjectGuid );
		}
	}

	return FolderNode;
}


void FSequencerNodeTree::CreateAndPopulateFolderNodes( 
	TArray<TSharedRef<FSequencerTrackNode>>& MasterTrackNodes, TArray<TSharedRef<FSequencerObjectBindingNode>>& ObjectNodes,
	TArray<UMovieSceneFolder*>& MovieSceneFolders, TArray<TSharedRef<FSequencerDisplayNode>>& FolderAndObjectNodes, TArray<TSharedRef<FSequencerDisplayNode>>&  MasterTrackNodesNotInFolders )
{
	TMap<UMovieSceneTrack*, TSharedRef<FSequencerTrackNode>> MasterTrackToDisplayNodeMap;
	for ( TSharedRef<FSequencerTrackNode> MasterTrackNode : MasterTrackNodes )
	{
		MasterTrackToDisplayNodeMap.Add( MasterTrackNode->GetTrack(), MasterTrackNode );
	}

	TMap<FGuid, TSharedRef<FSequencerObjectBindingNode>> ObjectGuidToDisplayNodeMap;
	for ( TSharedRef<FSequencerObjectBindingNode> ObjectBindingNode : ObjectNodes )
	{
		ObjectGuidToDisplayNodeMap.Add( ObjectBindingNode->GetObjectBinding(), ObjectBindingNode );
	}

	for (UMovieSceneFolder* MovieSceneFolder : MovieSceneFolders)
	{
		if (MovieSceneFolder != nullptr)
		{
			FolderAndObjectNodes.Add(CreateFolderNode(*MovieSceneFolder, *this, MasterTrackToDisplayNodeMap, ObjectGuidToDisplayNodeMap));
		}
	}

	TArray<TSharedRef<FSequencerTrackNode>> NonFolderTrackNodes;
	MasterTrackToDisplayNodeMap.GenerateValueArray( NonFolderTrackNodes );
	for ( TSharedRef<FSequencerTrackNode> NonFolderTrackNode : NonFolderTrackNodes )
	{
		MasterTrackNodesNotInFolders.Add( NonFolderTrackNode );
	}

	TArray<TSharedRef<FSequencerObjectBindingNode>> NonFolderObjectNodes;
	ObjectGuidToDisplayNodeMap.GenerateValueArray( NonFolderObjectNodes );
	for ( TSharedRef<FSequencerObjectBindingNode> NonFolderObjectNode : NonFolderObjectNodes )
	{
		FolderAndObjectNodes.Add( NonFolderObjectNode );
	}
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
	Node->ClearParent();

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
	// Sort the root first
	SortAndSetSortingOrder(RootNodes, RootNodes, TOptional<EItemDropZone>(), FDisplayNodeCategoricalSorter(), nullptr);
	
	// Recursively sort our children looking for folders.
	TArray<TSharedRef<FSequencerDisplayNode>> ChildNodes = GetRootNodes();
	for (TSharedRef<FSequencerDisplayNode> Child : ChildNodes)
	{
		Child->Traverse_ParentFirst([&](FSequencerDisplayNode& Node)
		{
			// Folders are the only type of node that can have children that we can sort, so there is no need
			// to follow the traversal all the way down.
			if (Node.GetType() != ESequencerNode::Folder)
				return false;
	
			SortAndSetSortingOrder(Node.GetChildNodes(), Node.GetChildNodes(), TOptional<EItemDropZone>(), FDisplayNodeCategoricalSorter(), nullptr);
			return true;
		}, true);
	}

	// Refresh the tree so that our changes are visible.
	GetSequencer().RefreshTree();
}

void FSequencerNodeTree::AddFilter(TSharedRef<FSequencerTrackFilter> TrackFilter)
{
	TrackFilters->Add(TrackFilter);
}

int32 FSequencerNodeTree::RemoveFilter(TSharedRef<FSequencerTrackFilter> TrackFilter)
{
	return TrackFilters->Remove(TrackFilter);
}

void FSequencerNodeTree::RemoveAllFilters()
{
	TrackFilters->RemoveAll();
	TrackFilterLevelFilter->ResetFilter();
}

bool FSequencerNodeTree::IsTrackFilterActive(TSharedRef<FSequencerTrackFilter> TrackFilter) const
{
	return TrackFilters->Contains(TrackFilter);
}

void FSequencerNodeTree::AddLevelFilter(const FString& LevelName)
{
	TrackFilterLevelFilter->UnhideLevel(LevelName);
}

void FSequencerNodeTree::RemoveLevelFilter(const FString& LevelName)
{
	TrackFilterLevelFilter->HideLevel(LevelName);
}

bool FSequencerNodeTree::IsTrackLevelFilterActive(const FString& LevelName) const
{
	return !TrackFilterLevelFilter->IsLevelHidden(LevelName);
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


bool FSequencerNodeTree::IsNodeFiltered(const TSharedRef<const FSequencerDisplayNode> Node) const
{
	for (auto It = FilteredNodes.CreateConstIterator(); It; ++It)
	{
		if ((*It) == Node)
		{
			return true;
		}
	}
	return false;
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

static void AddChildNodes(const TSharedRef<FSequencerDisplayNode>& StartNode, TSet<TSharedRef<FSequencerDisplayNode>>& OutFilteredNodes)
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
static void AddFilteredNode(const TSharedRef<FSequencerDisplayNode>& StartNode, TSet<TSharedRef<FSequencerDisplayNode>>& OutFilteredNodes)
{
	if (!StartNode->IsExpanded())
	{
		StartNode->SetExpansionState(true);
	}

	AddChildNodes(StartNode, OutFilteredNodes);

	// Gather parent folders up the chain
	TSharedPtr<FSequencerDisplayNode> ParentNode = StartNode->GetParent();
	while (ParentNode.IsValid())
	{
		if (!ParentNode.Get()->IsExpanded())
		{
			ParentNode.Get()->SetExpansionState(true);
		}

		OutFilteredNodes.Add(ParentNode.ToSharedRef());
		ParentNode = ParentNode->GetParent();
	}
}

static void AddParentNodes(const TSharedRef<FSequencerDisplayNode>& StartNode, TSet<TSharedRef<FSequencerDisplayNode>>& OutFilteredNodes)
{
	TSharedPtr<FSequencerDisplayNode> ParentNode = StartNode->GetParent();
	if (ParentNode.IsValid())
	{
		if (!ParentNode.Get()->IsExpanded())
		{
			ParentNode.Get()->SetExpansionState(true);
		}

		OutFilteredNodes.Add(ParentNode.ToSharedRef());
		AddParentNodes(ParentNode.ToSharedRef(), OutFilteredNodes);
	}
}

/**
 * Recursively filters nodes
 *
 * @param StartNode			The node to start from
 * @param Filters			The filter collection to test against
 * @param OutFilteredNodes	The list of all filtered nodes
 */

static void FilterNodesRecursive( FSequencer& Sequencer, const TSharedRef<FSequencerDisplayNode>& StartNode, TSharedPtr<FSequencerTrackFilterCollection> Filters, const TArray<FString>& FilterStrings, TSharedPtr<FSequencerTrackFilter_LevelFilter> LevelTrackFilter, TSet<TSharedRef<FSequencerDisplayNode>>& OutFilteredNodes )
{
	for (TSharedRef<FSequencerDisplayNode> Node : StartNode->GetChildNodes())
	{
		FilterNodesRecursive(Sequencer, Node, Filters, FilterStrings, LevelTrackFilter, OutFilteredNodes);
	}

	bool bPasssedAnyFilters = false;

	if (StartNode->GetType() == ESequencerNode::Track)
	{
		UMovieSceneTrack* Track = static_cast<const FSequencerTrackNode&>(StartNode.Get()).GetTrack();
		if (Filters->Num() == 0 || Filters->PassesAnyFilters(Track))
		{
			bPasssedAnyFilters = true;

			// Track nodes do not belong to a level, but might be a child of an objectbinding node that does
			if (LevelTrackFilter->IsActive())
			{
				TSharedPtr<const FSequencerDisplayNode> ParentNode = StartNode->GetParent();
				while (ParentNode.IsValid())
				{
					if (ParentNode->GetType() == ESequencerNode::Object)
					{
						// The track belongs to an objectbinding node, start by assuming it doesn't match the level filter
						bPasssedAnyFilters = false;

						const FSequencerObjectBindingNode* ObjectNode = static_cast<const FSequencerObjectBindingNode*>(ParentNode.Get());
						for (TWeakObjectPtr<>& Object : Sequencer.FindObjectsInCurrentSequence(ObjectNode->GetObjectBinding()))
						{
							if (LevelTrackFilter->PassesFilter(Object.Get()))
							{
								// If at least one of the objects on the objectbinding node pass the level filter, show the track
								bPasssedAnyFilters = true;
								break;
							}
						}

						break;
					}
					ParentNode = ParentNode->GetParent();
				}
			}
		}
	}
	else if (StartNode->GetType() == ESequencerNode::Object)
	{
		const FSequencerObjectBindingNode ObjectNode = static_cast<const FSequencerObjectBindingNode&>(StartNode.Get());
		for (TWeakObjectPtr<>& Object : Sequencer.FindObjectsInCurrentSequence(ObjectNode.GetObjectBinding()))
		{
			if ((Filters->Num() == 0 || Filters->PassesAnyFilters(Object.Get()))
				&& LevelTrackFilter->PassesFilter(Object.Get()))
			{
				bPasssedAnyFilters = true;
				break;
			}
		}
	}

	if (bPasssedAnyFilters)
	{
		// If we have a filter string, make sure we match
		if (FilterStrings.Num() > 0)
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
						return;
					}
				}
			}

			if (bObjectHasLabels && !bMatchedLabel)
			{
				return;
			}

			// check each string in the filter strings list against 
			for (const FString& String : FilterStrings)
			{
				if (!String.StartsWith(TEXT("label:")) && !StartNode->GetDisplayName().ToString().Contains(String))
				{
					return;
				}
			}
		}
		AddFilteredNode(StartNode, OutFilteredNodes);
	}
}

void FSequencerNodeTree::UpdateFilters()
{
	if (!bFilterUpdateRequested)
	{
		return;
	}

	FilteredNodes.Empty();


	UObject* PlaybackContext = Sequencer.GetPlaybackContext();
	UWorld* World = PlaybackContext ? PlaybackContext->GetWorld() : nullptr;
	if (World)
	{
		TrackFilterLevelFilter->UpdateWorld(World);
	}

	if (TrackFilters->Num() > 0 || !FilterString.IsEmpty() || TrackFilterLevelFilter->IsActive())
	{
		// Build a list of strings that must be matched
		TArray<FString> FilterStrings;

		// Remove whitespace from the front and back of the string
		FilterString.TrimStartAndEndInline();
		FilterString.ParseIntoArray(FilterStrings, TEXT(" "), true /*bCullEmpty*/);

		for (auto It = ObjectBindingMap.CreateIterator(); It; ++It)
		{
			FilterNodesRecursive(Sequencer, It.Value().ToSharedRef(), TrackFilters, FilterStrings, TrackFilterLevelFilter, FilteredNodes);
		}

		for (auto It = RootNodes.CreateIterator(); It; ++It)
		{
			FilterNodesRecursive(Sequencer, *It, TrackFilters, FilterStrings, TrackFilterLevelFilter, FilteredNodes);
		}
	}

	bFilterUpdateRequested = false;
}

void FSequencerNodeTree::FilterNodes(const FString& InFilter)
{
	if (InFilter != FilterString)
	{
		FilterString = InFilter;
		bFilterUpdateRequested = true;
	}
}

TArray< TSharedRef<FSequencerDisplayNode> > FSequencerNodeTree::GetAllNodes() const
{
	TArray< TSharedRef<FSequencerDisplayNode> > AllNodes;

	for (const TSharedRef<FSequencerDisplayNode>& Node : RootNodes)
	{
		Node->Traverse_ParentFirst([&](FSequencerDisplayNode& InNode) 
		{
			AllNodes.Add(InNode.AsShared());
			return true;
		});
	}

	return AllNodes;
}