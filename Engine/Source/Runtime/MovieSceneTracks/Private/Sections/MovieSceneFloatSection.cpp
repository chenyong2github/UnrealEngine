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

namespace UE
{
namespace MovieScene
{

const int32 FloatSectionFloatChannelImportingID = 0;
const int32 FloatSectionBoolToggleImportingID = 1;

}
}

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
	using namespace UE::MovieScene;

	// Add the default entity for this section.
	const int32 EntityIndex   = OutFieldBuilder->FindOrAddEntity(this, FloatSectionFloatChannelImportingID);
	const int32 MetaDataIndex = OutFieldBuilder->AddMetaData(InMetaData);
	OutFieldBuilder->AddPersistentEntity(EffectiveRange, EntityIndex, MetaDataIndex);

	if (UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(GetOuter()))
	{
		const FMovieScenePropertyBinding& PropertyBinding = PropertyTrack->GetPropertyBinding();

		TArray<FString> PropertyNames;
		PropertyBinding.PropertyPath.ToString().ParseIntoArray(PropertyNames, TEXT("."), true);
		// If we are writing to a PostProcessSettings property, add another entity to flip the corresponding override toggle property.
		if (PropertyNames.Num() >= 2 && PropertyNames[PropertyNames.Num() - 2] == "PostProcessSettings")
		{
			const int32 OverrideToggleEntityIndex = OutFieldBuilder->FindOrAddEntity(this, FloatSectionBoolToggleImportingID);
			const int32 OverrideToggleMetaDataIndex = OutFieldBuilder->AddMetaData(InMetaData);
			OutFieldBuilder->AddPersistentEntity(EffectiveRange, OverrideToggleEntityIndex, OverrideToggleMetaDataIndex);
		}
	}

	return true;
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
		if (Params.EntityID == FloatSectionFloatChannelImportingID)
		{
			OutImportedEntity->AddBuilder(
				FEntityBuilder()
				.Add(Components->FloatChannel[0], &FloatCurve)
				.Add(Components->PropertyBinding, PropertyTrack->GetPropertyBinding())
				.AddConditional(Components->GenericObjectBinding, ObjectBindingID, ObjectBindingID.IsValid())
				.AddTag(TracksComponents->Float.PropertyTag)
			);
		}
		else if (Params.EntityID == FloatSectionBoolToggleImportingID)
		{
			const FMovieScenePropertyBinding& PropertyBinding = PropertyTrack->GetPropertyBinding();

			TArray<FString> PropertyNames;
			PropertyBinding.PropertyPath.ToString().ParseIntoArray(PropertyNames, TEXT("."), true);
			check(PropertyNames.Num() >= 2);
			PropertyNames[PropertyNames.Num() - 1].InsertAt(0, TEXT("bOverride_"));

			const FName OverrideTogglePropertyName(PropertyNames[PropertyNames.Num() - 1]);
			const FString OverrideTogglePropertyPath = FString::Join(PropertyNames, TEXT("."));

			FMovieScenePropertyBinding OverrideTogglePropertyBinding(OverrideTogglePropertyName, OverrideTogglePropertyPath);

			OutImportedEntity->AddBuilder(
				FEntityBuilder()
				.Add(Components->BoolResult, true)
				.Add(Components->PropertyBinding, OverrideTogglePropertyBinding)
				.AddConditional(Components->GenericObjectBinding, ObjectBindingID, ObjectBindingID.IsValid())
				.AddTag(TracksComponents->Bool.PropertyTag)
			);
		}
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
