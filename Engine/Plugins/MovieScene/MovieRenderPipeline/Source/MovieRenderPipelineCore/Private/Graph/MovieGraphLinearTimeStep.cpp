// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/MovieGraphLinearTimeStep.h"
#include "Graph/MovieGraphPipeline.h"
#include "MoviePipelineQueue.h"
#include "MovieRenderPipelineCoreModule.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraActor.h"
#include "Interfaces/Interface_PostProcessVolume.h"

void UMovieGraphLinearTimeStep::TickProducingFrames()
{
	int32 CurrentShotIndex = GetOwningGraph()->GetCurrentShotIndex();
	const TArray<TObjectPtr<UMoviePipelineExecutorShot>>& ActiveShotList = GetOwningGraph()->GetActiveShotList();
	UMoviePipelineExecutorShot* CurrentCameraCut = ActiveShotList[CurrentShotIndex];

	// When start up we want to override the engine's Custom Timestep with our own.
	// This gives us the ability to completely control the engine tick/delta time before the frame
	// is started so that we don't have to always be thinking of delta times one frame ahead. We need
	// to do this only once we're ready to set the timestep though, as Initialize can be called as
	// a result of a OnBeginFrame, meaning that Initialize is called on the frame before TickProducingFrames
	// so there would be one frame where it used the custom timestep (after initialize) before TPF was called.
	//if (GEngine->GetCustomTimeStep() != CustomTimeStep)
	//{
	//	CachedPrevCustomTimeStep = GEngine->GetCustomTimeStep();
	//	GEngine->SetCustomTimeStep(CustomTimeStep);
	//}

	// We cache the frame metrics for the duration of a single output frame, instead of recalculating them every tick.
	// This is required for stochastic timesteps (which will jump around the actual evaluated time randomly within one
	// output frame), but it helps resolve some of our issues related to time dilation. Historically when doing it every
	// tick, we didn't know if we were going to overrun the end range of time represented until we had actually done it,
	// partway through the frame, at which point we had to backtrack and abandon the work. So now we calculate it all
	// in advance, check if our new time would overrun the end of the current camera cut, and if so, skip actually starting
	// that frame. Unfortunately this does cause some complications with actor time dilations - the time dilation track
	// will be updating each tick within the output sample and trying to change the world time dilation which we don't want.
	// ToDo: Still tbd how we'll fix that, currently the plan is to allow custom clocksources to disable world time dilation
	// support, at which point we'll handle the time dilation by affecting the whole engine tick instead of just world tick.
	//if (IsFirstTemporalSample())
	{
		UpdateFrameMetrics();

		// Now that we've calculated the total range of time we're trying to represent, we can check to see
		// if this would put us beyond our range of time this shot is supposed to represent.

	}


	if (CurrentCameraCut->ShotInfo.State == EMovieRenderShotState::Uninitialized)
	{
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("MovieGraph Initializing Camera Cut [%d/%d] in [%s] %s."),
			CurrentShotIndex + 1, ActiveShotList.Num(), *CurrentCameraCut->OuterName, *CurrentCameraCut->InnerName);

		GetOwningGraph()->SetupShot(CurrentCameraCut);
		// InitializeShot(CurrentCameraCut);
		GetOwningGraph()->GetDataSourceInstance()->InitializeShot(CurrentCameraCut);

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

void UMovieGraphLinearTimeStep::UpdateFrameMetrics()
{
	FFrameData FrameData;

	// ToDo: This needs to come from the graph
	int32 TemporalSampleCount = 1;

	// We inherit a tick resolution from the level sequence so that we can represent the same
	// range of time that the level sequence does.
	FrameData.TickResolution = GetOwningGraph()->GetDataSourceInstance()->GetTickResolution();
	FrameData.FrameRate = GetOwningGraph()->GetDataSourceInstance()->GetDisplayRate(); // ToDo, needs to come from config (config can override)
	FrameData.FrameTimePerOutputFrame = FFrameRate::TransformTime(FFrameTime(FFrameNumber(1)), FrameData.FrameRate, FrameData.TickResolution);

	// Manually perform blending of the Post Process Volumes/Camera/Camera Modifiers to match what the renderer will do.
	// This uses the primary camera specified by the PlayerCameraManager to get the motion blur amount so in the event of
	// multi-camera rendering, all cameras will end up using the same motion blur amount defined by the primary camera).
	FrameData.MotionBlurAmount = GetBlendedMotionBlurAmount();

	// Calculate how long of a duration we want to represent where the camera shutter is open.
	FrameData.FrameTimeWhileShutterOpen = FrameData.FrameTimePerOutputFrame * FrameData.MotionBlurAmount;

	// Now that we know how long the shutter is open, figure out how long each temporal sub-sample gets.
	FrameData.FrameTimePerTemporalSample = FrameData.FrameTimeWhileShutterOpen / TemporalSampleCount;

	// The amount of time closed + time open should add up to exactly how long a frame is.
	FrameData.FrameTimeWhileShutterClosed = FrameData.FrameTimePerOutputFrame - FrameData.FrameTimeWhileShutterOpen;

	// Shutter timing is a bias applied to the final evaluation time to let us change what we consider a frame
	// ie: Do we consider a frame the start of the timespan we captured? Or is the frame the end of the timespan?
	// We default to Centered so that the center of your evaluated time is what you see in Level Sequences.
	// ToDo: Shutter Timing Offset
	//switch (CameraSettings->ShutterTiming)
	//{
	//	// Subtract the entire time the shutter is open.
	//case EMoviePipelineShutterTiming::FrameClose:
	//	Output.ShutterOffsetTicks = -Output.TicksWhileShutterOpen;
	//	break;
	//	// Only subtract half the time the shutter is open.
	//case EMoviePipelineShutterTiming::FrameCenter:
	FrameData.ShutterOffsetFrameTime = -FrameData.FrameTimeWhileShutterOpen / 2.0;
	//	break;
	//	// No offset needed
	//case EMoviePipelineShutterTiming::FrameOpen:
	//	break;
	//}

	// Then, calculate our motion blur offset. Motion Blur in the engine is always
	// centered around the object so we offset our time sampling by half of the
	// motion blur distance so that the distance blurred represents that time.
	FrameData.MotionBlurCenteringOffsetTime = FrameData.FrameTimePerTemporalSample / 2.0;
}

float UMovieGraphLinearTimeStep::GetBlendedMotionBlurAmount()
{
	// 0.5f is the default engine motion blur in the event no Post Process/Camera overrides it.
	float FinalMotionBlurAmount = 0.5f;

	APlayerCameraManager* PlayerCameraManager = GetWorld()->GetFirstPlayerController()->PlayerCameraManager;
	if (PlayerCameraManager)
	{
		// Apply any motion blur settings from post process volumes in the world
		FVector ViewLocation = PlayerCameraManager->GetCameraLocation();
		for (IInterface_PostProcessVolume* PPVolume : GetWorld()->PostProcessVolumes)
		{
			const FPostProcessVolumeProperties VolumeProperties = PPVolume->GetProperties();

			// Skip any volumes which are either disabled or don't modify blur amount
			if (!VolumeProperties.bIsEnabled || !VolumeProperties.Settings->bOverride_MotionBlurAmount)
			{
				continue;
			}

			float LocalWeight = FMath::Clamp(VolumeProperties.BlendWeight, 0.0f, 1.0f);

			if (!VolumeProperties.bIsUnbound)
			{
				float DistanceToPoint = 0.0f;
				PPVolume->EncompassesPoint(ViewLocation, 0.0f, &DistanceToPoint);

				if (DistanceToPoint >= 0 && DistanceToPoint < VolumeProperties.BlendRadius)
				{
					LocalWeight *= FMath::Clamp(1.0f - DistanceToPoint / VolumeProperties.BlendRadius, 0.0f, 1.0f);
				}
				else
				{
					LocalWeight = 0.0f;
				}
			}

			if (LocalWeight > 0.0f)
			{
				FinalMotionBlurAmount = FMath::Lerp(FinalMotionBlurAmount, VolumeProperties.Settings->MotionBlurAmount, LocalWeight);
			}
		}

		// Now try from the camera, which takes priority over post processing volumes.
		ACameraActor* CameraActor = Cast<ACameraActor>(PlayerCameraManager->GetViewTarget());
		if (CameraActor)
		{
			UCameraComponent* CameraComponent = CameraActor->GetCameraComponent();
			if (CameraComponent && CameraComponent->PostProcessSettings.bOverride_MotionBlurAmount)
			{
				FinalMotionBlurAmount = FMath::Lerp(FinalMotionBlurAmount, CameraComponent->PostProcessSettings.MotionBlurAmount, CameraComponent->PostProcessBlendWeight);
			}
		}

		// Apply any motion blur settings from post processing blends attached to the camera manager
		TArray<FPostProcessSettings> const* CameraAnimPPSettings;
		TArray<float> const* CameraAnimPPBlendWeights;
		PlayerCameraManager->GetCachedPostProcessBlends(CameraAnimPPSettings, CameraAnimPPBlendWeights);
		for (int32 PPIdx = 0; PPIdx < CameraAnimPPBlendWeights->Num(); ++PPIdx)
		{
			if ((*CameraAnimPPSettings)[PPIdx].bOverride_MotionBlurAmount)
			{
				FinalMotionBlurAmount = FMath::Lerp(FinalMotionBlurAmount, (*CameraAnimPPSettings)[PPIdx].MotionBlurAmount, (*CameraAnimPPBlendWeights)[PPIdx]);
			}
		}
	}

	return FinalMotionBlurAmount;
}