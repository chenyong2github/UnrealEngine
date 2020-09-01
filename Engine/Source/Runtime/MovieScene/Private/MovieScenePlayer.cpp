// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "IMovieScenePlayer.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "EntitySystem/MovieSceneSequenceInstance.h"
#include "MovieSceneSequence.h"
#include "MovieSceneSequenceID.h"


static TSparseArray<IMovieScenePlayer*> GlobalPlayerRegistry;

IMovieScenePlayer::IMovieScenePlayer()
{
	UniqueIndex = GlobalPlayerRegistry.Add(this);
}

IMovieScenePlayer::~IMovieScenePlayer()
{
	ensureMsgf(IsInGameThread(), TEXT("Destruction must occur on the game thread"));

	GlobalPlayerRegistry.RemoveAt(UniqueIndex, 1);
}

IMovieScenePlayer* IMovieScenePlayer::Get(uint16 InUniqueIndex)
{
	check(GlobalPlayerRegistry.IsValidIndex(InUniqueIndex));
	return GlobalPlayerRegistry[InUniqueIndex];
}

void IMovieScenePlayer::ResolveBoundObjects(const FGuid& InBindingId, FMovieSceneSequenceID SequenceID, UMovieSceneSequence& Sequence, UObject* ResolutionContext, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const
{
	Sequence.LocateBoundObjects(InBindingId, ResolutionContext, OutObjects);
}

void IMovieScenePlayer::InvalidateCachedData()
{
	FMovieSceneRootEvaluationTemplateInstance& Template = GetEvaluationTemplate();

	UE::MovieScene::FSequenceInstance* RootInstance = Template.FindInstance(MovieSceneSequenceID::Root);
	if (RootInstance)
	{
		RootInstance->InvalidateCachedData(Template.GetEntitySystemLinker());
	}
}