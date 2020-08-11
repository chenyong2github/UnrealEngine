// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneObjectBindingID.h"
#include "Evaluation/MovieSceneSequenceHierarchy.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "Compilation/MovieSceneCompiledDataManager.h"
#include "IMovieScenePlayer.h"

FMovieSceneObjectBindingID FMovieSceneObjectBindingID::ResolveLocalToRoot(FMovieSceneSequenceID LocalSequenceID, IMovieScenePlayer& Player) const
{
	FMovieSceneSequenceID NewSequenceID = FMovieSceneSequenceID(uint32(SequenceID));

	if (Space == EMovieSceneObjectBindingSpace::Local && LocalSequenceID != MovieSceneSequenceID::Root)
	{
		FMovieSceneRootEvaluationTemplateInstance& Instance  = Player.GetEvaluationTemplate();
		const FMovieSceneSequenceHierarchy&        Hierarchy = Instance.GetCompiledDataManager()->GetHierarchyChecked(Instance.GetCompiledDataID());

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
