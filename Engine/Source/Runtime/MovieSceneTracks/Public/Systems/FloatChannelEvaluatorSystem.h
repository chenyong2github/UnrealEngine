// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/SortedMap.h"
#include "Containers/Set.h"
#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/EntityAllocationIterator.h"

#include "FloatChannelEvaluatorSystem.generated.h"

namespace UE
{
namespace MovieScene
{

	struct FSourceFloatChannel;
	struct FSourceFloatChannelFlags;

} // namespace MovieScene
} // namespace UE

/**
 * System that is responsible for evaluating float channels.
 */
UCLASS()
class MOVIESCENETRACKS_API UFloatChannelEvaluatorSystem : public UMovieSceneEntitySystem
{
public:


	GENERATED_BODY()

	UFloatChannelEvaluatorSystem(const FObjectInitializer& ObjInit);

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
	virtual bool IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const override;

	static void RegisterChannelType(TComponentTypeID<UE::MovieScene::FSourceFloatChannel> SourceChannelType, TComponentTypeID<UE::MovieScene::FSourceFloatChannelFlags> ChannelFlagsType, TComponentTypeID<float> ResultType);

private:

	struct FChannelType
	{
		TComponentTypeID<UE::MovieScene::FSourceFloatChannel> ChannelType;
		TComponentTypeID<UE::MovieScene::FSourceFloatChannelFlags> ChannelFlagsType;
		TComponentTypeID<float> ResultType;
	};

	static TArray<FChannelType, TInlineAllocator<16>> StaticChannelTypes;
};