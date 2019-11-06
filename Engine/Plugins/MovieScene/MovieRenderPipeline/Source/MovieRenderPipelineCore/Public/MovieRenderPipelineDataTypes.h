// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Misc/Timecode.h"
#include "ImagePixelData.h"
#include "MovieRenderPipelineDataTypes.generated.h"

class UMovieSceneCinematicShotSection;

template<typename ElementType> class TRange;
template<class T, class TWeakObjectPtrBase> struct TWeakObjectPtr;

class UWorld;
class ULevelSequence;
class UMovieSceneCinematicShotSection;
class UMoviePipelineMasterConfig;
class UMoviePipelineShotConfig;
class UMovieSceneCameraCutSection;
class UMoviePipelineRenderPass;


/**
* What is the current overall state of the Pipeline? States are processed in order from first to
* last and all states will be hit (though there is no guarantee the state will not be transitioned
* away from on the same frame it entered it). Used to help track overall progress and validate
* code flow.
*/
UENUM(BlueprintType)
enum class EMovieRenderPipelineState : uint8
{
	/** The pipeline has not been initialized yet. Only valid operation is to call Initialize. */
	Uninitialized = 0,
	/** The pipeline has been initialized and is now controlling time and working on producing frames. */
	ProducingFrames = 1,
	/** All desired frames have been produced. Audio is already finalized. Outputs have a chance to finish long processing tasks. */
	Finalize = 2,
	/** All outputs have finished writing to disk or otherwise processing. Additional exports that may have needed information about the produced file can now be run. */
	Export = 3,
	/** The pipeline has been shut down. It is an error to shut it down again. */
	Shutdown = 4,
};

/**
* What is the current state of a shot? States are processed in order from first to last but not
* all states are required, ie: WarmUp and MotionBlur can be disabled and the shot will never
* pass through this state.
*/
UENUM(BlueprintType)
enum class EMovieRenderShotState : uint8
{
	/** The shot has not been initialized yet.*/
	Uninitialized = 0,

	/** The shot is warming up. Engine ticks are passing but no frames are being produced. */
	WarmingUp = 1,
	/*
	* The shot is doing additional pre-roll for motion blur. No frames are being produced,
	* but the rendering pipeline is being run to seed histories.
	*/
	MotionBlur = 2,
	/*
	* The shot is working on producing frames and may be currently doing a sub-frame or
	* a whole frame.
	*/
	Rendering = 3,
	/*
	* The shot has produced all frames it will produce. No more evaluation should be
	* done for this shot once it reaches this state.
	*/
	Finished = 4
};

struct FMoviePipelinePassIdentifier
{
	FMoviePipelinePassIdentifier(const FName& InPassName)
		: Name(InPassName)
	{
	}

	FName Name;
};

namespace MoviePipeline
{
	/**
	* Utility function for the Pipeline to calculate the time metrics for the current
	* frame that can then be used by the custom time step to avoid custom time logic.
	*/
	struct FFrameTimeStepCache
	{
		FFrameTimeStepCache()
			: DeltaTime(0.0)
		{}

		FFrameTimeStepCache(double InDeltaTime)
			: DeltaTime(InDeltaTime)
		{}

		double DeltaTime;
	};

	struct FOutputFrameData
	{
		FIntPoint Resolution;

		TArray<FColor> ColorBuffer;

		FString PassName;
	};

	struct FFrameConstantMetrics
	{
		/** What is the tick resolution fo the sequence */
		FFrameRate TickResolution;
		/** What is the effective frame rate of the output */
		FFrameRate FrameRate;
		/** How many ticks per output frame. */
		FFrameTime TicksPerOutputFrame;
		/** How many ticks per sub-frame sample. */
		FFrameTime TicksPerSample;
		/** How many ticks while the shutter is closed */
		FFrameTime TicksWhileShutterClosed;
		/** How many ticks while the shutter is opened */
		FFrameTime TicksWhileShutterOpen;

		/** Fraction of the output frame that the accumulation frames should cover. */
		double ShutterAnglePercentage;

		/** Fraction of the output frame that the shutter is closed (that accumulation frames should NOT cover). */
		double ShutterClosedFraction;

