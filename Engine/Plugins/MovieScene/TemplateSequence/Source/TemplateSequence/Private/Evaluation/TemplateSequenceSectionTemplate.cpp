// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Evaluation/TemplateSequenceSectionTemplate.h"
#include "IMovieScenePlaybackClient.h"
#include "TemplateSequence.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "Sections/TemplateSequenceSection.h"

FTemplateSequenceSectionTemplate::FTemplateSequenceSectionTemplate()
{
}

FTemplateSequenceSectionTemplate::FTemplateSequenceSectionTemplate(const UTemplateSequenceSection& Section)
	: SectionStartTime(Section.HasStartFrame() ? Section.GetInclusiveStartFrame() : 0)
{
	if (const UTemplateSequence* InnerSequence = Cast<UTemplateSequence>(Section.GetSequence()))
	{
		if (InnerSequence->GetMovieScene() != nullptr)
		{
			const TArray<FMovieSceneBinding>& InnerSequenceBindings = InnerSequence->GetMovieScene()->GetBindings();
			if (InnerSequenceBindings.Num() > 0)
			{
				const FMovieSceneBinding& Binding = InnerSequenceBindings[0];
				InnerOperand = FMovieSceneEvaluationOperand(Section.GetSequenceID(), Binding.GetObjectGuid());
			}
		}
	}
}

void FTemplateSequenceSectionTemplate::SetupOverrides()
{
	EnableOverrides(RequiresSetupFlag | RequiresTearDownFlag);
}

void FTemplateSequenceSectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
}

void FTemplateSequenceSectionTemplate::Setup(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const
{
	if (InnerOperand.IsValid())
	{
		// Add the override that will tell the child template sequence to use our object binding.
		const FMovieSceneSequenceID SequenceID = PersistentData.GetSectionKey().SequenceID;
		const FMovieSceneObjectBindingID AbsoluteBinding = GetAbsoluteInnerBindingID(SequenceID, Player);
		const FMovieSceneEvaluationOperand AbsoluteInnerOperand(AbsoluteBinding.GetSequenceID(), AbsoluteBinding.GetGuid());

		const FMovieSceneEvaluationOperand Operand(SequenceID, OuterBindingId);
		Player.BindingOverrides.Add(AbsoluteInnerOperand, Operand);
	}
}

void FTemplateSequenceSectionTemplate::TearDown(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const
{
	if (InnerOperand.IsValid())
	{
		// Clear the override.
		const FMovieSceneSequenceID SequenceID = PersistentData.GetSectionKey().SequenceID;
		const FMovieSceneObjectBindingID AbsoluteBinding = GetAbsoluteInnerBindingID(SequenceID, Player);
		const FMovieSceneEvaluationOperand AbsoluteInnerOperand(AbsoluteBinding.GetSequenceID(), AbsoluteBinding.GetGuid());
		Player.BindingOverrides.Remove(AbsoluteInnerOperand);
	}
}

FMovieSceneObjectBindingID FTemplateSequenceSectionTemplate::GetAbsoluteInnerBindingID(FMovieSceneSequenceID LocalSequenceID, IMovieScenePlayer& Player) const
{
	// Convert the binding ID that we have, which is local to the child template sequence, into
	// an absolute ID that fits in the current hierarchy.
	const FMovieSceneObjectBindingID LocalBinding(InnerOperand.ObjectBindingID, InnerOperand.SequenceID, EMovieSceneObjectBindingSpace::Local);

	const FMovieSceneSequenceHierarchy& Hierarchy = Player.GetEvaluationTemplate().GetHierarchy();
	return LocalBinding.ResolveLocalToRoot(LocalSequenceID, Hierarchy);
}
