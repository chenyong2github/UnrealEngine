// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/TemplateSequenceSection.h"
#include "TemplateSequence.h"
#include "Evaluation/TemplateSequenceInstanceData.h"
#include "Evaluation/TemplateSequenceSectionTemplate.h"
#include "UObject/SequencerObjectVersion.h"


UTemplateSequenceSection::UTemplateSequenceSection()
{
}

void UTemplateSequenceSection::OnDilated(float DilationFactor, FFrameNumber Origin)
{
	// TODO-lchabant: shouldn't this be in the base class?
	Parameters.TimeScale /= DilationFactor;
}

FMovieSceneSubSequenceData UTemplateSequenceSection::GenerateSubSequenceData(const FSubSequenceInstanceDataParams& Params) const
{
	FMovieSceneSubSequenceData SubData(*this);

	FTemplateSequenceInstanceData InstanceData;
	InstanceData.Operand = Params.Operand;
	SubData.InstanceData = FMovieSceneSequenceInstanceDataPtr(InstanceData);

	return SubData;
}

FMovieSceneEvalTemplatePtr UTemplateSequenceSection::GenerateTemplate() const
{
	if (SubSequence != nullptr)
	{
		return FTemplateSequenceSectionTemplate(*this);
	}
	return FMovieSceneEvalTemplatePtr();
}