		/**
		* Our time samples are offset by this many ticks. For a given output frame N, we can bias the time N represents
		* to be before, during or after the data stored on time N.
		*/
		FFrameTime ShutterOffsetTicks;

		/** 
		* How many ticks do we have to go to center our motion blur? This makes it so the blurred area matches the object
		* position during the time it represents, and not before.
		*/
		FFrameTime MotionBlurCenteringOffsetTicks;

		FFrameTime GetFinalEvalTime(const FFrameNumber InTime) const
		{
			// We just use a consistent offset from the given time to take motion blur centering and
			// shutter timing offsets into account. Written here just to consolidate various usages.
			return FFrameTime(InTime) + MotionBlurCenteringOffsetTicks + ShutterOffsetTicks;
		}


	};
}

USTRUCT(BlueprintType)
struct FMoviePipelineWorkInfo
{
	GENERATED_BODY()

public:
	FMoviePipelineWorkInfo()
		: NumCameraCuts(0)
		, NumOutputFrames(0)
		, NumUtilityFrames(0)
		, NumOutputFramesWithSubsampling(0)
		, NumSamples(0)
		, NumTiles(0)
	{
	}

	FString ToDisplayString()
	{
		return FString::Printf(TEXT("Camera Cuts: %d Output Frames: %d SubsampledOutputFrames: %d TotalSamples: %d UtilityFrames: %d TileCount: %d"),
			NumCameraCuts, NumOutputFramesWithSubsampling, NumOutputFramesWithSubsampling, NumSamples, NumUtilityFrames, NumTiles);
	}

	/** Either the current camera cut number (not index) or the total number of camera cuts. */
	int32 NumCameraCuts;
	/** How many frames do we expect to see written to disk. */
	int32 NumOutputFrames;
	/** How many extra engine ticks are run for utility reasons that don't produce any output. */
	int32 NumUtilityFrames;
	/** OutputFrames * TemporalSamples. This many engine frames are passed during the production of all Output Frames. */
	int32 NumOutputFramesWithSubsampling;
	/** NumOutputFrames * NumTemporalSamples * SpatialSamples * NumTiles */
	int32 NumSamples;
	/** Either the current tile number (not index) or the total number of tiles */
	int32 NumTiles;

	FMoviePipelineWorkInfo& operator += (const FMoviePipelineWorkInfo& InRHS)
	{
		NumCameraCuts += InRHS.NumCameraCuts;
		NumOutputFrames += InRHS.NumOutputFrames;
		NumUtilityFrames += InRHS.NumUtilityFrames;
		NumOutputFramesWithSubsampling += InRHS.NumOutputFramesWithSubsampling;
		NumSamples += InRHS.NumSamples;
		NumTiles += InRHS.NumTiles;

		return *this;
	}

	friend FMoviePipelineWorkInfo operator+ (const FMoviePipelineWorkInfo& InLHS, const FMoviePipelineWorkInfo& InRHS)
	{
		FMoviePipelineWorkInfo Sum = InLHS;
		Sum += InRHS;
		return Sum;
	}

	bool operator == (const FMoviePipelineWorkInfo& InRHS) const
	{
		return	NumCameraCuts == InRHS.NumCameraCuts
				&& NumOutputFrames == InRHS.NumOutputFrames
				&& NumUtilityFrames == InRHS.NumUtilityFrames
				&& NumOutputFramesWithSubsampling == InRHS.NumOutputFramesWithSubsampling
				&& NumSamples == InRHS.NumSamples
				&& NumTiles == InRHS.NumTiles;
	}

	bool operator != (const FMoviePipelineWorkInfo& InRHS) const
	{
		return !(*this == InRHS);
	}


};



UENUM(BlueprintType)
enum class EMoviePipelineShutterTiming : uint8
{
	FrameOpen,
	FrameCenter,
	FrameClose
};

