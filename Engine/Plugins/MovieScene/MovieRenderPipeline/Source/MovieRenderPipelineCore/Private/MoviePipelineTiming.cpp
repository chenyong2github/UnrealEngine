// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipeline.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MoviePipelineAccumulationSetting.h"
#include "MoviePipelineShotConfig.h"
#include "MoviePipelineOutputBase.h"
#include "CoreGlobals.h"
#include "Containers/Array.h"
#include "MovieRenderPipelineCoreModule.h"
#include "MovieScene.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "MoviePipelineMasterConfig.h"
#include "MoviePipelineCameraSetting.h"
#include "MoviePipelineQueue.h"

void UMoviePipeline::TickProducingFrames()
{
	// The callback for this function does not get registered until Initialization has been called, which sets
	// the state to Render. If it's not, we have a initialization order/flow issue!
	check(PipelineState == EMovieRenderPipelineState::ProducingFrames);

	// We should not be calling this once we have completed all the shots.
	check(CurrentShotIndex >= 0 && CurrentShotIndex < ShotList.Num());

	// If we're debug stepping one frame at a time, this will return true
	// and we early out so that we don't end up advancing the pipeline at all.
	// This handles setting the delta time for us to a fixed number.
	if (DebugFrameStepPreTick())
	{
		return;
	}
	
	// Make sure we're unpaused for the duration of this frame. We could have been paused by the frame stepper
	// on the last Tick.
	GetWorld()->GetFirstPlayerController()->SetPause(false);

	FMoviePipelineShotInfo& CurrentShot = ShotList[CurrentShotIndex];
	FMoviePipelineCameraCutInfo& CurrentCameraCut = CurrentShot.GetCurrentCameraCut();

	// Check to see if we need to initialize a new shot. This potentially changes a lot of state.
	if (CurrentCameraCut.State == EMovieRenderShotState::Uninitialized)
	{
		// If this is the first camera cut, and it was uninitialized, then this is the first time the shot is being set up.
		if (CurrentShot.CurrentCameraCutIndex == 0)
		{
			// Solo our shot and create rendering resources.
			InitializeShot(CurrentShot);
		}

		UE_LOG(LogMovieRenderPipeline, Log, TEXT("[%d] Initializing Camera Cut [%d/%d] in Shot (%s)."), GFrameCounter, 
			CurrentShot.CurrentCameraCutIndex + 1, CurrentShot.CameraCuts.Num(), *CurrentShot.GetDisplayName());
		
		CurrentCameraCut.SetNextState(EMovieRenderShotState::Uninitialized);

		// The shot has been initialized and the new state chosen. Jump the Sequence to the starting time.
		// This means that the camera will be in the correct location for warm-up frames to allow any systems
		// dependent on camera position to properly warm up. If motion blur fixes are enabled, then we'll jump 
		// again after the warm-up frames.
		FFrameTime TimeInPlayRate = FFrameRate::TransformTime(CurrentCameraCut.CurrentTick, TargetSequence->GetMovieScene()->GetTickResolution(), TargetSequence->GetMovieScene()->GetDisplayRate());
		
		// We tell it to jump so that things responding to different scrub-types work correctly.
		LevelSequenceActor->GetSequencePlayer()->JumpToFrame(TimeInPlayRate);
		CustomSequenceTimeController->SetCachedFrameTiming(FQualifiedFrameTime(CurrentCameraCut.CurrentTick, TargetSequence->GetMovieScene()->GetTickResolution()));

		// Ensure we don't try to evaluate as we want to sit and wait during warm up and motion blur frames.
		LevelSequenceActor->GetSequencePlayer()->Pause();

		// We can safely fall through to the below states as they're OK to process the same frame we set up.
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("[%d] Finished initializing Camera Cut [%d] in Shot [%d] %s."), GFrameCounter, 
			CurrentShot.CurrentCameraCutIndex + 1, CurrentShotIndex, *CurrentShot.GetDisplayName());
	}
	
	// Do the WarmingUp State exit condition before the WarmingUp loop, to allow the correct WarmingUp state to
	// persist through to the post-frame render before switching to the next state.
	if (CurrentCameraCut.State == EMovieRenderShotState::WarmingUp && CurrentCameraCut.NumWarmUpFramesRemaining == 0)
	{
		// We can either jump to the MotionBlur state or to the Rendering state, same as with coming from Uninitialized stage. 
		// We just let the shot decide to move on if it's appropriate (to avoid duplicating the code from Uninitialized)
		CurrentCameraCut.SetNextState(EMovieRenderShotState::WarmingUp);
	}

	// If the shot is warming up then we will just pass a reasonable amount of time this frame (no output data will be produced).
	if (CurrentCameraCut.State == EMovieRenderShotState::WarmingUp)
	{
		UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("[%d] Evaluating WarmUp frame %d. Remaining: %d"), GFrameCounter, CurrentCameraCut.NumWarmUpFramesRemaining, CurrentCameraCut.NumWarmUpFramesRemaining - 1);

		// This if block will get called the same frame that we transition into the Warm Up state. We should not be in the
		// warm up state unless we have at least one frame. 
		CurrentCameraCut.NumWarmUpFramesRemaining--;

		// Render the next frame at a reasonable frame rate to pass time in the world.
		double FrameDeltaTime = GetPipelineMasterConfig()->GetEffectiveFrameRate(TargetSequence).AsInterval();

		UE_LOG(LogMovieRenderPipeline, VeryVerbose, TEXT("[%d] Shot WarmUp set engine DeltaTime to %f seconds."), GFrameCounter, FrameDeltaTime);
		CustomTimeStep->SetCachedFrameTiming(MoviePipeline::FFrameTimeStepCache(FrameDeltaTime));
		
		CachedOutputState.FrameDeltaTime = FrameDeltaTime;
		CachedOutputState.WorldSeconds = CachedOutputState.WorldSeconds + FrameDeltaTime;
		// We don't want to execute the other possible states until at least the next frame.
		// This ensures that we actually tick the world once for the WarmUp state.
		return;
	}

	// At this point, from now on, the Sequence should be in the Playing state. MotionBlur fixes only
	// last for one frame, and rendering expects it to be playing anyways. 
	if (!LevelSequenceActor->GetSequencePlayer()->IsPlaying())
	{
		UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("[%d] Setting Sequence Player to Play due to being paused when motion started."), GFrameCounter);
		LevelSequenceActor->GetSequencePlayer()->Play();
	}

	// Do the MotionBlur State exit condition before the MotionBlur loop, to allow the correct MotionBlur state to
	// persist through to the post-frame render before switching to the next state.
	if (CurrentCameraCut.State == EMovieRenderShotState::MotionBlur && CurrentCameraCut.bHasEvaluatedMotionBlurFrame)
	{
		// Motion Blur will evaluate further on in the frame than the first output frame. This is so that when
		// we jump to the correct evaluation time for the first frame, the motion vectors (which are bi-directional)
		// are correct and more likely to have valid data than if we started before the frame.
		const MoviePipeline::FFrameConstantMetrics FrameMetrics = CalculateShotFrameMetrics(CurrentShot);

		// The first frame of rendering code always assumes that we're coming from the previous frame. To avoid
		// complicating that code, we'll jump back to where that frame would have been rendered, had it existed.
		FFrameTime TicksToEndOfPreviousFrame;
		float WorldTimeDilation = GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation();

		UMoviePipelineCameraSetting* CameraSettings = FindOrAddSetting<UMoviePipelineCameraSetting>(CurrentShot);
		if (CameraSettings->TemporalSampleCount == 1)
		{
			TicksToEndOfPreviousFrame = FrameMetrics.TicksPerOutputFrame;
		}
		else
		{
			// The first sub-frame accumulation is going to try to go forward
			// by the amount the shutter is closed, plus the duration of one sample
			// because that is the logic we use for every other frame.
			TicksToEndOfPreviousFrame = FrameMetrics.TicksPerSample + FrameMetrics.TicksWhileShutterClosed;
		}

		if (!FMath::IsNearlyEqual(WorldTimeDilation, 1.f))
		{
			TicksToEndOfPreviousFrame = TicksToEndOfPreviousFrame * WorldTimeDilation;
		}
		AccumulatedTickSubFrameDeltas -= TicksToEndOfPreviousFrame.GetSubFrame();
		CurrentCameraCut.CurrentTick = CurrentCameraCut.CurrentTick - TicksToEndOfPreviousFrame.FloorToFrame();

		// Skip to rendering which will skip the next block.
		CurrentCameraCut.State = EMovieRenderShotState::Rendering;
	}

	// This block is optional, as they may not want motion blur fixes.
	if (CurrentCameraCut.State == EMovieRenderShotState::MotionBlur)
	{
		const MoviePipeline::FFrameConstantMetrics FrameMetrics = CalculateShotFrameMetrics(CurrentShot);
		CachedOutputState.MotionBlurFraction = FrameMetrics.ShutterAnglePercentage;
		
		// For the Motion Blur frame, we evaluate the Sequence past the starting frame so that on the next frame
		// we go backwards, and that gives us an approximation of motion blur for data we don't have. The world moves
		// forward in both cases which is a little odd conceptually but we can't use negative delta times.
		double FrameDeltaTime = FrameMetrics.TickResolution.AsSeconds(FrameMetrics.TicksPerSample);
		CachedOutputState.FrameDeltaTime = FrameDeltaTime;
		CachedOutputState.WorldSeconds = CachedOutputState.WorldSeconds + FrameDeltaTime;

		// We subtract a single tick here so that if you're trying to render one frame, and it is the last frame of the
		// camera cut, if we jump forward a whole frame the evaluation will fail (since end frames are exclusive). To
		// work around, we evaluate one tick before, which is indistinguishable in positions but neatly avoids the eval problem.
		FFrameNumber MotionBlurDurationTicks = FrameMetrics.TicksPerOutputFrame.FloorToFrame() - FFrameNumber(1);

		// CurrentShot.CurrentTick should be at the first frame they want to evaluate, without the shutter timing/motion blur
		// offsets taken into account. When we evaluate for this frame, take those into account.
		FFrameTime FinalEvalTime = FrameMetrics.GetFinalEvalTime(CurrentCameraCut.CurrentTick + MotionBlurDurationTicks);

		CustomTimeStep->SetCachedFrameTiming(MoviePipeline::FFrameTimeStepCache(FrameDeltaTime));
		CustomSequenceTimeController->SetCachedFrameTiming(FQualifiedFrameTime(FinalEvalTime, FrameMetrics.TickResolution));
		
		// We early out on this loop so that at least one tick passes for motion blur. We need to leave our state in
		// MotionBlur because the render tick needs to know that it is processing motion blur (it's a special case
		// with many renders for a frame to fill history buffers).
		CurrentCameraCut.bHasEvaluatedMotionBlurFrame = true;
		UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("[%d] Shot MotionBlur set engine DeltaTime to %f seconds."), GFrameCounter, FrameDeltaTime);
		return;
	}

	// We notify our output containers that we're starting to produce content again. This is useful for things like
	// sound where we may want to skip a period of sound (during warm-ups, etc.) We don't do this as part of another
	// block because you could be getting to this point from any of three states (Initializing, Warm Up, MotionBlur)
	// so it's easier to just track it separately and do it once here.
	if (!CurrentCameraCut.bHasNotifiedFrameProductionStarted)
	{
		CurrentCameraCut.bHasNotifiedFrameProductionStarted = true;

	}

	// Alright we're in the point where we finally want to start rendering out frames, we've finished warming up,
	// we've evaluated the correct frame to give us motion blur vector sources, etc. This will be called each time
	// we advance the world, even when sub-stepping, etc.
	if (CurrentCameraCut.State == EMovieRenderShotState::Rendering)
	{
		const MoviePipeline::FFrameConstantMetrics FrameMetrics = CalculateShotFrameMetrics(CurrentShot);
		float WorldTimeDilation = GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation();

		// This delta time may get modified to respect slowmo tracks and will be converted to seconds for
		// engine delta time at the end.
		FFrameTime DeltaFrameTime = FFrameTime();

		UMoviePipelineAccumulationSetting* AccumulationSettings = FindOrAddSetting<UMoviePipelineAccumulationSetting>(CurrentShot);
		check(AccumulationSettings);

		UMoviePipelineCameraSetting* CameraSettings = FindOrAddSetting<UMoviePipelineCameraSetting>(CurrentShot);

		// If we've rendered the last sample and wrapped around, then we're going to be
		// working on the next output frame for this frame. Since OutputFrameNumber gets
		// initialized to -1, this is ok on the very first frame of rendering as it pushes
		// us to be working on outputting frame zero.
		if (CachedOutputState.TemporalSampleIndex >= CameraSettings->TemporalSampleCount - 1)
		{
			CachedOutputState.TemporalSampleIndex = -1;
		}

		CachedOutputState.TemporalSampleIndex++;

		if (CameraSettings->TemporalSampleCount == 1)
		{
			// It should always be zero when using no sub-sampling.
			check(CachedOutputState.TemporalSampleIndex == 0);

			// When not using any sub-frame accumulation, the delta time is just
			// 1/FrameRate * World Timescale (Slo-mo), which is applied afterwards.
			DeltaFrameTime = FrameMetrics.TicksPerOutputFrame;
		}
		else
		{
			/*
			* Sub-frame accumulation is surprisingly nuanced and complicated. Unreal's real-time motion blur is
			* is an approximation that derives a velocity (per-vertex) by taking the difference in positions per 
			* frame and then simulates a 'centered' motion blur, blurring pixels on either side of the object.
			* The amount of pixels that are blurred is determined by the Shutter Angle ("Motion Blur Amount").
			* Because we are simulating a higher quality version of the existing motion blur, we override the
			* Shutter Angle to 360 during accumulation frames, as they are designed to represent one long blur
			* and we emulate the shutter angle ourself over multiple frames.
			*
			* Different industries have different needs for when data is collected relative to an output frame.
			* Since a "frame" is a span of time, and a non-360 shutter angle determines that no photons would
			* accumulate while it is closed, there are three options for what period of time makes up the frame.
			* In addition to the sampling being shifted, there may also be a brightness curve that gets applied
			* to each shutter type during the accumulation frame, as choosing to give a particular frame more
			* weight during the accumulation will make it brighter and thus appear to have more influence. This
			* is handled during the accumulation/pixel data instead of shifting samples around as that would make
			* the timing on this very complicated.
			*
			* In the diagram below, 'f' marks an output frame, 's' marks the shutter-angle (180 in this case)
			* and it is assumed there are four motion samples ('-')
			*
			* f         f         f         f
			* |    s    |    s    |    s    |
			* |    |    |    |    |    |    |
			*       ----     Frame Close
			*         -- --   Frame Center
			*		     ---- Frame Open
			* Frame Closed - In this scenario, a given frame 'n' is when the shutter should close, meaning that
			*                the accumulation is the motion that happened before the frame.
			* Frame Center - In this scenario, a given frame 'n' is split evenly with half of the motion coming
			*                from before that frame, and half afterwards. In this diagram, the space in the
			*                middle is irrelevant (-- --), and is just there to make the diagram line up evenly.
			* Frame Opened - In this scenario, a given frame 'n' is when the shutter should open, meaning that
			*                the accumulation is the motion that happens after the frame.
			*
			* Depending on the shutter angle the spacing between the frames will change, ie: 270 shutter angle
			* means that the accumulation will happen across 75% of the time. In the example below, this assumes
			* that you are using Frame Close, meaning that the 75% of time before each frame.
			* 
			* f         f         f         f
			* |  s      |  s      |  s      |
			* |  |      |  |      |  |      |
			*     ------    ------    ------ Frame Close accumulation.
			*
			* The above offset (Frame Open/Closed/Centered) is the first timing offset that we apply, which only
			* changes what data is sampled to be considered a given frame n. We need to apply a second offset to
			* our sampling, because motion blur is centered around the object. For our motion blur to accurately
			* represent the period of time denoted on the chart, we need to offset the sample by half of the length
			* of a sub-frame so that when the blur is applied on either side of it, no pixels beyond where the frame
			* lies are affected.
			*
			* These offsets are calculated at the start of the shot and counter-adjusted for when calculating out which
			* frame number the output frames should end up with.
			*
			* In the diagram below, 'f' marks an output frame, 's' marks the shutter angle time, and '-' marks where
			* we actually sample time to generate a sub-frame (for which there are 4 in this example).
			*
			* f               f               f
			* |       s       |       s       |
			* |       |       |       |       |
			* |       |-|-|-|-|               |
			*
			* Finally, the tricky part. Because time in Unreal only goes forward (due to world tick) and renders may
			* be very expensive, we need to minimize any extra, unnecessary renders. To emulate Shutter Angle, there is
			* a period of time where the shutter is closed. We still need this time to pass in the world, but we don't
			* want our camera motion blur to stretch to cover this gap. One solution would be to render an extra frame
			* (with a duration of |-|) right before the first accumulation frame but this would be unnecessarily expensive.
			* 
			* Instead, what we do is we jump from the last accumulation frame to the start of the next frame (keeping in
			* mind that we have a timing offset meaning that we're in the center of the frame), but we override the motion
			* blur for this frame and say that the motion blur only has an effective length matching |-|, even though 
			* |-----| time has passed. Our motion blur amount is a 0-1 fraction, so to calculate the desired motion blur
			* for the first frame we take the length of a sub-frame and divide it by the total number of time that has
			* passed. Because the motion blur is centered and we've landed in the middle of the first accumulation frame
			* this ends up being the correct length and centered at the correct period of time. Yay!
			*
			* f               f               f
			* |       s       |       s       |
			* |       |       |       |       |
			* |       |-|-|-|-|       |-|-|-|-|
			*                ^         ^
			*        Jump From         Jump To
			*
			* The only remaining thing to watch out for is accumulation drift. The Movie Pipeline does not rely on 
			* accumulating delta times via engine-tick (even though we control engine tick) to avoid any floating point
			* issues. Instead, we calculate the exact number of ticks to move each time and store the accumulated time as 
			* ticks. Integers don't drift, but certain tick rates/accumulation sub-frame counts can end up not moving time
			* forward by a whole number.
			*
			* Given a default tick rate of 24,000 ticks per second and a output frame rate of 24fps, we have 1,000 ticks to
			* represent each frame. This is a great deal of precision, but it doesn't divide evenly by the number of samples
			* they may choose, such as three. 1000/3 = 333.3333 ticks per sub-frame. Since ticks are our finest resolution
			* we have to either advance time by 333 or 334. To account for this, we need a running tally of how much we're
			* drifting behind and then add it once it crosses the threshold.
			*
			* The reason we derive the Sequence time from our own time, and we derive our own time from the series of multiple
			* samples, is to handle slo-mo tracks. Slo-mo is not linear and can change each frame (which is why we multiply our
			* final delta amount by it) but to ensure the newly generated frames have the most correct timecode we move time
			* on our own, and find the nearest frame number and timecode from the sequence. When slo-mo tracks are not in play
			* the numbers should always add up evenly and it will be an exact 1-to-1 match.
			*
			* Easy!
			*/

			
			// Shouldn't be in this else branch if they don't have an Accumulation Setting for this shot.
			check(CameraSettings);
			check(CameraSettings->TemporalSampleCount > 1);
				
			const bool bFirstFrame = CachedOutputState.TemporalSampleIndex == 0;
			if (bFirstFrame)
			{
				/* The first sub-frame has its timing calculated differently than the rest, because we're trying to skip
				* any unnecessary ticks so our world/engine delta time is long (to cover the gap where the shutter is 
				* closed) but the motion blur shutter angle is overridden to be short to simulate only the portion of time
				* with the shutter open that we care about.
				* f               f               f
				* |       s       |       s       |
				* |       |       |       |       |
				* |       |-|-|-|-|       |-|-|-|-|
				*                ^         ^
				*        Jump From         Jump To
				*
				* The duration between the last accumulation frame and the start of the next accumulation frame is
				* (360-ShutterAngle)/360 * Ticks Per Frame + Ticks Per Subframe.
				* The (360-ShutterAngle)/360 gives us the fraction of the output frame that the shutter was closed,
				* so in a 270 shutter angle (open 75% of the time) at 24000 ticks/24fps (1000 ticks per frame) the
				* shutter is closed for 1000 * 0.25 = 250 ticks. We need to add the duration of one accumulation frame
				* so that we end up in the middle of the first frame, and not before the first frame.
				*
				* We don't actually end up adding half offsets or shutter biases because the entire starting index has
				* been adjusted for those and they are constant throughout the shot.
				*/


				// Finally, we take the time it is closed plus the sample duration to put us into the new sample.
				DeltaFrameTime = FrameMetrics.TicksWhileShutterClosed + FrameMetrics.TicksPerSample;

				while (AccumulatedTickSubFrameDeltas >= 1.f)
				{
					// We add the deltas on the start of the first sample, since it's the one where 
					// we jump by a non-consistent amount of time anyways.
					DeltaFrameTime += FFrameTime(FFrameNumber(1));
					AccumulatedTickSubFrameDeltas -= 1.f;
				}
			}
			else
			{
				// We're moving from one sub-frame to the next. Our calculation is simpler.
				DeltaFrameTime = FrameMetrics.TicksPerSample;
			}
		}

		

		// Now that we've calculated our delta ticks, we need to multiply it by
		// time dilation so that we advance through the sequence as slow as we
		// advance through the world.
		bool bWasAffectedByTimeDilation = false;
		if (!FMath::IsNearlyEqual(WorldTimeDilation, 1.f))
		{
			UE_LOG(LogMovieRenderPipeline, VeryVerbose, TEXT("[%d] Modified FrameDeltaTime by a factor of %f to account for World Time Dilation."), GFrameCounter, WorldTimeDilation);

			// This is technically always a frame behind as this function executes before Sequencer evaluates slow-mo tracks
			// and sets the World Time Dilation, but it is close enough for now.
			DeltaFrameTime = DeltaFrameTime * WorldTimeDilation;
			bWasAffectedByTimeDilation = true;
		}

		/*
		* Now that we know how much we want to advance the sequence by (and the world)
		* We can calculate out the desired frame for the shot to evaluate. This is slightly
		* complicated, as it's not just a simple increment. The actual evaluation position
		* is modified by two things:
		*
		* - Shutter Timing: To affect whether the motion before, during, or after
		*                   the frame count as this frame, we add this offset to
		*                   the sampled range.
		*
		* - Motion Blur:    Unreal Motion blur is centered on the object. To
		*                   do our best to ensure that the written pixels match
		*					the motion that would have occurred during a frame,
		*                   we offset by half of a sample to ensure our samples
		*                   are in the middle and stretch half way to each side.
		*
		* When the shutter timing is Frame Open (meaning the motion all comes from
		* after the shutter opens) we have to add half a sample's duration so that
		* our motion blur is centered.
		* When the shutter timing is Frame Center, we add half a sample's duration
		* which counter-acts the half-sample removed by the shutter timing offset
		* which ends up centering our sample exactly on the frame.
		* When the shutter timing is Frame Close, we subtract an entire sample's
		* duration and add half of it back, meaning we only sample information
		* before the current frame, but stretch the motion to cover up to that.
		*
		* For example, example we can look at the following situations when using
		* a 24fps sequence, 24000 tick resolution (1000 tick per frame), and
		* 270* shutter angle.
		*
		* 270* Shutter Angle means the shutter is open for 750 ticks (1000*0.75)
		* since we want to stretch from the 0 to 750, and our motion blur is bi
		* directional, we offset 350 ticks (750/2) to center it.
		*/

		// Convert any sub-frame time we have into a rolling accumulation
		AccumulatedTickSubFrameDeltas += DeltaFrameTime.GetSubFrame();

		// Increment where we should evaluate on the current camera cut.
		CurrentCameraCut.CurrentTick += DeltaFrameTime.GetFrame();

		FFrameTime FinalEvalTime = FrameMetrics.GetFinalEvalTime(CurrentCameraCut.CurrentTick);
		UE_LOG(LogMovieRenderPipeline, VeryVerbose, TEXT("[%d] FinalEvalTime: %s (Tick: %d SOffset: %d MOffset: %d)"), 
			GFrameCounter, *LexToString(FinalEvalTime.FloorToFrame()),
			CurrentCameraCut.CurrentTick.Value, FrameMetrics.ShutterOffsetTicks.GetFrame().Value, FrameMetrics.MotionBlurCenteringOffsetTicks.GetFrame().Value);

		
		// If the user doesn't want to render every frame we need to increase the delta time so that enough
		// time passes in the world. The framerate of the output media will be adjusted to match.
		// FrameDeltaTime = FrameDeltaTime * 1.0 / (double)Config->OutputFrameStep.Value;

		double FrameDeltaTime = FrameMetrics.TickResolution.AsSeconds(FFrameTime(DeltaFrameTime.GetFrame()));
		CachedOutputState.FrameDeltaTime = FrameDeltaTime;
		CachedOutputState.WorldSeconds = CachedOutputState.WorldSeconds + FrameDeltaTime;
		CachedOutputState.bWasAffectedByTimeDilation = bWasAffectedByTimeDilation;

		// The combination of shutter angle percentage, non-uniform render frame delta times and dividing by sample
		// count produce the correct length for motion blur in all cases.
		CachedOutputState.MotionBlurFraction = FrameMetrics.ShutterAnglePercentage / CameraSettings->TemporalSampleCount;

		// Update our Output State with any data we need to derive based on our current time.
		CalculateFrameNumbersForOutputState(FrameMetrics, CurrentCameraCut, CachedOutputState);

		// Now that we know the delta time for the upcoming frame, we can see if this time would extend past the end of our
		// camera cut. If it does, we don't want to produce this frame as that would produce an extra frame at the end!
		FFrameNumber EndOfCameraCut = CurrentCameraCut.TotalOutputRange.GetUpperBoundValue();
		if (CurrentCameraCut.CurrentTick >= EndOfCameraCut)
		{
			// If this isn't the last shot, we'll immediately call this function again to just
			// determine a new setup/seek/etc. on the same engine tick. Otherwise we would have
			// used a random delta time that didn't actually correlate to anything.
			if (!ProcessEndOfCameraCut(CurrentShot, CurrentCameraCut))
			{
				CachedOutputState.TemporalSampleIndex = -1;
				TickProducingFrames();
				return;
			}
		}

		// If we've made it this far, we're going to produce a frame so we can check to see if we want to increment the output
		// number. If we do it before the above block, then the last frame of each shot increments this global state incorrectly.
		if (CachedOutputState.TemporalSampleIndex == 0)
		{
			CachedOutputState.OutputFrameNumber++;
		}
		
		// Set our time step for the next frame
		CustomTimeStep->SetCachedFrameTiming(MoviePipeline::FFrameTimeStepCache(FrameDeltaTime));
		CustomSequenceTimeController->SetCachedFrameTiming(FQualifiedFrameTime(FinalEvalTime, FrameMetrics.TickResolution));
		return;
	}

	// This function should not be called when the shot isn't in one of the above states.
	check(false);
}

