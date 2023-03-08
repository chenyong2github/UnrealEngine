// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "MoviePipelineBase.h"
#include "MovieGraphDataTypes.h"
#include "MovieGraphConfig.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MovieGraphPipeline.generated.h"

// Forward Declares
class UMoviePipelineExecutorJob;
class UMoviePipelineExecutorShot;

UCLASS(BlueprintType)
class MOVIERENDERPIPELINECORE_API UMovieGraphPipeline : public UMoviePipelineBase
{
	GENERATED_BODY()

public:
	UMovieGraphPipeline();

	UFUNCTION(BlueprintCallable, Category = "Movie Graph")
	void Initialize(UMoviePipelineExecutorJob* InJob, const FMovieGraphInitConfig& InitConfig);

	UFUNCTION(BlueprintCallable, Category = "Movie Graph")
	UMoviePipelineExecutorJob* GetCurrentJob() const { return CurrentJob; }


public:
	UMovieGraphConfig* GetRootGraphForShot(UMoviePipelineExecutorShot* InShot) const;
	FMovieGraphTraversalContext GetTraversalContextForShot(UMoviePipelineExecutorShot* InShot) const;
protected:
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	virtual void OnMoviePipelineFinishedImpl();

protected:
	virtual void OnEngineTickBeginFrame();
	virtual void OnEngineTickEndFrame();
	virtual void RenderFrame();
	virtual void BuildShotListFromDataSource();
	virtual void SetupShot(UMoviePipelineExecutorShot* InShot);
	virtual void TeardownShot(UMoviePipelineExecutorShot* InShot);

	virtual void TickProducingFrames();
	virtual void TickPostFinalizeExport(const bool bInForceFinish);
	virtual void TickFinalizeOutputContainers(const bool bInForceFinish);
	virtual void TransitionToState(const EMovieRenderPipelineState InNewState);

	// UMoviePipelineBase Interface
	virtual void RequestShutdownImpl(bool bIsError) override;
	virtual void ShutdownImpl(bool bIsError) override;
	virtual bool IsShutdownRequestedImpl() const override { return bShutdownRequested; }
	virtual EMovieRenderPipelineState GetPipelineStateImpl() const override { return PipelineState; }
	// ~UMoviePipelineBase Interface

protected:
	UPROPERTY(Transient)
	TObjectPtr<UMovieGraphTimeStepBase> GraphTimeStepInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMovieGraphRendererBase> GraphRendererInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMovieGraphTimeRangeBuilderBase> GraphTimeRangeBuilderInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMovieGraphDataCachingBase> GraphDataCachingInstance;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UMoviePipelineExecutorJob> CurrentJob;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMoviePipelineExecutorShot>> ActiveShotList;

	int32 CurrentShotIndex;

	/** True if we're in a TransitionToState call. Used to prevent reentrancy. */
	bool bIsTransitioningState;

	/** True if RequestShutdown() was called. At the start of the next frame we will stop producing frames and start shutting down. */
	FThreadSafeBool bShutdownRequested;

	/** Set to true during Shutdown/RequestShutdown if we are shutting down due to an error. */
	FThreadSafeBool bShutdownSetErrorFlag;

	/** Which step of the rendering process is the graph currently in. */
	EMovieRenderPipelineState PipelineState;
};