USTRUCT(BlueprintType)
struct FMoviePipelineCameraCutInfo
{
	GENERATED_BODY()
public:
	FMoviePipelineCameraCutInfo()
		: OriginalRange(TRange<FFrameNumber>::Empty())
		, TotalOutputRange(TRange<FFrameNumber>::Empty())
		, NumWarmUpFrames(0)
		, bAccurateFirstFrameHistory(true)
		, NumTemporalSamples(0)
		, NumSpatialSamples(0)
		, NumTiles(0)
		, State(EMovieRenderShotState::Uninitialized)
		, CurrentTick(FFrameNumber(0))
		, bHasEvaluatedMotionBlurFrame(false)
		, bHasNotifiedFrameProductionStarted(false)
		, NumWarmUpFramesRemaining(0)
	{}

	bool IsInitialized() const { return State != EMovieRenderShotState::Uninitialized; }
	void SetNextState(const EMovieRenderShotState InCurrentState);

	/** 
	* Returns the number of expected frames to be produced by Initial Range + Handle Frames given a Frame Rate.
	* This will be inaccurate when PlayRate tracks are involved.
	*/
	FFrameNumber GetOutputFrameCountEstimate() const;

	/**
	* Returns the expected number of frames different frames that will be submitted for rendering. This is
	* the number of output frames * temporal samples. This will be inaccurate when PlayRate tracks are involved.
	*/
	FFrameNumber GetTemporalFrameCountEstimate() const;

	/** 
	* Returns misc. utility frame counts (Warm Up + MotionBlur Fix) since these are outside the ranges.
	*/
	FFrameNumber GetUtilityFrameCountEstimate() const;

	/**
	* Returns the number of samples submitted to the GPU, optionally counting samples for the various parts.
	* This will be inaccurate when PlayRate tracks are involved.
	*/
	FFrameNumber GetSampleCountEstimate(const bool bIncludeWarmup = true, const bool bIncludeMotionBlur = true) const;
	
	/**
	* Summarizes the total expected amount of work into one easy to access struct.
	*/
	FMoviePipelineWorkInfo GetTotalWorkEstimate() const;

public:
	/** The original non-modified range for this shot that will be rendered. */
	TRange<FFrameNumber> OriginalRange;

	/** The range for this shot including handle frames that will be rendered. */
	TRange<FFrameNumber> TotalOutputRange;

	/** How many warm up frames should this shot wait for. */
	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	int32 NumWarmUpFrames;

	/** Should we evaluate/render an extra frame at the start of this shot to show correct motion blur on the first frame? */
	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	bool bAccurateFirstFrameHistory;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Movie Render Pipeline")
	TWeakObjectPtr<UMovieSceneCameraCutSection> CameraCutSection;

	/** How many temporal samples is each frame broken up into? */
	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	int32 NumTemporalSamples;

	/** For each temporal sample, how many spatial samples do we take? */
	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	int32 NumSpatialSamples;

	/** How many image tiles are going to be rendered per temporal frame. */
	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	int32 NumTiles;

	/** Display name for UI Purposes. */
	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	FString DisplayName;

	/** Cached Frame Rate these are being rendered at. Simplifies some APIs. */
	FFrameRate CachedFrameRate;

	/** Cached Tick Resolution our numbers are in. Simplifies some APIs. */
	FFrameRate CachedTickResolution;
public:
	/** The current state of processing this Shot is in. Not all states will be passed through. */
	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	EMovieRenderShotState State;

	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	FFrameNumber CurrentTick;

	/** Metrics - How much work has been done for this particular shot. */
	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	FMoviePipelineWorkInfo CurrentWorkInfo;

	/** 
	* Metrics - How much work is there estimated to do for this shot? Can be more than estimated total due to PlayRate tracks. 
	* Cached after setup so that Blueprints can read the variable directly due to no support for UFunctions on Structs.
	*/
	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	FMoviePipelineWorkInfo TotalWorkInfo;

public:
	/** Have we evaluated the motion blur frame? Only used if bEvaluateMotionBlurOnFirstFrame is set */
	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	bool bHasEvaluatedMotionBlurFrame;

	/** Have we notified the output containers that we're about to begin producing frames? */
	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	bool bHasNotifiedFrameProductionStarted;

	/** How many warm up frames are left to process for this shot. May always be zero. */
	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	int32 NumWarmUpFramesRemaining;

