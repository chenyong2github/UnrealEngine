// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneTestObjects.h"
#include "Compilation/MovieSceneCompiler.h"
#include "Compilation/MovieSceneCompilerRules.h"
#include "Compilation/MovieSceneSegmentCompiler.h"

FMovieSceneEvalTemplatePtr UTestMovieSceneTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	return FTestMovieSceneEvalTemplate();
}

FMovieSceneTrackSegmentBlenderPtr UTestMovieSceneTrack::GetTrackSegmentBlender() const
{
	struct FSegmentBlender : FMovieSceneTrackSegmentBlender
	{
		bool bHighPass;
		bool bEvaluateNearest;

		FSegmentBlender(bool bInHighPassFilter, bool bInEvaluateNearest)
			: bHighPass(bInHighPassFilter)
			, bEvaluateNearest(bInEvaluateNearest)
		{
			bCanFillEmptySpace = bEvaluateNearest;
		}

		virtual void Blend(FSegmentBlendData& BlendData) const
		{
			if (bHighPass)
			{
				MovieSceneSegmentCompiler::ChooseLowestRowIndex(BlendData);
			}

			// Always sort by priority
			Algo::SortBy(BlendData, [](const FMovieSceneSectionData& In) { return In.Section->GetRowIndex(); });
		}

		virtual TOptional<FMovieSceneSegment> InsertEmptySpace(const TRange<FFrameNumber>& Range, const FMovieSceneSegment* PreviousSegment, const FMovieSceneSegment* NextSegment) const
		{
			return bEvaluateNearest ? MovieSceneSegmentCompiler::EvaluateNearestSegment(Range, PreviousSegment, NextSegment) : TOptional<FMovieSceneSegment>();
		}
	};

	// Evaluate according to bEvalNearestSection preference
	return FSegmentBlender(bHighPassFilter, EvalOptions.bCanEvaluateNearestSection && EvalOptions.bEvalNearestSection);
}
