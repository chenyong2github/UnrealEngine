// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieScenePiecewiseFloatBlenderSystem.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "Systems/FloatChannelEvaluatorSystem.h"
#include "Systems/MovieSceneBaseValueEvaluatorSystem.h"
#include "Systems/MovieScenePiecewiseBlenderSystemImpl.h"
#include "Systems/MovieSceneQuaternionInterpolationRotationSystem.h"
#include "Systems/WeightAndEasingEvaluatorSystem.h"


DECLARE_CYCLE_STAT(TEXT("Piecewise Float Blender System"),       MovieSceneEval_PiecewiseFloatBlenderSystem,  STATGROUP_MovieSceneECS);
DECLARE_CYCLE_STAT(TEXT("Blend float values"),                   MovieSceneEval_BlendFloatValues,        STATGROUP_MovieSceneECS);
DECLARE_CYCLE_STAT(TEXT("Default combine blended float values"), MovieSceneEval_BlendCombineFloatValues, STATGROUP_MovieSceneECS);
 

UMovieScenePiecewiseFloatBlenderSystem::UMovieScenePiecewiseFloatBlenderSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(UFloatChannelEvaluatorSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(UMovieSceneBaseValueEvaluatorSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(UMovieSceneQuaternionInterpolationRotationSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(UWeightAndEasingEvaluatorSystem::StaticClass(), GetClass());
	}
}

void UMovieScenePiecewiseFloatBlenderSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	SCOPE_CYCLE_COUNTER(MovieSceneEval_PiecewiseFloatBlenderSystem)

	CompactBlendChannels();

	FPiecewiseBlenderSystemImplRunParams Params;
	Params.MaximumNumBlends = AllocatedBlendChannels.Num();
	Params.BlendValuesStatId = GET_STATID(MovieSceneEval_BlendFloatValues);
	Params.CombineBlendsStatId = GET_STATID(MovieSceneEval_BlendCombineFloatValues);
	Impl.Run(Params, Linker->EntityManager, InPrerequisites, Subsequents);
}

FGraphEventRef UMovieScenePiecewiseFloatBlenderSystem::DispatchDecomposeTask(const UE::MovieScene::FValueDecompositionParams& Params, UE::MovieScene::FAlignedDecomposedValue* Output)
{
	return Impl.DispatchDecomposeTask(Linker->EntityManager, Params, Output);
}

