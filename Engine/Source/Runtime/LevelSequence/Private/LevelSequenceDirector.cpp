// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSequenceDirector.h"
#include "Engine/World.h"
#include "UObject/Stack.h"
#include "MovieSceneObjectBindingID.h"
#include "LevelSequencePlayer.h"
#include "IMovieScenePlayer.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"

UWorld* ULevelSequenceDirector::GetWorld() const
{
	if (ULevel* OuterLevel = GetTypedOuter<ULevel>())
	{
		return OuterLevel->OwningWorld;
	}
	return GetTypedOuter<UWorld>();
}

TArray<UObject*> ULevelSequenceDirector::GetBoundObjects(FMovieSceneObjectBindingID ObjectBinding)
{
	TArray<UObject*> Objects;

	if (IMovieScenePlayer* PlayerInterface = IMovieScenePlayer::Get(static_cast<uint16>(MovieScenePlayerIndex)))
	{
		for (TWeakObjectPtr<> WeakObject : ObjectBinding.ResolveBoundObjects(FMovieSceneSequenceID(SubSequenceID), *PlayerInterface))
		{
			if (UObject* Object = WeakObject.Get())
			{
				Objects.Add(Object);
			}
		}
	}
	else
	{
		FFrame::KismetExecutionMessage(TEXT("No player interface available or assigned."), ELogVerbosity::Error);
	}

	return Objects;
}

UObject* ULevelSequenceDirector::GetBoundObject(FMovieSceneObjectBindingID ObjectBinding)
{
	if (IMovieScenePlayer* PlayerInterface = IMovieScenePlayer::Get(static_cast<uint16>(MovieScenePlayerIndex)))
	{
		for (TWeakObjectPtr<> WeakObject : ObjectBinding.ResolveBoundObjects(FMovieSceneSequenceID(SubSequenceID), *PlayerInterface))
		{
			if (UObject* Object = WeakObject.Get())
			{
				return Object;
			}
		}
	}
	else
	{
		FFrame::KismetExecutionMessage(TEXT("No player interface available or assigned."), ELogVerbosity::Error);
	}

	return nullptr;
}

TArray<AActor*> ULevelSequenceDirector::GetBoundActors(FMovieSceneObjectBindingID ObjectBinding)
{
	TArray<AActor*> Actors;

	if (IMovieScenePlayer* PlayerInterface = IMovieScenePlayer::Get(static_cast<uint16>(MovieScenePlayerIndex)))
	{
		for (TWeakObjectPtr<> WeakObject : ObjectBinding.ResolveBoundObjects(FMovieSceneSequenceID(SubSequenceID), *PlayerInterface))
		{
			if (AActor* Actor = Cast<AActor>(WeakObject.Get()))
			{
				Actors.Add(Actor);
			}
		}
	}
	else
	{
		FFrame::KismetExecutionMessage(TEXT("No player interface available or assigned."), ELogVerbosity::Error);
	}

	return Actors;
}

AActor* ULevelSequenceDirector::GetBoundActor(FMovieSceneObjectBindingID ObjectBinding)
{
	if (IMovieScenePlayer* PlayerInterface = IMovieScenePlayer::Get(static_cast<uint16>(MovieScenePlayerIndex)))
	{
		for (TWeakObjectPtr<> WeakObject : ObjectBinding.ResolveBoundObjects(FMovieSceneSequenceID(SubSequenceID), *PlayerInterface))
		{
			if (AActor* Actor = Cast<AActor>(WeakObject.Get()))
			{
				return Actor;
			}
		}
	}
	else
	{
		FFrame::KismetExecutionMessage(TEXT("No player interface available or assigned."), ELogVerbosity::Error);
	}

	return nullptr;
}

UMovieSceneSequence* ULevelSequenceDirector::GetSequence()
{
	if (IMovieScenePlayer* PlayerInterface = IMovieScenePlayer::Get(static_cast<uint16>(MovieScenePlayerIndex)))
	{
		return PlayerInterface->GetEvaluationTemplate().GetSequence(FMovieSceneSequenceID(SubSequenceID));
	}
	else
	{		
		FFrame::KismetExecutionMessage(TEXT("No sequence player."), ELogVerbosity::Error);

		return nullptr;
	}
}

