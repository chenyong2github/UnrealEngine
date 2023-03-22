// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/MovieGraphLinearTimeStep.h"
#include "Graph/MovieGraphPipeline.h"
#include "MoviePipelineQueue.h"
#include "MovieRenderPipelineCoreModule.h"
#include "LevelSequence.h"
#include "MovieScene.h"

void UMovieGraphLinearTimeStep::TickProducingFrames()
{
	int32 CurrentShotIndex = GetOwningGraph()->GetCurrentShotIndex();
	const TArray<TObjectPtr<UMoviePipelineExecutorShot>>& ActiveShotList = GetOwningGraph()->GetActiveShotList();
	UMoviePipelineExecutorShot* CurrentCameraCut = ActiveShotList[CurrentShotIndex];

	if (CurrentCameraCut->ShotInfo.State == EMovieRenderShotState::Uninitialized)
	{
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("MovieGraph Initializing Camera Cut [%d/%d] in [%s] %s."),
			CurrentShotIndex + 1, ActiveShotList.Num(), *CurrentCameraCut->OuterName, *CurrentCameraCut->InnerName);

		GetOwningGraph()->SetupShot(CurrentCameraCut);
		// InitializeShot(CurrentCameraCut);
		GetOwningGraph()->GetDataCachingInstance()->InitializeShot(CurrentCameraCut);

		// We can safely fall through to the below states as they're OK to process the same frame we set up.
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("MovieGraph Finished initializing Camera Cut [%d/%d] in [%s] %s."),
			CurrentShotIndex + 1, ActiveShotList.Num(), *CurrentCameraCut->OuterName, *CurrentCameraCut->InnerName);

		// Temp...
		CurrentCameraCut->ShotInfo.State = EMovieRenderShotState::Rendering;
	}

	

	if (CurrentCameraCut->ShotInfo.State == EMovieRenderShotState::Rendering)
	{
		ULevelSequence* CurrentSequence = Cast<ULevelSequence>(GetOwningGraph()->GetCurrentJob()->Sequence.TryLoad());
		FFrameRate TickResolution = CurrentSequence->GetMovieScene()->GetTickResolution();
		FFrameRate FrameRate = CurrentSequence->GetMovieScene()->GetDisplayRate();

		FFrameTime TicksPerOutputFrame = FFrameRate::TransformTime(FFrameTime(FFrameNumber(1)), FrameRate, TickResolution);
		CurrentCameraCut->ShotInfo.CurrentTickInRoot += TicksPerOutputFrame.GetFrame();

		// Convert the CurrentTickInRoot back to a frame number.
		FFrameTime CurrentTickInRootInOutputFrames = FFrameRate::TransformTime(CurrentCameraCut->ShotInfo.CurrentTickInRoot, TickResolution, FrameRate);
		CurrentTimeStepData.OutputFrameNumber = CurrentTickInRootInOutputFrames.FloorToFrame().Value;
		CurrentTimeStepData.FrameDeltaTime = FrameRate.AsInterval();
		CurrentTimeStepData.WorldSeconds = 0.f;
		CurrentTimeStepData.MotionBlurFraction = FrameRate.AsInterval();
		
		// ToDo: This will change once we have temporal sub-sample support
		CurrentTimeStepData.bIsFirstTemporalSampleForFrame = true;
		CurrentTimeStepData.bIsLastTemporalSampleForFrame = true;


		if (CurrentCameraCut->ShotInfo.CurrentTickInRoot >= CurrentCameraCut->ShotInfo.TotalOutputRangeRoot.GetUpperBoundValue())
		{
			CurrentCameraCut->ShotInfo.State = EMovieRenderShotState::Finished;
			GetOwningGraph()->TeardownShot(CurrentCameraCut);
			return;
		}
	}
}
