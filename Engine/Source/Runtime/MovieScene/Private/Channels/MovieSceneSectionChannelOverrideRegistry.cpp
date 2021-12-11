// Copyright Epic Games, Inc. All Rights Reserved.

#include "Channels/MovieSceneSectionChannelOverrideRegistry.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneChannelOverrideContainer.h"
#include "EntitySystem/MovieSceneEntityBuilder.h"

UMovieSceneSectionChannelOverrideRegistry::UMovieSceneSectionChannelOverrideRegistry()
{
}

EMovieSceneChannelProxyType UMovieSceneSectionChannelOverrideRegistry::CacheChannelProxy()
{
	// TODO in Phase 2
	return EMovieSceneChannelProxyType();
}

void UMovieSceneSectionChannelOverrideRegistry::AddChannel(int32 ChannelToOverride, UMovieSceneChannelOverrideContainer* ChannelContainer)
{
	Overrides.Emplace(ChannelToOverride, ChannelContainer);
}

void UMovieSceneSectionChannelOverrideRegistry::RemoveChannel(int32 ChannelToRemove)
{
	Overrides.Remove(ChannelToRemove);
}

bool UMovieSceneSectionChannelOverrideRegistry::IsOverriden(int32 ChannelIndex) const
{
	return Overrides.Contains(ChannelIndex);
}

void UMovieSceneSectionChannelOverrideRegistry::ImportEntityImpl(const UE::MovieScene::FEntityImportParams& Params, UE::MovieScene::FImportedEntity* OutImportedEntity)
{
	using namespace UE::MovieScene;
;
	int32 ChannelIndex = UMovieSceneSectionChannelOverrideRegistry::ToChannelIndex(Params.EntityID);
	check(IsOverriden(ChannelIndex));	
	Overrides[ChannelIndex]->ImportEntityImpl(ChannelIndex, Params, OutImportedEntity);
}

bool UMovieSceneSectionChannelOverrideRegistry::PopulateEvaluationFieldImpl(const TRange<FFrameNumber>& EffectiveRange, const FMovieSceneEvaluationFieldEntityMetaData& InMetaData, FMovieSceneEntityComponentFieldBuilder* OutFieldBuilder, UMovieSceneSection& OwnerSection)
{
	using namespace UE::MovieScene;

	for (const TPair<int32, TObjectPtr<UMovieSceneChannelOverrideContainer>>& OverrideChannelPair : Overrides)
	{
		const int32 EntityIndex = OutFieldBuilder->FindOrAddEntity(&OwnerSection, ToEntityID(OverrideChannelPair.Key));
		const int32 MetaDataIndex = OutFieldBuilder->AddMetaData(InMetaData);
		OutFieldBuilder->AddPersistentEntity(EffectiveRange, EntityIndex, MetaDataIndex);
	}

	return false;
}
