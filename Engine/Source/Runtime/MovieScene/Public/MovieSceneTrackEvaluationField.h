// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/CoreDefines.h"
#include "Containers/Array.h"
#include "Misc/FrameNumber.h"
#include "Math/Range.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "Evaluation/MovieSceneSegment.h"
#include "Evaluation/MovieSceneEvaluationTree.h"

#include "MovieSceneTrackEvaluationField.generated.h"

class UMovieSceneTrack;
class UMovieSceneSection;

USTRUCT()
struct FMovieSceneTrackEvaluationFieldEntry
{
	GENERATED_BODY()

	UPROPERTY()
	UMovieSceneSection* Section = nullptr;

	UPROPERTY()
	FFrameNumberRange Range;

	UPROPERTY()
	FFrameNumber ForcedTime;

	UPROPERTY()
	ESectionEvaluationFlags Flags = ESectionEvaluationFlags::None;

	UPROPERTY()
	int16 LegacySortOrder = 0;
};

USTRUCT()
struct FMovieSceneTrackEvaluationField
{
	GENERATED_BODY()

	void Reset(int32 NumExpected = 0)
	{
		Entries.Reset(NumExpected);
	}

	UPROPERTY()
	TArray<FMovieSceneTrackEvaluationFieldEntry>  Entries;
};

struct FMovieSceneTrackEvaluationData
{
	FMovieSceneTrackEvaluationData()
		: ForcedTime(TNumericLimits<int32>::Lowest())
		, SortOrder(0)
		, Flags(ESectionEvaluationFlags::None)
	{}

	MOVIESCENE_API static FMovieSceneTrackEvaluationData FromSection(UMovieSceneSection* InSection);

	MOVIESCENE_API static FMovieSceneTrackEvaluationData FromTrack(UMovieSceneTrack* InTrack);

	FMovieSceneTrackEvaluationData& SetFlags(ESectionEvaluationFlags InFlags)
	{
		Flags = InFlags;
		return *this;
	}

	FMovieSceneTrackEvaluationData& Sort(int16 InSortOrder)
	{
		SortOrder = InSortOrder;
		return *this;
	}

	TWeakObjectPtr<UMovieSceneTrack> Track;

	TWeakObjectPtr<UMovieSceneSection> Section;

	FFrameNumber ForcedTime;

	int16 SortOrder;

	ESectionEvaluationFlags Flags;
};
