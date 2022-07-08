// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MediaPlateTrackEditor.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ISequencer.h"
#include "MediaTexture.h"
#include "MediaPlate.h"
#include "MediaPlateComponent.h"
#include "MediaPlaylist.h"
#include "MovieSceneMediaSection.h"
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

void FMediaPlateTrackEditor::BuildObjectBindingContextMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	if ((ObjectClass != nullptr) && (ObjectClass->IsChildOf(AMediaPlate::StaticClass())))
	{
		MenuBuilder.BeginSection("Media Plate", LOCTEXT("MediaPlate", "Media Plate"));

		MenuBuilder.AddMenuEntry(
			LOCTEXT("Import", "Import"),
			LOCTEXT("ImportTooltip", "Import tracks from the Media Plate object."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this,
				&FMediaPlateTrackEditor::ImportObjectBinding, ObjectBindings)));
	
		MenuBuilder.EndSection();
	}
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

		// Populate track.
		UMediaPlaylist* Playlist = Component->MediaPlaylist;
		if (Playlist != nullptr)
		{
			for (int32 Index = 0; Index < Playlist->Num(); ++Index)
			{
				UMediaSource* MediaSource = Playlist->Get(Index);
				if (MediaSource != nullptr)
				{
					UMovieSceneSection* Section = MediaTrack->AddNewMediaSource(*MediaSource, FFrameNumber(0));
					
					// Copy cache settings from media plate.
					UMovieSceneMediaSection* MediaSection = Cast<UMovieSceneMediaSection>(Section);
					if (MediaSection != nullptr)
					{
						MediaSection->CacheSettings = Component->CacheSettings;
					}
				}
			}
		}
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

void FMediaPlateTrackEditor::ImportObjectBinding(const TArray<FGuid> ObjectBindings)
{
	// Get actor.
	const TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	for (const FGuid ObjectBinding : ObjectBindings)
	{
		UObject* BoundObject = SequencerPtr.IsValid() ? SequencerPtr->FindSpawnedObjectOrTemplate(ObjectBinding) : nullptr;
		AActor* Actor = Cast<AActor>(BoundObject);
		if (Actor != nullptr)
		{
			// Get media plate componnent.
			if (UMediaPlateComponent* MediaPlateComponent = Actor->FindComponentByClass<UMediaPlateComponent>())
			{
				// Add tracks for this.
				AddTrackForComponent(MediaPlateComponent);
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE
