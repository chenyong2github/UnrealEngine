// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "UObject/SequencerObjectVersion.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "MovieSceneCommonHelpers.h"
#include "Evaluation/MovieScenePropertyTemplate.h"
#include "Evaluation/MovieSceneEvaluationCustomVersion.h"

#include "EntitySystem/MovieSceneEntityManager.h"
#include "EntitySystem/MovieSceneEntityBuilder.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "MovieSceneTracksComponentTypes.h"
#include "Systems/MovieSceneFloatPropertySystem.h"

UMovieSceneFloatSection::UMovieSceneFloatSection( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	EvalOptions.EnableAndSetCompletionMode
		(GetLinkerCustomVersion(FSequencerObjectVersion::GUID) < FSequencerObjectVersion::WhenFinishedDefaultsToRestoreState ? 
			EMovieSceneCompletionMode::KeepState : 
			GetLinkerCustomVersion(FSequencerObjectVersion::GUID) < FSequencerObjectVersion::WhenFinishedDefaultsToProjectDefault ? 
			EMovieSceneCompletionMode::RestoreState : 
			EMovieSceneCompletionMode::ProjectDefault);
	BlendType = EMovieSceneBlendType::Absolute;
	bSupportsInfiniteRange = true;
}

EMovieSceneChannelProxyType UMovieSceneFloatSection::CacheChannelProxy()
{
#if WITH_EDITOR

	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(FloatCurve, FMovieSceneChannelMetaData(), TMovieSceneExternalValue<float>::Make());

#else

	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(FloatCurve);

#endif

	return EMovieSceneChannelProxyType::Static;
}

void UMovieSceneFloatSection::ImportEntityImpl(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity)
{
	using namespace UE::MovieScene;

	if (!FloatCurve.HasAnyData())
	{
		return;
	}

	FGuid ObjectBindingID = Params.GetObjectBindingID();

	FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();
	FMovieSceneTracksComponentTypes* TracksComponents = FMovieSceneTracksComponentTypes::Get();

	if (UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(GetOuter()))
	{
		OutImportedEntity->AddBuilder(
			FEntityBuilder()
			.Add(Components->FloatChannel[0], &FloatCurve)
			.Add(Components->PropertyBinding, PropertyTrack->GetPropertyBinding())
			.AddConditional(Components->GenericObjectBinding, ObjectBindingID, ObjectBindingID.IsValid())
			.AddTag(TracksComponents->Float.PropertyTag)
		);
	}
	else
	{
		OutImportedEntity->AddBuilder(
			FEntityBuilder()
			.Add(Components->FloatChannel[0], &FloatCurve)
			.AddConditional(Components->GenericObjectBinding, ObjectBindingID, ObjectBindingID.IsValid())
			.AddTag(TracksComponents->Float.PropertyTag)
		);
	}
}