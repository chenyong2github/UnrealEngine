// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntitySystem.h"
#include "ByteChannelEvaluatorSystem.generated.h"

namespace UE
{
namespace MovieScene
{

	struct FSourceByteChannel;

} // namespace MovieScene
} // namespace UE

/**
 * System that is responsible for evaluating byte channels.
 */
UCLASS()
class MOVIESCENETRACKS_API UByteChannelEvaluatorSystem : public UMovieSceneEntitySystem
{
public:

	GENERATED_BODY()

	UByteChannelEvaluatorSystem(const FObjectInitializer& ObjInit);

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
};