	bool operator == (const FMoviePipelineCameraCutInfo& InRHS) const
	{
		return
			OriginalRange == InRHS.OriginalRange &&
			TotalOutputRange == InRHS.TotalOutputRange &&
			State == InRHS.State &&
			NumWarmUpFramesRemaining == InRHS.NumWarmUpFramesRemaining &&
			bAccurateFirstFrameHistory == InRHS.bAccurateFirstFrameHistory &&
			bHasEvaluatedMotionBlurFrame == InRHS.bHasEvaluatedMotionBlurFrame &&
			bHasNotifiedFrameProductionStarted == InRHS.bHasNotifiedFrameProductionStarted &&
			CurrentTick == InRHS.CurrentTick &&
			CameraCutSection == InRHS.CameraCutSection;
	}

	bool operator != (const FMoviePipelineCameraCutInfo& InRHS) const
	{
		return !(*this == InRHS);
	}
};

/**
* Pre-calculated information about a shot we are going to produce. This lets us build
* the expected output at the start of the process and just read from it later. Having
* all information in advanced aids in debugging and visualization of progress.
*/
USTRUCT(BlueprintType)
struct FMoviePipelineShotInfo
{
	GENERATED_BODY()
public:
	FMoviePipelineShotInfo()
		: NumHandleFrames(0)
		, OriginalRange(TRange<FFrameNumber>::Empty())
		, TotalOutputRange(TRange<FFrameNumber>::Empty())
		, HandleFrameRangeStart(TRange<FFrameNumber>::Empty())
		, HandleFrameRangeEnd(TRange<FFrameNumber>::Empty())
		, ShotConfig(nullptr)
		, CinematicShotSection(nullptr)
		, CurrentCameraCutIndex(0)
	{}

	FString GetDisplayName() const;

public:
	/** How many handle frames (in Display Rate of the Master Sequence) */
	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	int32 NumHandleFrames;

	/** The original non-modified (overall) range for this shot that will be rendered. */
	TRange<FFrameNumber> OriginalRange;

	/** The range for this shot including handle frames that will be rendered. */
	TRange<FFrameNumber> TotalOutputRange;

	TRange<FFrameNumber> HandleFrameRangeStart;
	TRange<FFrameNumber> HandleFrameRangeEnd;
	
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Movie Render Pipeline")
	UMoviePipelineShotConfig* ShotConfig;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Movie Render Pipeline")
	TWeakObjectPtr<UMovieSceneCinematicShotSection> CinematicShotSection;

	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	TArray<FMoviePipelineCameraCutInfo> CameraCuts;

	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	int32 CurrentCameraCutIndex;

	/** 
	* An array of the Render Passes for this particular shot. These are duplicated out of the settings
	* so that they can have their own state per shot that overlaps with other in progress shots. This
	* is useful for long running async tasks.
	*/
	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	TArray<UMoviePipelineRenderPass*> RenderPasses;

	FMoviePipelineCameraCutInfo& GetCurrentCameraCut()
	{
		check(CurrentCameraCutIndex >= 0 && CurrentCameraCutIndex < CameraCuts.Num());
		return CameraCuts[CurrentCameraCutIndex];
	}

	bool SetNextShotActive()
	{
		if (CurrentCameraCutIndex + 1 < CameraCuts.Num() - 1)
		{
			CurrentCameraCutIndex++;
			return false;
		}

		// We're on the last camera cut, we can't make another one active.
		return true;
	}

	bool operator == (const FMoviePipelineShotInfo& InRHS) const
	{ 
		return
			NumHandleFrames == InRHS.NumHandleFrames &&
			OriginalRange == InRHS.OriginalRange &&
			TotalOutputRange == InRHS.TotalOutputRange &&
			HandleFrameRangeStart == InRHS.HandleFrameRangeStart &&
			HandleFrameRangeEnd == InRHS.HandleFrameRangeEnd &&
			ShotConfig == InRHS.ShotConfig &&
			CinematicShotSection == InRHS.CinematicShotSection &&
			CameraCuts == InRHS.CameraCuts;
	}

	bool operator != (const FMoviePipelineShotInfo& InRHS) const
	{
		return !(*this == InRHS);
	}
};

