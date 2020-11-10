// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieScene/MovieSceneNiagaraSystemSpawnSection.h"

UMovieSceneNiagaraSystemSpawnSection::UMovieSceneNiagaraSystemSpawnSection()
{
	EvalOptions.CompletionMode = EMovieSceneCompletionMode::RestoreState;
	SectionStartBehavior = ENiagaraSystemSpawnSectionStartBehavior::Activate;
	SectionEvaluateBehavior = ENiagaraSystemSpawnSectionEvaluateBehavior::ActivateIfInactive;
	SectionEndBehavior = ENiagaraSystemSpawnSectionEndBehavior::SetSystemInactive;
	AgeUpdateMode = ENiagaraAgeUpdateMode::TickDeltaTime;
}

ENiagaraSystemSpawnSectionStartBehavior UMovieSceneNiagaraSystemSpawnSection::GetSectionStartBehavior() const
{
	return SectionStartBehavior;
}

ENiagaraSystemSpawnSectionEvaluateBehavior UMovieSceneNiagaraSystemSpawnSection::GetSectionEvaluateBehavior() const
{
	return SectionEvaluateBehavior;
}

ENiagaraSystemSpawnSectionEndBehavior UMovieSceneNiagaraSystemSpawnSection::GetSectionEndBehavior() const
{
	return SectionEndBehavior;
}

ENiagaraAgeUpdateMode UMovieSceneNiagaraSystemSpawnSection::GetAgeUpdateMode() const
{
	return AgeUpdateMode;
}


