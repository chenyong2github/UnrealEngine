// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieSceneBaseValueEvaluatorSystem.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneBoundObjectInstantiator.h"
#include "EntitySystem/EntityAllocationIterator.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneComponentRegistry.h"
#include "Channels/MovieSceneFloatChannel.h"

namespace UE
{
namespace MovieScene
{

struct FEvaluateBaseValues
{
	void ForEachEntity(FSourceFloatChannel FloatChannel, FFrameTime FrameTime, float& OutResult)
	{
		if (!FloatChannel.Source->Evaluate(FrameTime, OutResult))
		{
			OutResult = MIN_flt;
		}
	}
};

} // namespace MovieScene
} // namespace UE

UMovieSceneBaseValueEvaluatorSystem::UMovieSceneBaseValueEvaluatorSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	Phase = ESystemPhase::Instantiation;
	RelevantComponent = FBuiltInComponentTypes::Get()->BaseValueEvalTime;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->BoundObject);
	}
}

void UMovieSceneBaseValueEvaluatorSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	static_assert(
			UE_ARRAY_COUNT(BuiltInComponents->BaseFloat) == UE_ARRAY_COUNT(BuiltInComponents->FloatChannel),
			"There should be a matching number of float channels and float base values.");
	for (size_t Index = 0; Index < UE_ARRAY_COUNT(BuiltInComponents->BaseFloat); ++Index)
	{
		const TComponentTypeID<float> BaseFloat = BuiltInComponents->BaseFloat[Index];
		const TComponentTypeID<FSourceFloatChannel> FloatChannel = BuiltInComponents->FloatChannel[Index];

		FEntityTaskBuilder()
		.Read(FloatChannel)
		.Read(BuiltInComponents->BaseValueEvalTime)
		.Write(BaseFloat)
		.FilterAll({ BuiltInComponents->Tags.NeedsLink })
		.FilterNone({ BuiltInComponents->Tags.Ignored })
		.RunInline_PerEntity(&Linker->EntityManager, FEvaluateBaseValues());
	}
}
