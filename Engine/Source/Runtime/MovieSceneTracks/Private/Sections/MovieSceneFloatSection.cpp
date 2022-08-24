// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneFloatSection.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneSectionChannelOverrideRegistry.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntityBuilder.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "Evaluation/MovieSceneEvaluationCustomVersion.h"
#include "Evaluation/MovieScenePropertyTemplate.h"
#include "MovieSceneCommonHelpers.h"
#include "MovieSceneTracksComponentTypes.h"
#include "Systems/MovieSceneFloatPropertySystem.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "UObject/SequencerObjectVersion.h"

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

bool UMovieSceneFloatSection::PopulateEvaluationFieldImpl(const TRange<FFrameNumber>& EffectiveRange, const FMovieSceneEvaluationFieldEntityMetaData& InMetaData, FMovieSceneEntityComponentFieldBuilder* OutFieldBuilder)
{
	FMovieScenePropertyTrackEntityImportHelper::PopulateEvaluationField(*this, EffectiveRange, InMetaData, OutFieldBuilder);
	return true;
}

void UMovieSceneFloatSection::ImportEntityImpl(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity)
{
	using namespace UE::MovieScene;

	if (!FloatCurve.HasAnyData())
	{
		return;
	}

	const FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();
	const FMovieSceneTracksComponentTypes* TracksComponents = FMovieSceneTracksComponentTypes::Get();

	FPropertyTrackWithOverridableChannelsEntityImportHelper(TracksComponents->Float, this)
		.Add(Components->FloatChannel[0], FName("FloatChannel"), &FloatCurve)
		.Commit(this, Params, OutImportedEntity);
}

UMovieSceneSectionChannelOverrideRegistry* UMovieSceneFloatSection::GetChannelOverrideRegistry(bool bCreateIfMissing)
{
	if (bCreateIfMissing && OverrideRegistry == nullptr)
	{
		OverrideRegistry = NewObject<UMovieSceneSectionChannelOverrideRegistry>(this);
	}
	return OverrideRegistry;
}

UE::MovieScene::FChannelOverrideProviderTraitsHandle UMovieSceneFloatSection::GetChannelOverrideProviderTraits() const
{
	UE::MovieScene::TSingleChannelOverrideProviderTraits<FMovieSceneFloatChannel> Traits(FName("FloatChannel"));
	return UE::MovieScene::FChannelOverrideProviderTraitsHandle(Traits);
}

void UMovieSceneFloatSection::OnChannelOverridesChanged()
{
	ChannelProxy = nullptr;
}

