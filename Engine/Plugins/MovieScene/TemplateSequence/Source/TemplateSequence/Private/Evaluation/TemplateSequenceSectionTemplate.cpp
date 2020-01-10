// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluation/TemplateSequenceSectionTemplate.h"
#include "IMovieScenePlaybackClient.h"
#include "TemplateSequence.h"
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
	EnableOverrides(RequiresInitializeFlag | RequiresTearDownFlag);
}

void FTemplateSequenceSectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
}

void FTemplateSequenceSectionTemplate::Initialize(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const
{
	if (InnerOperand.IsValid())
	{
		Player.BindingOverrides.Add(InnerOperand, Operand);
	}
}

void FTemplateSequenceSectionTemplate::TearDown(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const
{
	if (InnerOperand.IsValid())
	{
		Player.BindingOverrides.Remove(InnerOperand);
	}
}