void UMoviePipeline::CalculateFrameNumbersForOutputState(const MoviePipeline::FFrameConstantMetrics& InFrameMetrics, const FMoviePipelineCameraCutInfo& InCameraCut, FMoviePipelineFrameOutputState& InOutOutputState) const
{
	// Find the closest number on the source Sequence. This may produce duplicates when using slow-mo tracks.
	FFrameRate SourceDisplayRate = TargetSequence->GetMovieScene()->GetDisplayRate();

	// "Closest" isn't straightforward when using temporal sub-sampling, ie: A large enough shutter angle pushes a sample over the half way point and it rounds to
	// the wrong one. Because temporal sub-sampling isn't centered around a frame (the centering is done via the final eval time) we can just subtract TS*TPS to get our centered value.
	FFrameNumber CenteringOffset = FFrameNumber(InOutOutputState.TemporalSampleIndex * InFrameMetrics.TicksPerSample.RoundToFrame().Value);
	FFrameNumber CenteredTick = InCameraCut.CurrentTick - CenteringOffset;

	InOutOutputState.SourceFrameNumber = FFrameRate::TransformTime(CenteredTick, InFrameMetrics.TickResolution, SourceDisplayRate).RoundToFrame();
	InOutOutputState.SourceTimeCode = FTimecode::FromFrameNumber(InOutOutputState.SourceFrameNumber, InFrameMetrics.FrameRate, false);
	InOutOutputState.EffectiveFrameNumber = FFrameRate::TransformTime(CenteredTick, InFrameMetrics.TickResolution, InFrameMetrics.FrameRate).RoundToFrame();
	InOutOutputState.EffectiveTimeCode = FTimecode::FromFrameNumber(InOutOutputState.SourceFrameNumber, InFrameMetrics.FrameRate, false);
}