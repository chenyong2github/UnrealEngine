// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/WeightAndEasingEvaluatorSystem.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/EntityAllocationIterator.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "Systems/FloatChannelEvaluatorSystem.h"
#include "MovieSceneSection.h"

#include "EntitySystem/MovieSceneEvalTimeSystem.h"

#include "Algo/Find.h"

DECLARE_CYCLE_STAT(TEXT("MovieScene: Evaluate easing"), MovieSceneEval_EvaluateEasingTask, STATGROUP_MovieSceneECS);

namespace UE
{
namespace MovieScene
{

struct FEvaluateEasings
{
	void ForEachAllocation(const FEntityAllocation* InAllocation, TRead<FFrameTime> TimeData, TReadOptional<FEasingComponentData> EasingData, TReadOptional<float> WeightData, TWrite<float> EasingResultData)
	{
		const int32 Num          = InAllocation->Num();
		const FFrameTime* Times  = TimeData.Resolve(InAllocation);
		float* Results           = EasingResultData.Resolve(InAllocation);

		// Initialize our result array.
		{
			float* CurResult = Results;
			for (int32 Idx = 0; Idx < Num; ++Idx)
			{
				*(CurResult++) = 1.f;
			}
		}

		// Compute and add easing weight.
		const bool bHasEasing = InAllocation->HasComponent(EasingData.ComponentType);
		if (bHasEasing)
		{
			float* CurResult = Results;
			const FFrameTime* CurTime = Times;
			const FEasingComponentData* CurEasing = EasingData.Resolve(InAllocation);
			for (int32 Idx = 0; Idx < Num; ++Idx)
			{
				*(CurResult++) *= (CurEasing++)->Section->EvaluateEasing(*(CurTime++));
			}
		}

		// Manual weight has already been computed by the float channel evaluator system, so we
		// just need to pick up the result and combine it.
		const bool bHasWeight = InAllocation->HasComponent(WeightData.ComponentType);
		if (bHasWeight)
		{
			float* CurResult = Results;
			const FFrameTime* CurTime = Times;
			const float* CurWeight = WeightData.Resolve(InAllocation);
			for (int32 Idx = 0; Idx < Num; ++Idx)
			{
				*(CurResult++) *= *(CurWeight++);
			}
		}
	}
};

} // namespace MovieScene
} // namespace UE

UWeightAndEasingEvaluatorSystem::UWeightAndEasingEvaluatorSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(UMovieSceneEvalTimeSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(UFloatChannelEvaluatorSystem::StaticClass(), GetClass());
	}
}

bool UWeightAndEasingEvaluatorSystem::IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const
{
	using namespace UE::MovieScene;
	FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();

	return InLinker->EntityManager.ContainsAnyComponent({ Components->Easing, Components->WeightResult });
}

void UWeightAndEasingEvaluatorSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FBuiltInComponentTypes* Components = FBuiltInComponentTypes::Get();

	FEntityTaskBuilder()
	.Read(Components->EvalTime)
	.ReadOptional(Components->Easing)
	.ReadOptional(Components->WeightResult)
	.Write(Components->WeightAndEasingResult)
	.SetStat(GET_STATID(MovieSceneEval_EvaluateEasingTask))
	.Dispatch_PerAllocation<FEvaluateEasings>(&Linker->EntityManager, InPrerequisites, &Subsequents);
}

