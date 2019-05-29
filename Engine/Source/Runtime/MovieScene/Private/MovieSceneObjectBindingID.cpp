// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MovieSceneObjectBindingID.h"
#include "Evaluation/MovieSceneSequenceHierarchy.h"

FMovieSceneObjectBindingID FMovieSceneObjectBindingID::ResolveLocalToRoot(FMovieSceneSequenceID LocalSequenceID, const FMovieSceneSequenceHierarchy& Hierarchy) const
{
	FMovieSceneSequenceID NewSequenceID = FMovieSceneSequenceID(uint32(SequenceID));

	// If the space is root, the binding was made as a root binding, but it's now being referred to as a local space binding, so resolve from the parent
	bool bResolveFromParent = false;
	if (Space == EMovieSceneObjectBindingSpace::Root)
	{
		const FMovieSceneSequenceHierarchyNode* CurrentNode = Hierarchy.FindNode(LocalSequenceID);
		if (CurrentNode && CurrentNode->ParentID != MovieSceneSequenceID::Root && Hierarchy.FindNode(CurrentNode->ParentID))
		{
			LocalSequenceID = CurrentNode->ParentID;
			bResolveFromParent = true;
		}
	}

	if ((Space == EMovieSceneObjectBindingSpace::Local && LocalSequenceID != MovieSceneSequenceID::Root) || bResolveFromParent)
	{
		while (LocalSequenceID != MovieSceneSequenceID::Root)
		{
			const FMovieSceneSequenceHierarchyNode* CurrentNode = Hierarchy.FindNode(LocalSequenceID);
			const FMovieSceneSubSequenceData* SubData = Hierarchy.FindSubData(LocalSequenceID);
			
			if (!ensureAlwaysMsgf(CurrentNode && SubData, TEXT("Malformed sequence hierarchy")))
			{
				return FMovieSceneObjectBindingID(Guid, NewSequenceID);
			}

			NewSequenceID = NewSequenceID.AccumulateParentID(SubData->DeterministicSequenceID);
			LocalSequenceID = CurrentNode->ParentID;
		}
	}

	return FMovieSceneObjectBindingID(Guid, NewSequenceID);
}
