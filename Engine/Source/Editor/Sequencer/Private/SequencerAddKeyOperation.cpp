// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerAddKeyOperation.h"

#include "ISequencer.h"
#include "ISequencerTrackEditor.h"
#include "DisplayNodes/SequencerDisplayNode.h"
#include "DisplayNodes/SequencerTrackNode.h"
#include "DisplayNodes/SequencerSectionKeyAreaNode.h"

#include "Algo/Find.h"


namespace UE
{
namespace Sequencer
{


FAddKeyOperation FAddKeyOperation::FromNodes(const TSet<TSharedRef<FSequencerDisplayNode>>& InNodes)
{
	FAddKeyOperation Operation;

	TArray<TSharedRef<FSequencerDisplayNode>> FilteredNodes;

	// Remove any child nodes that have a parent also included in the set
	for (const TSharedRef<FSequencerDisplayNode>& ProspectiveNode : InNodes)
	{
		TSharedPtr<FSequencerDisplayNode> Parent = ProspectiveNode->GetParent();
		while (Parent)
		{
			if (InNodes.Contains(Parent.ToSharedRef()))
			{
				goto Continue;
			}
			Parent = Parent->GetParent();
		}

		FilteredNodes.Add(ProspectiveNode);

	Continue:
		continue;
	}

	Operation.AddPreFilteredNodes(FilteredNodes);
	return Operation;
}

FAddKeyOperation FAddKeyOperation::FromNode(TSharedRef<FSequencerDisplayNode> InNode)
{
	FAddKeyOperation Operation;
	Operation.AddPreFilteredNodes(MakeArrayView(&InNode, 1));
	return Operation;
}

FAddKeyOperation FAddKeyOperation::FromKeyAreas(ISequencerTrackEditor* TrackEditor, const TArrayView<TSharedRef<IKeyArea>> InKeyAreas)
{
	FAddKeyOperation Operation;
	if (ensure(TrackEditor))
	{
		for (const TSharedRef<IKeyArea>& KeyArea : InKeyAreas)
		{
			Operation.ProcessKeyArea(TrackEditor, KeyArea);
		}
	}
	return Operation;
}

void FAddKeyOperation::AddPreFilteredNodes(TArrayView<const TSharedRef<FSequencerDisplayNode>> FilteredNodes)
{
	auto KeyChildTrackArea = [this](FSequencerDisplayNode& InNode)
	{
		if (InNode.GetType() == ESequencerNode::Track)
		{
			FSequencerTrackNode* TrackNode = static_cast<FSequencerTrackNode*>(&InNode);
			if (TrackNode->GetSubTrackMode() != FSequencerTrackNode::ESubTrackMode::ParentTrack)
			{
				// Consider everything underneath this track for keying
				this->ConsiderKeyableAreas(TrackNode, TrackNode);
			}
		}
		return true;
	};

	for (const TSharedRef<FSequencerDisplayNode>& Node : FilteredNodes)
	{
		if (TSharedPtr<FSequencerTrackNode> ParentTrack = Node->FindParentTrackNode())
		{
			ConsiderKeyableAreas(ParentTrack.Get(), &Node.Get());
		}
		else
		{
			Node->Traverse_ParentFirst(KeyChildTrackArea);
		}
	}
}

bool FAddKeyOperation::ConsiderKeyableAreas(FSequencerTrackNode* InTrackNode, FSequencerDisplayNode* KeyAnythingBeneath)
{
	bool bKeyedAnything = false;

	auto Traversal = [this, InTrackNode, &bKeyedAnything](FSequencerDisplayNode& InNode)
	{
		if (InNode.GetType() == ESequencerNode::Track)
		{
			FSequencerTrackNode* ThisTrackNode = static_cast<FSequencerTrackNode*>(&InNode);

			if (TSharedPtr<FSequencerSectionKeyAreaNode> KeyAreaNode = ThisTrackNode->GetTopLevelKeyNode())
			{
				bKeyedAnything |= this->ProcessKeyAreaNode(ThisTrackNode, KeyAreaNode.Get());
			}
		}

		if (InNode.GetType() == ESequencerNode::KeyArea)
		{
			bKeyedAnything |= this->ProcessKeyAreaNode(InTrackNode, static_cast<FSequencerSectionKeyAreaNode*>(&InNode));
		}
		return true;
	};
	KeyAnythingBeneath->Traverse_ParentFirst(Traversal, true);

	return bKeyedAnything;
}

bool FAddKeyOperation::ProcessKeyAreaNode(FSequencerTrackNode* InTrackNode, const FSequencerSectionKeyAreaNode* KeyAreaNode)
{
	bool bKeyedAnything = false;

	for (const TSharedRef<IKeyArea>& KeyArea : KeyAreaNode->GetAllKeyAreas())
	{
		bKeyedAnything |= ProcessKeyArea(InTrackNode, KeyArea);
	}

	return bKeyedAnything;
}

bool FAddKeyOperation::ProcessKeyArea(FSequencerTrackNode* InTrackNode, TSharedPtr<IKeyArea> InKeyArea)
{
	ISequencerTrackEditor* TrackEditor = &InTrackNode->GetTrackEditor();
	return ProcessKeyArea(TrackEditor, InKeyArea);
}

bool FAddKeyOperation::ProcessKeyArea(ISequencerTrackEditor* InTrackEditor, TSharedPtr<IKeyArea> InKeyArea)
{
	TSharedPtr<ISequencerSection> Section       = InKeyArea->GetSectionInterface();
	UMovieSceneSection*           SectionObject = Section       ? Section->GetSectionObject()                      : nullptr;
	UMovieSceneTrack*             TrackObject   = SectionObject ? SectionObject->GetTypedOuter<UMovieSceneTrack>() : nullptr;

	if (TrackObject)
	{
		GetTrackOperation(InTrackEditor).Populate(TrackObject, Section, InKeyArea);
		return true;
	}

	return false;
}

void FAddKeyOperation::Commit(FFrameNumber KeyTime, ISequencer& InSequencer)
{
	for (TTuple<ISequencerTrackEditor*, FKeyOperation>& Pair : OperationsByTrackEditor)
	{
		Pair.Value.InitializeOperation(KeyTime);
		Pair.Key->ProcessKeyOperation(KeyTime, Pair.Value, InSequencer);
	}

	InSequencer.UpdatePlaybackRange();
	InSequencer.NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
}

FKeyOperation& FAddKeyOperation::GetTrackOperation(ISequencerTrackEditor* TrackEditor)
{
	return OperationsByTrackEditor.FindOrAdd(TrackEditor);
}

} // namespace Sequencer
} // namespace UE
