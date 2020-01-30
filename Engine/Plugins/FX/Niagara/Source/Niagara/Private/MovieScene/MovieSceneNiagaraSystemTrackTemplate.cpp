// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneNiagaraSystemTrackTemplate.h"
#include "MovieSceneExecutionToken.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemInstance.h"
#include "IMovieScenePlayer.h"

#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"

struct FPreAnimatedNiagaraComponentToken : IMovieScenePreAnimatedToken
{
	FPreAnimatedNiagaraComponentToken(bool bInComponentIsActive, bool bInComponentForceSolo, bool bInComponentRenderingEnabled, TOptional<ENiagaraExecutionState> InSystemInstanceExecutionState, ENiagaraAgeUpdateMode InComponentAgeUpdateMode, float InComponentSeekDelta, float InComponentDesiredAge)
		: bComponentIsActive(bInComponentIsActive)
		, bComponentForceSolo(bInComponentForceSolo)
		, bComponentRenderingEnabled(bInComponentRenderingEnabled)
		, SystemInstanceExecutionState(InSystemInstanceExecutionState)
		, ComponentAgeUpdateMode(InComponentAgeUpdateMode)
		, ComponentSeekDelta(InComponentSeekDelta)
		, ComponentDesiredAge(InComponentDesiredAge)
	{ }

	virtual void RestoreState(UObject& InObject, IMovieScenePlayer& InPlayer)
	{
		UNiagaraComponent* NiagaraComponent = CastChecked<UNiagaraComponent>(&InObject);
		FNiagaraSystemInstance* SystemInstance = NiagaraComponent->GetSystemInstance();
		if (bComponentIsActive)
		{
			NiagaraComponent->Activate();
		}
		else
		{
			if (SystemInstance != nullptr)
			{
				SystemInstance->Reset(FNiagaraSystemInstance::EResetMode::ResetSystem);
			}
			NiagaraComponent->Deactivate();
		}
		NiagaraComponent->SetForceSolo(bComponentForceSolo);
		NiagaraComponent->SetRenderingEnabled(bComponentRenderingEnabled);
		if (SystemInstance != nullptr && SystemInstanceExecutionState.IsSet())
		{
			SystemInstance->SetRequestedExecutionState(SystemInstanceExecutionState.GetValue());
		}
		NiagaraComponent->SetAgeUpdateMode(ComponentAgeUpdateMode);
		NiagaraComponent->SetSeekDelta(ComponentSeekDelta);
		NiagaraComponent->SetDesiredAge(ComponentDesiredAge);
	}

	bool bComponentIsActive;
	bool bComponentForceSolo;
	bool bComponentRenderingEnabled;
	TOptional<ENiagaraExecutionState> SystemInstanceExecutionState;
	ENiagaraAgeUpdateMode ComponentAgeUpdateMode;
	float ComponentSeekDelta;
	float ComponentDesiredAge;
};

struct FPreAnimatedNiagaraComponentTokenProducer : IMovieScenePreAnimatedTokenProducer
{
	virtual IMovieScenePreAnimatedTokenPtr CacheExistingState(UObject& InObject) const override
	{
		UNiagaraComponent* NiagaraComponent = CastChecked<UNiagaraComponent>(&InObject);
		FNiagaraSystemInstance* SystemInstance = NiagaraComponent->GetSystemInstance();
		return FPreAnimatedNiagaraComponentToken(
			NiagaraComponent->IsActive(),
			NiagaraComponent->GetForceSolo(),
			NiagaraComponent->GetRenderingEnabled(),
			SystemInstance != nullptr ? SystemInstance->GetRequestedExecutionState() : TOptional<ENiagaraExecutionState>(),
			NiagaraComponent->GetAgeUpdateMode(),
			NiagaraComponent->GetSeekDelta(),
			NiagaraComponent->GetDesiredAge());
	}
};

