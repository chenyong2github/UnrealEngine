// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Evaluation/MovieSceneSequenceHierarchy.h"
#include "Sections/MovieSceneSubSection.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "MovieSceneTimeHelpers.h"

FMovieSceneSubSequenceData::FMovieSceneSubSequenceData()
	: Sequence(nullptr)
	, HierarchicalBias(0)
{}

FMovieSceneSubSequenceData::FMovieSceneSubSequenceData(const UMovieSceneSubSection& InSubSection)
	: Sequence(InSubSection.GetSequence())
	, DeterministicSequenceID(InSubSection.GetSequenceID())
	, HierarchicalBias(InSubSection.Parameters.HierarchicalBias)
#if WITH_EDITORONLY_DATA
	, SectionPath(*InSubSection.GetPathNameInMovieScene())
#endif
	, SubSectionSignature(InSubSection.GetSignature())
{
	PreRollRange.Value  = TRange<FFrameNumber>::Empty();
	PostRollRange.Value = TRange<FFrameNumber>::Empty();

	UMovieSceneSequence* SequencePtr   = GetSequence();
	UMovieScene*         MovieScenePtr = SequencePtr ? SequencePtr->GetMovieScene() : nullptr;

	checkf(MovieScenePtr, TEXT("Attempting to construct sub sequence data with a null sequence."));

	TickResolution = MovieScenePtr->GetTickResolution();
	FullPlayRange.Value = MovieScenePtr->GetPlaybackRange();

	TRange<FFrameNumber> SubSectionRange = InSubSection.GetTrueRange();
	checkf(SubSectionRange.GetLowerBound().IsClosed() && SubSectionRange.GetUpperBound().IsClosed(), TEXT("Use of open (infinite) bounds with sub sections is not supported."));

	// Get the transform from the given section to its inner sequence.
	// Note that FMovieSceneCompiler will accumulate RootToSequenceTransform for us a bit later so that it ends up
	// being truly the full transform.
	OuterToInnerTransform = RootToSequenceTransform = InSubSection.OuterToInnerTransform();

	if (!InSubSection.Parameters.bCanLoop)
	{
		PlayRange.Value = SubSectionRange * RootToSequenceTransform.LinearTransform;
		UnwarpedPlayRange.Value = PlayRange.Value;
	}
	else
	{
		// If we're looping, there's a good chance we need the entirety of the sub-sequence to be compiled.
		PlayRange.Value = MovieScenePtr->GetPlaybackRange();
		PlayRange.Value.SetLowerBoundValue(PlayRange.Value.GetLowerBoundValue() + InSubSection.Parameters.StartFrameOffset);
		PlayRange.Value.SetUpperBoundValue(FMath::Max(
			PlayRange.Value.GetUpperBoundValue() - InSubSection.Parameters.EndFrameOffset,
			PlayRange.Value.GetLowerBoundValue() + 1));
		UnwarpedPlayRange.Value = RootToSequenceTransform.TransformRangeUnwarped(SubSectionRange);
	}

	// Make sure pre/postroll *ranges* are in the inner sequence's time space. Pre/PostRollFrames are in the outer sequence space.
	if (InSubSection.GetPreRollFrames() > 0)
	{
		PreRollRange = MovieScene::MakeDiscreteRangeFromUpper( TRangeBound<FFrameNumber>::FlipInclusion(SubSectionRange.GetLowerBound()), InSubSection.GetPreRollFrames() ) * RootToSequenceTransform.LinearTransform;
	}
	if (InSubSection.GetPostRollFrames() > 0)
	{
		PostRollRange = MovieScene::MakeDiscreteRangeFromLower( TRangeBound<FFrameNumber>::FlipInclusion(SubSectionRange.GetUpperBound()), InSubSection.GetPostRollFrames() ) * RootToSequenceTransform.LinearTransform;
	}
}

UMovieSceneSequence* FMovieSceneSubSequenceData::GetSequence() const
{
	UMovieSceneSequence* ResolvedSequence = GetLoadedSequence();
	if (!ResolvedSequence)
	{
		ResolvedSequence = Cast<UMovieSceneSequence>(Sequence.ResolveObject());
		CachedSequence = ResolvedSequence;
	}
	return ResolvedSequence;
}


UMovieSceneSequence* FMovieSceneSubSequenceData::GetLoadedSequence() const
{
	return CachedSequence.Get();
}

bool FMovieSceneSubSequenceData::IsDirty(const UMovieSceneSubSection& InSubSection) const
{
	return InSubSection.GetSignature() != SubSectionSignature || InSubSection.OuterToInnerTransform() != OuterToInnerTransform;
}

void FMovieSceneSequenceHierarchy::Add(const FMovieSceneSubSequenceData& Data, FMovieSceneSequenceIDRef ThisSequenceID, FMovieSceneSequenceIDRef ParentID)
{
	check(ParentID != MovieSceneSequenceID::Invalid);

	// Add (or update) the sub sequence data
	SubSequences.Add(ThisSequenceID, Data);

	// Set up the hierarchical information if we don't have any, or its wrong
	FMovieSceneSequenceHierarchyNode* ExistingHierarchyNode = Hierarchy.Find(ThisSequenceID);
	if (!ExistingHierarchyNode || ExistingHierarchyNode->ParentID != ParentID)
	{
		if (!ExistingHierarchyNode)
		{
			// The node doesn't yet exist - create it
			FMovieSceneSequenceHierarchyNode Node(ParentID);
			Hierarchy.Add(ThisSequenceID, Node);
		}
		else
		{
			// The node exists already but under the wrong parent - we need to move it
			FMovieSceneSequenceHierarchyNode* Parent = Hierarchy.Find(ExistingHierarchyNode->ParentID);
			check(Parent);
			// Remove it from its parent's children
			Parent->Children.Remove(ThisSequenceID);

			// Set the parent ID
			ExistingHierarchyNode->ParentID = ParentID;
		}

		// Add the node to its parent's children array
		FMovieSceneSequenceHierarchyNode* Parent = Hierarchy.Find(ParentID);
		check(Parent);
		ensure(!Parent->Children.Contains(ThisSequenceID));
		Parent->Children.Add(ThisSequenceID);
	}
}

void FMovieSceneSequenceHierarchy::Remove(TArrayView<const FMovieSceneSequenceID> SequenceIDs)
{
	TArray<FMovieSceneSequenceID, TInlineAllocator<16>> IDsToRemove;
	IDsToRemove.Append(SequenceIDs.GetData(), SequenceIDs.Num());

	while (IDsToRemove.Num())
	{
		int32 NumRemaining = IDsToRemove.Num();
		for (int32 Index = 0; Index < NumRemaining; ++Index)
		{
			FMovieSceneSequenceID ID = IDsToRemove[Index];

			SubSequences.Remove(ID);

			// Remove all children too
			if (const FMovieSceneSequenceHierarchyNode* Node = FindNode(ID))
			{
				FMovieSceneSequenceHierarchyNode* Parent = Hierarchy.Find(Node->ParentID);
				if (Parent)
				{
					Parent->Children.Remove(ID);
				}

				IDsToRemove.Append(Node->Children);
				Hierarchy.Remove(ID);
			}
		}

		IDsToRemove.RemoveAt(0, NumRemaining);
	}
}