/**
* The Tick/Render loops are decoupled from the actual desired output. In some cases
* we may render but not desire an output (such as filling temporal histories) or
* we may be accumulating the results into a target which does not produce an output.
* Finally, we may be running the Tick/Render loop but not wanting to do anything!
* This is the case when we are using the frame-step debugging, the engine is still
* live and running, but we don't want to advance the output process forwards at all.
*/
USTRUCT(BlueprintType)
struct FMoviePipelineFrameOutputState
{
	GENERATED_BODY()
public:
	FMoviePipelineFrameOutputState()
		: OutputFrameNumber(-1)
		, TotalSamplesRendered(0)
		, TemporalSampleIndex(-1)
		, MotionBlurFraction(0.5f)
		, FrameDeltaTime(0)
		, WorldSeconds(0)
		, bWasAffectedByTimeDilation(false)
	{
		// There is other code which relies on this starting on -1.
		check(OutputFrameNumber == -1);

		// Likewise, the code assumes you start on sample index 0.
		check(TemporalSampleIndex == -1);
	}

	/**
	* The expected output frame count that the render is working towards creating.
	* This number accurately tracks the number of frames we have produced even if
	* the file written to disk uses a different number (due to relative frame numbers
	* or offset frames being added.
	*/
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Movie Render Pipeline")
	int32 OutputFrameNumber;

	/**
	* This is an approximation of the number of samples submitted to the GPU to be rendered.
	* This only an approximation and is used to create an estimated time remaining approximation.
	*/
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Movie Render Pipeline")
	int32 TotalSamplesRendered;

	/** Which sub-frame are we on when using Accumulation Frame rendering. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Movie Render Pipeline")
	int32 TemporalSampleIndex;

	/** A 0-1 fraction of how much motion blur should be applied. This is tied to the shutter angle but more complicated when using sub-sampling */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Movie Render Pipeline")
	float MotionBlurFraction;

	/** The delta time in seconds used for this frame. */
	double FrameDeltaTime;

	/** The total elapsed time since the pipeline started.*/
	double WorldSeconds;
	
	/** The closest frame number (in Display Rate) on the Sequence. May be duplicates in the case of different output framerate or Play Rate tracks. */
	FFrameNumber SourceFrameNumber;

	/** The closest time code version of the SourceFrameNumber on the Sequence. May be a duplicate in the case of different output framerate or Play Rate tracks. */
	FTimecode SourceTimeCode;

	/** 
	* The closest frame number (in Display Rate) on the Sequence adjusted for the effective output rate. These numbers will not line up with the frame
	* in the source Sequence if the output frame rate differs from the Sequence display rate. May be a duplicate in the event of Play Rate tracks.
	*/
	FFrameNumber EffectiveFrameNumber;

	/** The closest time code version of the EffectiveFrameNumber. May be a duplicate in the event of Play Rate tracks. */
	FTimecode EffectiveTimeCode;

	/**
	* If true, there was a non-1.0 Time Dilation in effect when this frame was produced. This indicates that there 
	* may be duplicate frame Source/Effective frame numbers as they find the closest ideal time to the current.
	*/
	bool bWasAffectedByTimeDilation;

	bool operator == (const FMoviePipelineFrameOutputState& InRHS) const
	{
		return
			OutputFrameNumber == InRHS.OutputFrameNumber &&
			TemporalSampleIndex == InRHS.TemporalSampleIndex &&
			FMath::IsNearlyEqual(MotionBlurFraction,InRHS.MotionBlurFraction) &&
			FMath::IsNearlyEqual(FrameDeltaTime, InRHS.FrameDeltaTime) &&
			FMath::IsNearlyEqual(WorldSeconds, InRHS.WorldSeconds) &&
			SourceFrameNumber == InRHS.SourceFrameNumber &&
			SourceTimeCode == InRHS.SourceTimeCode &&
			EffectiveFrameNumber == InRHS.EffectiveFrameNumber &&
			EffectiveTimeCode == InRHS.EffectiveTimeCode &&
			bWasAffectedByTimeDilation == InRHS.bWasAffectedByTimeDilation;
	}

