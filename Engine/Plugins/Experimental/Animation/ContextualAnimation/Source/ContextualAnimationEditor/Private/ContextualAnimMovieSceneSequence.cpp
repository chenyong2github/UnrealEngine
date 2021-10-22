// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimMovieSceneSequence.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Tracks/MovieSceneActorReferenceTrack.h"
#include "ContextualAnimMovieSceneNotifyTrack.h"
#include "ContextualAnimViewModel.h"

UContextualAnimMovieSceneSequence::UContextualAnimMovieSceneSequence(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

ETrackSupport UContextualAnimMovieSceneSequence::IsTrackSupported(TSubclassOf<class UMovieSceneTrack> InTrackClass) const
{ 
	//@TODO: Remove UMovieSceneSkeletalAnimationTrack and UMovieSceneEventTrack from here after adding a custom track to represent the animation
	if (InTrackClass == UMovieSceneSkeletalAnimationTrack::StaticClass() ||
		InTrackClass == UMovieSceneEventTrack::StaticClass() ||
		InTrackClass == UMovieSceneActorReferenceTrack::StaticClass() ||
		InTrackClass == UContextualAnimMovieSceneNotifyTrack::StaticClass())
	{
		return ETrackSupport::Supported;
	}
	
	return Super::IsTrackSupported(InTrackClass);
}

void UContextualAnimMovieSceneSequence::Initialize(const TSharedRef<FContextualAnimViewModel>& ViewModelRef)
{
	ViewModelPtr = ViewModelRef;
}

void UContextualAnimMovieSceneSequence::BindPossessableObject(const FGuid& ObjectId, UObject& PossessedObject, UObject* Context)
{
	if (AActor* Actor = Cast<AActor>(&PossessedObject))
	{
		BoundActors.Add(ObjectId, Actor);
	}
}

bool UContextualAnimMovieSceneSequence::CanPossessObject(UObject& Object, UObject* InPlaybackContext) const
{
	return true;
}

void UContextualAnimMovieSceneSequence::LocateBoundObjects(const FGuid& ObjectId, UObject* Context, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const
{
	const TWeakObjectPtr<AActor>* WeakActorPtr = BoundActors.Find(ObjectId);
	if (WeakActorPtr && WeakActorPtr->IsValid())
	{
		OutObjects.Add(WeakActorPtr->Get());
	}
}

UObject* UContextualAnimMovieSceneSequence::GetParentObject(UObject* Object) const
{
	return nullptr;
}

void UContextualAnimMovieSceneSequence::UnbindPossessableObjects(const FGuid& ObjectId)
{
	BoundActors.Remove(ObjectId);
}

void UContextualAnimMovieSceneSequence::UnbindObjects(const FGuid& ObjectId, const TArray<UObject*>& InObjects, UObject* Context)
{
	BoundActors.Remove(ObjectId);
}

void UContextualAnimMovieSceneSequence::UnbindInvalidObjects(const FGuid& ObjectId, UObject* Context)
{
	BoundActors.Remove(ObjectId);
}

UMovieScene* UContextualAnimMovieSceneSequence::GetMovieScene() const
{
	UMovieScene* MovieScene = GetViewModel().GetMovieScene();
	checkf(MovieScene, TEXT("ContextualAnim sequence not initialized"));
	return MovieScene;
}