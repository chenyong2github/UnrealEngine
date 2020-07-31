// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieScenePropertyComponentHandler.h"
#include "EntitySystem/MovieSceneOverlappingEntityTracker.h"

#include "SequencerInterrogatedPropertyInstantiator.generated.h"

class UMovieSceneBlenderSystem;

/** Class responsible for resolving all property types registered with FBuiltInComponentTypes::PropertyRegistry */
UCLASS()
class SEQUENCER_API USequencerInterrogatedPropertyInstantiatorSystem
	: public UMovieSceneEntityInstantiatorSystem
{
public:

	using FMovieSceneEntityID = UE::MovieScene::FMovieSceneEntityID;

	GENERATED_BODY()

	USequencerInterrogatedPropertyInstantiatorSystem(const FObjectInitializer& ObjInit);


	struct FPropertyInfo
	{
		FPropertyInfo()
			: BlendChannel(INVALID_BLEND_CHANNEL)
		{}
		/** POinter to the blender system to use for this property, if its blended */
		TWeakObjectPtr<UMovieSceneBlenderSystem> Blender;
		UE::MovieScene::FInterrogationChannel InterrogationChannel;
		/** The entity that contains the property component itself. For fast path properties this is the actual child entity produced from the bound object instantiators. */
		UE::MovieScene::FMovieSceneEntityID PropertyEntityID;
		/** Blend channel allocated from Blender, or INVALID_BLEND_CHANNEL if unblended. */
		uint16 BlendChannel;
	};

	void InitializeOutput(UE::MovieScene::FInterrogationChannel InterrogationChannel, TArrayView<const FMovieSceneEntityID> Inputs, FPropertyInfo* Output, UE::MovieScene::FEntityOutputAggregate Aggregate);
	void UpdateOutput(UE::MovieScene::FInterrogationChannel InterrogationChannel, TArrayView<const FMovieSceneEntityID> Inputs, FPropertyInfo* Output, UE::MovieScene::FEntityOutputAggregate Aggregate);
	void DestroyOutput(UE::MovieScene::FInterrogationChannel InterrogationChannel, FPropertyInfo* Output, UE::MovieScene::FEntityOutputAggregate Aggregate);

private:

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
	virtual bool IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const override;

	bool PropertySupportsFastPath(TArrayView<const FMovieSceneEntityID> Inputs, FPropertyInfo* Output) const;
	UClass* ResolveBlenderClass(TArrayView<const FMovieSceneEntityID> Inputs) const;

private:

	static constexpr uint16 INVALID_BLEND_CHANNEL = uint16(-1);

	UE::MovieScene::TOverlappingEntityTracker<UE::MovieScene::FInterrogationChannel, FPropertyInfo> PropertyTracker;
	UE::MovieScene::FComponentMask CleanFastPathMask;
	UE::MovieScene::FBuiltInComponentTypes* BuiltInComponents;
};

