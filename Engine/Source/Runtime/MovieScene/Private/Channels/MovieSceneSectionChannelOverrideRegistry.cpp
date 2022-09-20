// Copyright Epic Games, Inc. All Rights Reserved.

#include "Channels/MovieSceneSectionChannelOverrideRegistry.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneChannelOverrideContainer.h"
#include "Channels/IMovieSceneChannelOverrideProvider.h"
#include "EntitySystem/MovieSceneEntityBuilder.h"

UMovieSceneSectionChannelOverrideRegistry::UMovieSceneSectionChannelOverrideRegistry()
{
}

void UMovieSceneSectionChannelOverrideRegistry::AddChannel(FName ChannelName, UMovieSceneChannelOverrideContainer* ChannelContainer)
{
	Overrides.Emplace(ChannelName, ChannelContainer);
}

bool UMovieSceneSectionChannelOverrideRegistry::ContainsChannel(FName ChannelName) const
{
	return Overrides.Contains(ChannelName);
}

int32 UMovieSceneSectionChannelOverrideRegistry::NumChannels() const
{
	return Overrides.Num();
}

UMovieSceneChannelOverrideContainer* UMovieSceneSectionChannelOverrideRegistry::GetChannel(FName ChannelName) const
{
	return Overrides.FindRef(ChannelName);
}

void UMovieSceneSectionChannelOverrideRegistry::RemoveChannel(FName ChannelName)
{
	Overrides.Remove(ChannelName);
}

void UMovieSceneSectionChannelOverrideRegistry::ImportEntityImpl(const UE::MovieScene::FChannelOverrideEntityImportParams& OverrideParams, const UE::MovieScene::FEntityImportParams& ImportParams, UE::MovieScene::FImportedEntity* OutImportedEntity)
{
	TObjectPtr<UMovieSceneChannelOverrideContainer>* ContainerPtr = Overrides.Find(OverrideParams.ChannelName);
	if (ensure(ContainerPtr))
	{
		(*ContainerPtr)->ImportEntityImpl(OverrideParams, ImportParams, OutImportedEntity);
	}
}

void UMovieSceneSectionChannelOverrideRegistry::PopulateEvaluationFieldImpl(const TRange<FFrameNumber>& EffectiveRange, const FMovieSceneEvaluationFieldEntityMetaData& InMetaData, FMovieSceneEntityComponentFieldBuilder* OutFieldBuilder, UMovieSceneSection& OwnerSection)
{
	using namespace UE::MovieScene;

	IMovieSceneChannelOverrideProvider* OverrideProvider = Cast<IMovieSceneChannelOverrideProvider>(GetOuter());
	if (ensure(OverrideProvider))
	{
		for (const TPair<FName, TObjectPtr<UMovieSceneChannelOverrideContainer>>& Override : Overrides)
		{
			FChannelOverrideProviderTraitsHandle ChannelOverrideTraits = OverrideProvider->GetChannelOverrideProviderTraits();
			check(ChannelOverrideTraits.IsValid());
			const int32 EntityID = ChannelOverrideTraits->GetChannelOverrideEntityID(Override.Key);
			const int32 EntityIndex = OutFieldBuilder->FindOrAddEntity(&OwnerSection, EntityID);
			const int32 MetaDataIndex = OutFieldBuilder->AddMetaData(InMetaData);
			OutFieldBuilder->AddPersistentEntity(EffectiveRange, EntityIndex, MetaDataIndex);
		}
	}
}
