// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Evaluation/MovieSceneActorReferenceTemplate.h"

#include "Sections/MovieSceneActorReferenceSection.h"
#include "Tracks/MovieSceneActorReferenceTrack.h"
#include "GameFramework/Actor.h"
#include "Evaluation/MovieSceneEvaluation.h"


namespace PropertyTemplate
{
	template<>
	UObject* ConvertFromIntermediateType<UObject*, FMovieSceneObjectBindingID>(const FMovieSceneObjectBindingID& InObjectBinding, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player)
	{
		FMovieSceneObjectBindingID ResolvedID = InObjectBinding.ResolveLocalToRoot(Operand.SequenceID, Player.GetEvaluationTemplate().GetHierarchy());

		for (TWeakObjectPtr<>& WeakObject : Player.FindBoundObjects(ResolvedID.GetGuid(), ResolvedID.GetSequenceID()))
		{
			if (AActor* Actor = Cast<AActor>(WeakObject.Get()))
			{
				return Actor;
			}
		}
		return nullptr;
	}

	template<>
	UObject* ConvertFromIntermediateType<UObject*, TWeakObjectPtr<>>(const TWeakObjectPtr<>& InWeakPtr, IMovieScenePlayer& Player)
	{
		return InWeakPtr.Get();
	}

	template<>
	UObject* ConvertFromIntermediateType<UObject*, TWeakObjectPtr<>>(const TWeakObjectPtr<>& InWeakPtr, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player)
	{
		return InWeakPtr.Get();
	}

	static bool IsValueValid(UObject* InValue)
	{
		return InValue != nullptr;
	}

	template<> IMovieScenePreAnimatedTokenPtr CacheExistingState<UObject*, FMovieSceneObjectBindingID>(UObject& Object, FTrackInstancePropertyBindings& PropertyBindings)
	{
		return TCachedState<UObject*, TWeakObjectPtr<>>(PropertyBindings.GetCurrentValue<UObject*>(Object), PropertyBindings);
	}
}

FMovieSceneActorReferenceSectionTemplate::FMovieSceneActorReferenceSectionTemplate(const UMovieSceneActorReferenceSection& Section, const UMovieScenePropertyTrack& Track)
	: PropertyData(Track.GetPropertyName(), Track.GetPropertyPath())
	, ActorReferenceData(Section.GetActorReferenceData())
{
}

void FMovieSceneActorReferenceSectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	using namespace PropertyTemplate;

	FMovieSceneActorReferenceKey ObjectBinding = ActorReferenceData.Evaluate(Context.GetTime());
	ExecutionTokens.Add(TPropertyTrackExecutionToken<UObject*, FMovieSceneObjectBindingID>(ObjectBinding.Object));
}
