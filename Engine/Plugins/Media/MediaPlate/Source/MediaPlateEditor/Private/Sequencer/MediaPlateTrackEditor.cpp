// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MediaPlateTrackEditor.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ISequencer.h"
#include "MediaTexture.h"
#include "MediaPlate.h"
#include "MediaPlateComponent.h"
#include "MovieSceneMediaTrack.h"

#define LOCTEXT_NAMESPACE "FMediaPlateTrackEditor"

/* FMediaTrackEditor static functions
 *****************************************************************************/

TArray<FAnimatedPropertyKey, TInlineAllocator<1>> FMediaPlateTrackEditor::GetAnimatedPropertyTypes()
{
	return TArray<FAnimatedPropertyKey, TInlineAllocator<1>>({ FAnimatedPropertyKey::FromObjectType(UMediaTexture::StaticClass()) });
}

/* FMediaTrackEditor structors
 *****************************************************************************/

FMediaPlateTrackEditor::FMediaPlateTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer)
{
	OnActorAddedToSequencerHandle = InSequencer->OnActorAddedToSequencer().AddRaw(this, &FMediaPlateTrackEditor::HandleActorAdded);
}

FMediaPlateTrackEditor::~FMediaPlateTrackEditor()
{
}

void FMediaPlateTrackEditor::BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	// Is this a media plate?
	if (ObjectClass != nullptr)
	{
		if (ObjectClass->IsChildOf(AMediaPlate::StaticClass()))
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("AddTrack", "Media"),
				LOCTEXT("AddAttachedTooltip", "Adds a media track attached to the object."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateRaw(this, &FMediaPlateTrackEditor::HandleAddMediaTrackToObjectBindingMenuEntryExecute, ObjectBindings)));
		}
	}
}

bool FMediaPlateTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> TrackClass) const
{
	return false;
}

void FMediaPlateTrackEditor::HandleAddMediaTrackToObjectBindingMenuEntryExecute(TArray<FGuid> InObjectBindingIDs)
{
	UMovieScene* FocusedMovieScene = GetFocusedMovieScene();

	if (FocusedMovieScene == nullptr)
	{
		return;
	}

	if (FocusedMovieScene->IsReadOnly())
	{
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("Sequencer", "AddMediaTrack_Transaction", "Add Media Track"));
	FocusedMovieScene->Modify();

	// Loop through all objects.
	for (FGuid InObjectBindingID : InObjectBindingIDs)
	{
		if (InObjectBindingID.IsValid())
		{
			// Add media track.
			UMovieSceneMediaTrack* NewObjectTrack = FocusedMovieScene->AddTrack<UMovieSceneMediaTrack>(InObjectBindingID);
			NewObjectTrack->SetDisplayName(LOCTEXT("MediaTrackName", "Media"));

			if (GetSequencer().IsValid())
			{
				GetSequencer()->OnAddTrack(NewObjectTrack, InObjectBindingID);
			}
		}
	}
}

void FMediaPlateTrackEditor::HandleActorAdded(AActor* Actor, FGuid TargetObjectGuid)
{
	if (Actor)
	{
		if (UMediaPlateComponent* MediaPlateComponent = Actor->FindComponentByClass<UMediaPlateComponent>())
		{
			AddTrackForComponent(MediaPlateComponent);
		}
	}
}

void FMediaPlateTrackEditor::AddTrackForComponent(UMediaPlateComponent* Component)
{
	// Get object.
	UObject* Object = Component->GetOwner();
	FFindOrCreateHandleResult HandleResult = FindOrCreateHandleToObject(Object);
	FGuid ObjectHandle = HandleResult.Handle;

	// Add media track.
	FFindOrCreateTrackResult TrackResult = FindOrCreateTrackForObject(ObjectHandle, UMovieSceneMediaTrack::StaticClass());
	UMovieSceneTrack* Track = TrackResult.Track;
	if (Track != nullptr)
	{
		UMovieSceneMediaTrack* MediaTrack = Cast<UMovieSceneMediaTrack>(Track);
		MediaTrack->SetDisplayName(LOCTEXT("MediaTrackName", "Media"));
	}
}

void FMediaPlateTrackEditor::OnRelease()
{
	if (GetSequencer().IsValid())
	{
		if (OnActorAddedToSequencerHandle.IsValid())
		{
			GetSequencer()->OnActorAddedToSequencer().Remove(OnActorAddedToSequencerHandle);
		}
	}

	FMovieSceneTrackEditor::OnRelease();
}

#undef LOCTEXT_NAMESPACE
