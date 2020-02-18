// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/TemplateSequenceEditorUtil.h"
#include "ISequencer.h"
#include "Templates/Casts.h"
#include "TemplateSequence.h"
#include "UObject/Class.h"

FTemplateSequenceEditorUtil::FTemplateSequenceEditorUtil(UTemplateSequence* InTemplateSequence, ISequencer& InSequencer)
	: TemplateSequence(InTemplateSequence)
	, Sequencer(InSequencer)
{
	check(InTemplateSequence != nullptr);
}

void FTemplateSequenceEditorUtil::ChangeActorBinding(UObject* Object, UActorFactory* ActorFactory, bool bSetupDefaults)
{
	if (!ensure(Object != nullptr))
	{
		return;
	}

	// See if we have anything to do in the first place.
	if (UClass* ChosenClass = Cast<UClass>(Object))
	{
		if (ChosenClass == TemplateSequence->BoundActorClass)
		{
			return;
		}
	}

	UMovieScene* MovieScene = TemplateSequence->GetMovieScene();
	check(MovieScene != nullptr);

	// See if we previously had a main object binding.
	FGuid PreviousSpawnableGuid;
	if (MovieScene->GetSpawnableCount() > 0)
	{
		PreviousSpawnableGuid = MovieScene->GetSpawnable(0).GetGuid();
	}

	// Make the new spawnable object binding.
	FGuid NewSpawnableGuid = Sequencer.MakeNewSpawnable(*Object);
	FMovieSceneSpawnable* NewSpawnable = MovieScene->FindSpawnable(NewSpawnableGuid);

	if (Object->IsA<UClass>())
	{
		UClass* ChosenClass = StaticCast<UClass*>(Object);
		TemplateSequence->BoundActorClass = ChosenClass;
	}
	else
	{
		const UObject* SpawnableTemplate = NewSpawnable->GetObjectTemplate();
		if (ensure(SpawnableTemplate != nullptr))
		{
			TemplateSequence->BoundActorClass = SpawnableTemplate->GetClass();
		}
	}

	// If we had a previous one, move everything under it to the new binding, and clean up.
	if (PreviousSpawnableGuid.IsValid())
	{
		MovieScene->MoveBindingContents(PreviousSpawnableGuid, NewSpawnableGuid);

		if (MovieScene->RemoveSpawnable(PreviousSpawnableGuid))
		{
			FMovieSceneSpawnRegister& SpawnRegister = Sequencer.GetSpawnRegister();
			SpawnRegister.DestroySpawnedObject(PreviousSpawnableGuid, Sequencer.GetFocusedTemplateID(), Sequencer);
		}
	}
}

