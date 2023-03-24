// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Graph/MovieGraphDataTypes.h"
#include "Misc/FrameTime.h"
#include "MovieGraphSequenceDataSource.generated.h"

// Forward Declares
class ULevelSequence;
class ALevelSequenceActor;
class UMovieSceneSequencePlayer;

/**
* The UMovieGraphSequenceDataSource allows using a ULevelSequence as the external datasource for the Movie Graph.
* It will build the range of shots based on the contents of the level sequence (one shot per camera cut found inside
* the sequence hierarchy, not allowing overlapping Camera Cut sections), and then it will evaluate the level sequence
* for the given time when rendering.
*/
UCLASS(BlueprintType)
class MOVIERENDERPIPELINECORE_API UMovieGraphSequenceDataSource : public UMovieGraphDataSourceBase
{
	GENERATED_BODY()

public:
	virtual void CacheDataPreJob(const FMovieGraphInitConfig& InInitConfig) override;
	virtual void RestoreCachedDataPostJob() override;
	virtual void UpdateShotList() override;
	virtual FFrameRate GetTickResolution() const override;
	virtual FFrameRate GetDisplayRate() const override;

protected:
	void CacheLevelSequenceData(ULevelSequence* InSequence);
	void OnSequenceEvaluated(const UMovieSceneSequencePlayer& Player, FFrameTime CurrentTime, FFrameTime PreviousTime);

protected:
	UPROPERTY(Transient)
	TObjectPtr<ALevelSequenceActor> LevelSequenceActor;

};