struct FNiagaraSystemUpdateDesiredAgeExecutionToken : IMovieSceneExecutionToken
{
	FNiagaraSystemUpdateDesiredAgeExecutionToken(FFrameNumber InSpawnSectionStartFrame, FFrameNumber InSpawnSectionEndFrame)
		: SpawnSectionStartFrame(InSpawnSectionStartFrame)
		, SpawnSectionEndFrame(InSpawnSectionEndFrame)
		
	{}

	virtual void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override
	{
		for (TWeakObjectPtr<> Object : Player.FindBoundObjects(Operand))
		{
			UObject* ObjectPtr = Object.Get();
			UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(ObjectPtr);

			static FMovieSceneAnimTypeID TypeID = TMovieSceneAnimTypeID<FNiagaraSystemUpdateDesiredAgeExecutionToken, 0>();
			Player.SavePreAnimatedState(*NiagaraComponent, TypeID, FPreAnimatedNiagaraComponentTokenProducer(), PersistentData.GetTrackKey());

			NiagaraComponent->SetForceSolo(true);
			NiagaraComponent->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);

			UMovieSceneSequence* MovieSceneSequence = Player.GetEvaluationTemplate().GetSequence(Operand.SequenceID);
			if (MovieSceneSequence != nullptr)
			{
				UMovieScene* MovieScene = MovieSceneSequence->GetMovieScene();
				if (MovieScene != nullptr)
				{
					NiagaraComponent->SetSeekDelta((float)MovieScene->GetDisplayRate().Denominator / MovieScene->GetDisplayRate().Numerator);
				}
			}

			FNiagaraSystemInstance* SystemInstance = NiagaraComponent->GetSystemInstance();

			if (Context.GetTime() < SpawnSectionStartFrame)
			{
				if (NiagaraComponent->IsActive() && SystemInstance != nullptr)
				{
					SystemInstance->SetRequestedExecutionState(ENiagaraExecutionState::Complete);
				}
			}
			else if (Context.GetTime() < SpawnSectionEndFrame)
			{
				if (NiagaraComponent->IsActive() == false)
				{
					NiagaraComponent->Activate();
				}
				
				if (SystemInstance != nullptr)
				{
					SystemInstance->SetRequestedExecutionState(ENiagaraExecutionState::Active);
				}
			}
			else
			{
				if (SystemInstance != nullptr)
				{
					SystemInstance->SetRequestedExecutionState(ENiagaraExecutionState::Inactive);
				}
			}

			bool bRenderingEnabled = Context.IsPreRoll() == false;
			NiagaraComponent->SetRenderingEnabled(bRenderingEnabled);

			if (SystemInstance != nullptr && SystemInstance->IsComplete() == false)
			{
				float DesiredAge = Context.GetFrameRate().AsSeconds(Context.GetTime() - SpawnSectionStartFrame);
				if (DesiredAge >= 0)
				{
					// Add a quarter of a frame offset here to push the desired age into the middle of the frame since it will be automatically rounded
					// down to the nearest seek delta.  This prevents a situation where float rounding results in a value which is just slightly less than
					// the frame boundary, which results in a skipped simulation frame.
					float FrameOffset = NiagaraComponent->GetSeekDelta() / 4;
					NiagaraComponent->SetDesiredAge(DesiredAge + FrameOffset);
				}
			}
		}
	}

	FFrameNumber SpawnSectionStartFrame;
	FFrameNumber SpawnSectionEndFrame;
};

FMovieSceneNiagaraSystemTrackImplementation::FMovieSceneNiagaraSystemTrackImplementation(FFrameNumber InSpawnSectionStartFrame, FFrameNumber InSpawnSectionEndFrame)
	: SpawnSectionStartFrame(InSpawnSectionStartFrame)
	, SpawnSectionEndFrame(InSpawnSectionEndFrame)
{}

void FMovieSceneNiagaraSystemTrackImplementation::Evaluate(const FMovieSceneEvaluationTrack& Track, FMovieSceneSegmentIdentifier SegmentID, const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	ExecutionTokens.SetContext(Context);
	ExecutionTokens.Add(FNiagaraSystemUpdateDesiredAgeExecutionToken(SpawnSectionStartFrame, SpawnSectionEndFrame));
}