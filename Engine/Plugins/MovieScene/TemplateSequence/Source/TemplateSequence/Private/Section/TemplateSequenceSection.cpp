// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/TemplateSequenceSection.h"
#include "TemplateSequence.h"
#include "Evaluation/TemplateSequenceInstanceData.h"
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