	bool operator != (const FMoviePipelineFrameOutputState& InRHS) const
	{
		return !(*this == InRHS);
	}

	friend uint32 GetTypeHash(FMoviePipelineFrameOutputState OutputState)
	{
		return GetTypeHash(OutputState.OutputFrameNumber);
	}
};

USTRUCT(BlueprintType)
struct FMoviePipelineRenderPassInitSettings
{
	GENERATED_BODY()
public:
	FMoviePipelineRenderPassInitSettings()
		: ShotConfig(nullptr)
		, TileResolution(0, 0)
		, TileCount(0)
	{
	}

public:
	/** The config associated with the shot currently being rendered. */
	UPROPERTY()
	UMoviePipelineShotConfig* ShotConfig;

	// These are derived out of other settings and cached directly to needing to re-derive values.
	
	/** The back buffer should match this setting and not the final output resolution.*/
	FIntPoint TileResolution;
	
	/** How many tiles (in each direction) are we rendering with. */
	int32 TileCount;
};

USTRUCT(BlueprintType)
struct FMoviePipelineRenderPassMetrics
{
	GENERATED_BODY()

public:
	FVector2D SpatialShift;
	int32 TileIndex;
	int32 TileIndexX;
	int32 TileIndexY;
	int32 NumTilesX;
	int32 NumTilesY;
	FMoviePipelineFrameOutputState OutputState;
	bool bIsHistoryOnlyFrame;
	int32 SpatialJitterIndex;
	int32 NumSpatialJitters;

	int32 TemporalJitterIndex;
	int32 NumTemporalJitters;

	float JitterOffsetX;
	float JitterOffsetY;

	float AccumulationGamma;

	bool bIsUsingOverlappedTiles;
	int32 OverlappedOffsetX;
	int32 OverlappedOffsetY;
	int32 OverlappedSizeX;
	int32 OverlappedSizeY;
	int32 OverlappedPadX;
	int32 OverlappedPadY;
	FVector2D OverlappedSubpixelShift;
};

struct FImagePixelDataPayload : IImagePixelDataPayload
{
	FMoviePipelineFrameOutputState OutputState;
	FVector2D SpatialShift;
	int32 TileIndexX;
	int32 TileIndexY;
	int32 TileSizeX;
	int32 TileSizeY;
	int32 NumTilesX;
	int32 NumTilesY;
	int32 SpatialJitterIndex;
	int32 NumSpatialJitters;

	int32 TemporalJitterIndex;
	int32 NumTemporalJitters;

	float JitterOffsetX;
	float JitterOffsetY;

	float AccumulationGamma;

	bool bIsUsingOverlappedTiles;
	int32 OverlappedOffsetX;
	int32 OverlappedOffsetY;
	int32 OverlappedSizeX;
	int32 OverlappedSizeY;
	int32 OverlappedPadX;
	int32 OverlappedPadY;
	FVector2D OverlappedSubpixelShift;


	FString PassName;
};

/**
* Describes an individual job executed by a Movie Pipeline Executor.
*/
USTRUCT(BlueprintType)
struct FMoviePipelineExecutorJob
{
	GENERATED_BODY()
public:
	FMoviePipelineExecutorJob()
		: Sequence()
		, Configuration(nullptr)
	{
	}

	FMoviePipelineExecutorJob(const FSoftObjectPath& InSequenceAsset, UMoviePipelineMasterConfig* InConfig)
		: Sequence(InSequenceAsset)
		, Configuration(InConfig)
	{
	}

	/** Which sequence should this job render? */
	UPROPERTY(BlueprintReadWrite, Category = "Movie Render Pipeline")
	FSoftObjectPath Sequence;

	/** What master configuration is used to render this sequence? This specifies output resolution/path, etc. */
	UPROPERTY(BlueprintReadWrite, Category = "Movie Render Pipeline")
	UMoviePipelineMasterConfig* Configuration;

	/** Which shots (by name) should this job render out of the given sequence? Leave empty to render whole sequence. */
	UPROPERTY(BlueprintReadWrite, Category = "Movie Render Pipeline")
	TArray<FString> ShotRenderMask;
};