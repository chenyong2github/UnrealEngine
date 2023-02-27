// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Graph/MovieGraphDataTypes.h"
#include "Misc/FrameTime.h"
#include "MovieGraphDefaultDataCaching.generated.h"

// Forward Declares
class ULevelSequence;
class ALevelSequenceActor;
class UMovieSceneSequencePlayer;

/**
*
*/
UCLASS(BlueprintType)
class MOVIERENDERPIPELINECORE_API UMovieGraphDefaultDataCaching : public UMovieGraphDataCachingBase
{
	GENERATED_BODY()
public:
	virtual void CacheDataPreJob(const FMovieGraphInitConfig& InInitConfig) override;
	virtual void RestoreCachedDataPostJob() override;
	virtual void UpdateShotList() override;

protected:
	void CacheLevelSequenceData(ULevelSequence* InSequence);
	void OnSequenceEvaluated(const UMovieSceneSequencePlayer& Player, FFrameTime CurrentTime, FFrameTime PreviousTime);

protected:
	UPROPERTY(Transient)
	TObjectPtr<ALevelSequenceActor> LevelSequenceActor;
};