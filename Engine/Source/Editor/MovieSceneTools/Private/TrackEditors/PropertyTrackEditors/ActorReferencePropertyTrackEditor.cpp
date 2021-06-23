// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrackEditors/PropertyTrackEditors/ActorReferencePropertyTrackEditor.h"
#include "GameFramework/Actor.h"
#include "MovieSceneSpawnableAnnotation.h"


TSharedRef<ISequencerTrackEditor> FActorReferencePropertyTrackEditor::CreateTrackEditor( TSharedRef<ISequencer> OwningSequencer )
{
	return MakeShareable(new FActorReferencePropertyTrackEditor(OwningSequencer));
}

void FActorReferencePropertyTrackEditor::OnAnimatedPropertyChanged( const FPropertyChangedParams& PropertyChangedParams )
{
	// Override the params by always manually adding a key/track so that we don't reference other spawnables and their levels. Override only if it's set to autokey, so that if a key is manually forced, it will still be created
	ESequencerKeyMode OverrideKeyMode = PropertyChangedParams.KeyMode;
	if (OverrideKeyMode == ESequencerKeyMode::AutoKey)
	{
		OverrideKeyMode = ESequencerKeyMode::ManualKey;
	}
	FPropertyChangedParams OverridePropertyChangedParams(PropertyChangedParams.ObjectsThatChanged, PropertyChangedParams.PropertyPath, PropertyChangedParams.StructPathToKey, OverrideKeyMode);

	FPropertyTrackEditor::OnAnimatedPropertyChanged( OverridePropertyChangedParams );
}

void FActorReferencePropertyTrackEditor::GenerateKeysFromPropertyChanged( const FPropertyChangedParams& PropertyChangedParams, UMovieSceneSection* SectionToKey, FGeneratedTrackKeys& OutGeneratedKeys )
{
	// Care is taken here to ensure that GetPropertyValue is templated on UObject* which causes it to use the correct instantiation of GetPropertyValueImpl
	AActor* NewReferencedActor = Cast<AActor>(PropertyChangedParams.GetPropertyValue<UObject*>());
	if ( NewReferencedActor != nullptr )
	{
		TSharedPtr<ISequencer> SequencerPtr = GetSequencer();

		FMovieSceneObjectBindingID Binding;

		TOptional<FMovieSceneSpawnableAnnotation> Spawnable = FMovieSceneSpawnableAnnotation::Find(NewReferencedActor);
		if (Spawnable.IsSet())
		{
			// Check whether the spawnable is underneath the current sequence, if so, we can remap it to a local sequence ID
			Binding = UE::MovieScene::FRelativeObjectBindingID(SequencerPtr->GetFocusedTemplateID(), Spawnable->SequenceID, Spawnable->ObjectBindingID, *SequencerPtr);
		}
		else
		{
			FGuid ParentActorId = FindOrCreateHandleToObject(NewReferencedActor).Handle;
			Binding = UE::MovieScene::FRelativeObjectBindingID(ParentActorId);
		}

		if (Binding.IsValid())
		{
			FMovieSceneActorReferenceKey NewKey;
			NewKey.Object = Binding;
			OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneActorReferenceData>(0, NewKey, true));
		}
	}
}
