// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieScenePiecewiseDoubleBlenderSystem.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "Systems/DoubleChannelEvaluatorSystem.h"
#include "Systems/MovieSceneBaseValueEvaluatorSystem.h"
#include "Systems/MovieSceneQuaternionInterpolationRotationSystem.h"
#include "Systems/WeightAndEasingEvaluatorSystem.h"


DECLARE_CYCLE_STAT(TEXT("Piecewise Double Blender System"),       MovieSceneEval_PiecewiseDoubleBlenderSystem,  STATGROUP_MovieSceneECS);
DECLARE_CYCLE_STAT(TEXT("Blend double values"),                   MovieSceneEval_BlendDoubleValues,        STATGROUP_MovieSceneECS);
DECLARE_CYCLE_STAT(TEXT("Default combine blended double values"), MovieSceneEval_BlendCombineDoubleValues, STATGROUP_MovieSceneECS);
 

UMovieScenePiecewiseDoubleBlenderSystem::UMovieScenePiecewiseDoubleBlenderSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		DefineImplicitPrerequisite(UDoubleChannelEvaluatorSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(UMovieSceneBaseValueEvaluatorSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(UMovieSceneQuaternionInterpolationRotationSystem::StaticClass(), GetClass());
		DefineImplicitPrerequisite(UWeightAndEasingEvaluatorSystem::StaticClass(), GetClass());
	}
}

void UMovieScenePiecewiseDoubleBlenderSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	SCOPE_CYCLE_COUNTER(MovieSceneEval_PiecewiseDoubleBlenderSystem)

	CompactBlendChannels();

	FPiecewiseBlenderSystemImplRunParams Params;
	Params.MaximumNumBlends = AllocatedBlendChannels.Num();
	Params.BlendValuesStatId = GET_STATID(MovieSceneEval_BlendDoubleValues);
	Params.CombineBlendsStatId = GET_STATID(MovieSceneEval_BlendCombineDoubleValues);
	Impl.Run(Params, Linker->EntityManager, InPrerequisites, Subsequents);
}

FGraphEventRef UMovieScenePiecewiseDoubleBlenderSystem::DispatchDecomposeTask(const UE::MovieScene::FValueDecompositionParams& Params, UE::MovieScene::FAlignedDecomposedValue* Output)
{
	return Impl.DispatchDecomposeTask(Linker->EntityManager, Params, Output);
}

