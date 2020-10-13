// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "MovieSceneSection.h"
#include "EntitySystem/IMovieSceneEntityProvider.h"
#include "Evaluation/IMovieSceneEvaluationHook.h"
#include "MovieSceneHookSection.generated.h"


/**
 * 
 */
UCLASS()
class MOVIESCENE_API UMovieSceneHookSection
	: public UMovieSceneSection
	, public IMovieSceneEntityProvider
	, public IMovieSceneEvaluationHook
{
public:

	GENERATED_BODY()

	UMovieSceneHookSection(const FObjectInitializer&);

	virtual TArrayView<const FFrameNumber> GetTriggerTimes() const { return TArrayView<const FFrameNumber>(); }

protected:

	/*~ Implemented in derived classes

	virtual void Begin(IMovieScenePlayer* Player, const UE::MovieScene::FEvaluationHookParams& Params) const override;
	virtual void Update(IMovieScenePlayer* Player, const UE::MovieScene::FEvaluationHookParams& Params) const override;
	virtual void End(IMovieScenePlayer* Player, const UE::MovieScene::FEvaluationHookParams& Params) const override;

	virtual void Trigger(IMovieScenePlayer* Player, const UE::MovieScene::FEvaluationHookParams& Params) const override;

	*/

	virtual void ImportEntityImpl(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity) override;
	virtual bool PopulateEvaluationFieldImpl(const TRange<FFrameNumber>& EffectiveRange, const FMovieSceneEvaluationFieldEntityMetaData& InMetaData, FMovieSceneEntityComponentFieldBuilder* OutFieldBuilder) override;

	void ImportRangedEntity(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity);
	void ImportTriggerEntity(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity);

protected:

	UPROPERTY()
	uint8 bRequiresRangedHook : 1;

	UPROPERTY()
	uint8 bRequiresTriggerHooks : 1;
};

