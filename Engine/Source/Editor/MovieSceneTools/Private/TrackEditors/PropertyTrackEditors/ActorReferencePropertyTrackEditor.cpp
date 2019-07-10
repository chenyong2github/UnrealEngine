// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TrackEditors/PropertyTrackEditors/ActorReferencePropertyTrackEditor.h"
#include "GameFramework/Actor.h"


TSharedRef<ISequencerTrackEditor> FActorReferencePropertyTrackEditor::CreateTrackEditor( TSharedRef<ISequencer> OwningSequencer )
{
	return MakeShareable(new FActorReferencePropertyTrackEditor(OwningSequencer));
}

void FActorReferencePropertyTrackEditor::GenerateKeysFromPropertyChanged( const FPropertyChangedParams& PropertyChangedParams, FGeneratedTrackKeys& OutGeneratedKeys )
{
	// Care is taken here to ensure that GetPropertyValue is templated on UObject* which causes it to use the correct instantiation of GetPropertyValueImpl
	AActor* NewReferencedActor = Cast<AActor>(PropertyChangedParams.GetPropertyValue<UObject*>());
	if ( NewReferencedActor != nullptr )
	{
		FGuid ActorGuid = GetSequencer()->GetHandleToObject( NewReferencedActor );
		if ( ActorGuid.IsValid() )
		{
			FMovieSceneActorReferenceKey NewKey;
			NewKey.Object = FMovieSceneObjectBindingID(ActorGuid, MovieSceneSequenceID::Root, EMovieSceneObjectBindingSpace::Local);
			OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneActorReferenceData>(0, NewKey, true));
		}
	}
}
