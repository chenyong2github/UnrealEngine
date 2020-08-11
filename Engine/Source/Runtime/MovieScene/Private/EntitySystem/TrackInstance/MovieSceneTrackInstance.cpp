// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/TrackInstance/MovieSceneTrackInstance.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "Algo/Sort.h"

void UMovieSceneTrackInstance::Initialize(UObject* InAnimatedObject, UMovieSceneEntitySystemLinker* InLinker)
{
	// We make the difference between a master track instance, and a bound track instance that lost its binding,
	// by remembering if this track instance was initialized with a valid bound object.
	AnimatedObject = InAnimatedObject;
	bIsMasterTrackInstance = (InAnimatedObject == nullptr);

	Linker = InLinker;

	OnInitialize();
}

void UMovieSceneTrackInstance::Animate()
{
	OnAnimate();
}

void UMovieSceneTrackInstance::Destroy()
{
	if (bIsMasterTrackInstance || !UE::MovieScene::FBuiltInComponentTypes::IsBoundObjectGarbage(AnimatedObject))
	{
		OnDestroyed();
	}
}

void UMovieSceneTrackInstance::UpdateInputs(TArray<FMovieSceneTrackInstanceInput>&& InNewInputs)
{
	Algo::Sort(InNewInputs);

	// Fast path if they are the same
	if (Inputs == InNewInputs)
	{
		return;
	}

	// We know they are different in some way - now 
	OnBeginUpdateInputs();

	int32 OldIndex = 0;
	int32 NewIndex = 0;

	const int32 OldNum = Inputs.Num();
	const int32 NewNum = InNewInputs.Num();

	for ( ; OldIndex < OldNum || NewIndex < NewNum; )
	{
		while (OldIndex < OldNum && NewIndex < NewNum && Inputs[OldIndex] == InNewInputs[NewIndex])
		{
			++OldIndex;
			++NewIndex;
		}

		if (OldIndex >= OldNum && NewIndex >= NewNum)
		{
			break;
		}
		else if (OldIndex < OldNum && NewIndex < NewNum)
		{
			if (Inputs[OldIndex].Section < InNewInputs[NewIndex].Section)
			{
				// Out with the old
				OnInputRemoved(Inputs[OldIndex]);
				++OldIndex;
			}
			else
			{
				// and in with the new
				OnInputAdded(InNewInputs[NewIndex]);
				++NewIndex;
			}
		}
		else if (OldIndex < OldNum)
		{
			// Out with the old
			OnInputRemoved(Inputs[OldIndex]);
			++OldIndex;
		}
		else if (ensure(NewIndex < NewNum))
		{
			// and in with the new
			OnInputAdded(InNewInputs[NewIndex]);
			++NewIndex;
		}
	}

	Swap(Inputs, InNewInputs);

	OnEndUpdateInputs();
}

UWorld* UMovieSceneTrackInstance::GetWorld() const
{
	return AnimatedObject ? AnimatedObject->GetWorld() : Super::GetWorld();
}
