// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Misc/Timecode.h"
#include "ImagePixelData.h"
#include "Containers/UnrealString.h"
#include "Engine/Scene.h"
#include "Engine/EngineTypes.h"
#include "DSP/BufferVectorOperations.h"
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
struct FImagePixelData;
struct FImageOverlappedAccumulator;
class FMoviePipelineOutputMerger;
class FRenderTarget;
class UMoviePipeline;
struct FMoviePipelineFormatArgs;
namespace Audio { class FMixerSubmix; }

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
	Finished = 4,
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
	FMoviePipelinePassIdentifier()
	{}

	FMoviePipelinePassIdentifier(const FString& InPassName)
		: Name(InPassName)
	{
	}

	bool operator == (const FMoviePipelinePassIdentifier& InRHS) const
	{
		return Name == InRHS.Name;
	}

	bool operator != (const FMoviePipelinePassIdentifier& InRHS) const
	{
		return !(*this == InRHS);
	}

	friend uint32 GetTypeHash(FMoviePipelinePassIdentifier OutputState)
	{
		return GetTypeHash(OutputState.Name);
	}

public:
	FString Name;
};

namespace MoviePipeline
{
	/**
	* Frame info needed for creating the FSceneView for each tile.
	*/
	struct FMoviePipelineFrameInfo
	{
	public:
		FMoviePipelineFrameInfo() {}
		virtual ~FMoviePipelineFrameInfo() {};

		/**
		 * We have to apply camera motion vectors manually. So we keep the current and previous fram'es camera view and rotation.
		 * Then we render a sequence of the same movement, and update after running the game sim.
		 **/

		/** Current frame camera view location **/
		FVector CurrViewLocation;

		/** Current frame camera view rotation **/
		FRotator CurrViewRotation;

		/** Previous frame camera view location **/
		FVector PrevViewLocation;

		/** Previous frame camera view rotation **/
		FRotator PrevViewRotation;
	};

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

	struct MOVIERENDERPIPELINECORE_API FTileWeight1D
	{
	public:
		/**
		 * For tiles, this struct stores the weight function in one dimension.
		 *
		 *                X1           X2
		 * 1.0              |------------|
		 *                 /              \
		 *                /                \
		 *               /                  \
		 * 0.0 |--------|                    |----------------|
		 *     0       X0                    X3             Size
		 *
		 *  For a tile, the weight goes from:
		 *  [0 ,X0]    : 0.0
		 *  [X0,X1]    : 0.0 to 1.0
		 *  [X1,X2]    : 1.0
		 *  [X2,X3]    : 1.0 to 0.0
		 *  [X3,SizeX] : 0.0
		 **/

		/** Constructor. Sets to invalid values and must be initialized to reasonable values before using. **/
		FTileWeight1D()
		{
			X0 = 0;
			X1 = 0;
			X2 = 0;
			X3 = 0;
		}

		/** Make sure this map has valid values. **/
		void CheckValid() const
		{
			check(0 <= X0);
			check(X0 <= X1);
			check(X1 <= X2);
			check(X2 <= X3);
		}

		/** Is equal operator.**/
		bool operator==(const FTileWeight1D& Rhs) const;

		/**
		 * The full tile is of size PadLeft + SizeCenter + PadRight
		 *
		 *  |------PadLeft-------|------------SizeCenter-----------|------PadRight-----|
		 *  
		 * This function puts X0 in the middle of PadLeft, X3 in the middle of PadRight.
		 * And X1 is "reflected" around PadLeft, and X2 is "reflected" around PadRight.
		 *
		 *  |------PadLeft-------|------------SizeCenter-----------|------PadRight-----|
		 *           X0                  X1                  X2              X3
		 * Also note that PadLeft and PadRight are allowed to be zero, but SizeCenter must be > 0;
		 **/
		void InitHelper(int32 PadLeft, int32 SizeCenter, int32 PadRight);

		float CalculateWeight(int32 Pixel) const;

		void CalculateArrayWeight(TArray<float>& WeightData, int Size) const;

		int32 X0;
		int32 X1;
		int32 X2;
		int32 X3;
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
struct FMoviePipelineSegmentWorkMetrics
{
	GENERATED_BODY()

public:
	FMoviePipelineSegmentWorkMetrics()
		: SegmentName()
		, OutputFrameIndex(-1)
		, TotalOutputFrameCount(-1)
		, OutputSubSampleIndex(-1)
		, TotalSubSampleCount(-1)
		, EngineWarmUpFrameIndex(-1)
		, TotalEngineWarmUpFrameCount(-1)
	{ }	
	
	// This information is used to be presented in the UI.
	
	/** The name of the segment (if any) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pipeline Segment")
	FString SegmentName;
	
	/** Index of the output frame we are working on right now. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pipeline Segment")
	int32 OutputFrameIndex;
	/** The number of output frames we expect to make for this segment. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pipeline Segment")
	int32 TotalOutputFrameCount;


	/** Which temporal/spatial sub sample are we working on right now. This counts temporal, spatial, and tile samples to accurately reflect how much work is needed for this output frame. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pipeline Segment")
	int32 OutputSubSampleIndex;
	
	/** The total number of samples we will have to build to render this output frame. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pipeline Segment")
	int32 TotalSubSampleCount;
	
	/** The index of the engine warm up frame that we are working on. No rendering samples are produced for these. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pipeline Segment")
	int32 EngineWarmUpFrameIndex;
	
	/** The total number of engine warm up frames for this segment. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pipeline Segment")
	int32 TotalEngineWarmUpFrameCount;
};


UENUM(BlueprintType)
enum class EMoviePipelineShutterTiming : uint8
{
	FrameOpen,
	FrameCenter,
	FrameClose
};

// ToDo: Rename this to segment.
USTRUCT(BlueprintType)
struct FMoviePipelineCameraCutInfo
{
	GENERATED_BODY()
public:
	FMoviePipelineCameraCutInfo()
		: OriginalRange(TRange<FFrameNumber>::Empty())
		, TotalOutputRange(TRange<FFrameNumber>::Empty())
		, bEmulateFirstFrameMotionBlur(true)
		, NumTemporalSamples(0)
		, NumSpatialSamples(0)
		, NumTiles(0, 0)
		, State(EMovieRenderShotState::Uninitialized)
		, CurrentMasterSeqTick(FFrameNumber(0))
		, CurrentLocalSeqTick(FFrameNumber(0))
		, bHasEvaluatedMotionBlurFrame(false)
		, NumEngineWarmUpFramesRemaining(0)
	{
	}

	bool IsInitialized() const { return State != EMovieRenderShotState::Uninitialized; }
	void SetNextStateAfter(const EMovieRenderShotState InCurrentState);
	void CalculateWorkMetrics();

private:
	FFrameNumber GetOutputFrameCountEstimate() const;

public:
	/** The original non-modified range for this shot that will be rendered. */
	TRange<FFrameNumber> OriginalRange;

	/** The range for this shot including handle frames that will be rendered. */
	TRange<FFrameNumber> TotalOutputRange;

	/** 
	* Should we evaluate/render an extra frame at the start of this shot to show correct motion blur on the first frame? 
	* This emulates motion blur by evaluating forward one frame and then going backwards which is a approximation.
	*/
	bool bEmulateFirstFrameMotionBlur;

	/** How many temporal samples is each frame broken up into? */
	int32 NumTemporalSamples;

	/** For each temporal sample, how many spatial samples do we take? */
	int32 NumSpatialSamples;

	/** How many image tiles are going to be rendered per temporal frame. */
	FIntPoint NumTiles;

	/** Display name for UI Purposes. */
	FString CameraName;
	FString ShotName;

	/** Cached Frame Rate these are being rendered at. Simplifies some APIs. */
	FFrameRate CachedFrameRate;

	/** Cached Tick Resolution our numbers are in. Simplifies some APIs. */
	FFrameRate CachedTickResolution;
	
	
	TWeakObjectPtr<UMovieSceneCameraCutSection> CameraCutSection;
public:
	/** The current state of processing this Shot is in. Not all states will be passed through. */
	EMovieRenderShotState State;

	/** The current tick of this shot that we're on in master sequence space, for evaluating the master sequence at this time. */
	FFrameNumber CurrentMasterSeqTick;

	/** The current tick of this shot that we're on local space, for knowing which frame of this sub-section is equivalent to the master. */
	FFrameNumber CurrentLocalSeqTick;

	/** Metrics - How much work has been done for this particular shot and how much do we estimate we have to do? */
	FMoviePipelineSegmentWorkMetrics WorkMetrics;

public:
	/** Have we evaluated the motion blur frame? Only used if bEvaluateMotionBlurOnFirstFrame is set */
	bool bHasEvaluatedMotionBlurFrame;

	/** How many engine warm up frames are left to process for this shot. May be zero. */
	int32 NumEngineWarmUpFramesRemaining;



	bool operator == (const FMoviePipelineCameraCutInfo& InRHS) const
	{
		return
			OriginalRange == InRHS.OriginalRange &&
			TotalOutputRange == InRHS.TotalOutputRange &&
			State == InRHS.State &&
			NumEngineWarmUpFramesRemaining == InRHS.NumEngineWarmUpFramesRemaining &&
			bEmulateFirstFrameMotionBlur == InRHS.bEmulateFirstFrameMotionBlur &&
			bHasEvaluatedMotionBlurFrame == InRHS.bHasEvaluatedMotionBlurFrame &&
			CurrentMasterSeqTick == InRHS.CurrentMasterSeqTick &&
			CurrentLocalSeqTick == InRHS.CurrentLocalSeqTick &&
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
		, ShotOverrideConfig(nullptr)
		, CinematicShotSection(nullptr)
		, CurrentCameraCutIndex(0)
	{}

	FString GetDisplayName() const;

public:
	/** How many handle frames (in Display Rate of the Master Sequence) */
	int32 NumHandleFrames;

	/** The original non-modified (overall) range for this shot that will be rendered. */
	TRange<FFrameNumber> OriginalRange;

	/** The range for this shot including handle frames that will be rendered. */
	TRange<FFrameNumber> TotalOutputRange;

	/** The range of time that represents the handle frames (if any) before the shot. */
	TRange<FFrameNumber> HandleFrameRangeStart;

	/** The range of time that represents the handle frames (if any) after the shot. */
	TRange<FFrameNumber> HandleFrameRangeEnd;

	FFrameNumber StartFrameOffsetTick;
	
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Movie Render Pipeline")
	UMoviePipelineShotConfig* ShotOverrideConfig;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Movie Render Pipeline")
	TWeakObjectPtr<UMovieSceneCinematicShotSection> CinematicShotSection;

	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	TArray<FMoviePipelineCameraCutInfo> CameraCuts;

	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	int32 CurrentCameraCutIndex;

	FMoviePipelineCameraCutInfo& GetCurrentCameraCut()
	{
		check(CurrentCameraCutIndex >= 0 && CurrentCameraCutIndex < CameraCuts.Num());
		return CameraCuts[CurrentCameraCutIndex];
	}

	const FMoviePipelineCameraCutInfo& GetCurrentCameraCut() const
	{
		check(CurrentCameraCutIndex >= 0 && CurrentCameraCutIndex < CameraCuts.Num());
		return CameraCuts[CurrentCameraCutIndex];
	}

	bool SetNextShotActive()
	{
		if (CurrentCameraCutIndex == CameraCuts.Num() - 1)
		{
			// We're on the last camera cut, we can't make another one active.
			return true;
		}
		
		CurrentCameraCutIndex++;
		return false;
	}

	bool operator == (const FMoviePipelineShotInfo& InRHS) const
	{ 
		return
			NumHandleFrames == InRHS.NumHandleFrames &&
			OriginalRange == InRHS.OriginalRange &&
			TotalOutputRange == InRHS.TotalOutputRange &&
			HandleFrameRangeStart == InRHS.HandleFrameRangeStart &&
			HandleFrameRangeEnd == InRHS.HandleFrameRangeEnd &&
			ShotOverrideConfig == InRHS.ShotOverrideConfig &&
			CinematicShotSection == InRHS.CinematicShotSection &&
			CameraCuts == InRHS.CameraCuts &&
			CurrentCameraCutIndex == InRHS.CurrentCameraCutIndex;
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
*/
struct FMoviePipelineFrameOutputState
{
public:
	struct FTimeData
	{
		FTimeData()
			: MotionBlurFraction(0.f)
			, FrameDeltaTime(0.0)
			, WorldSeconds(0.0)
			, bWasAffectedByTimeDilation(false)
		{
		}

		/** A 0-1 fraction of how much motion blur should be applied. This is tied to the shutter angle but more complicated when using sub-sampling */
		float MotionBlurFraction;

		/** The delta time in seconds used for this frame. */
		double FrameDeltaTime;

		/** The total elapsed time since the pipeline started.*/
		double WorldSeconds;

		/**
		* If true, there was a non-1.0 Time Dilation in effect when this frame was produced. This indicates that there
		* may be duplicate frame Source/Effective frame numbers as they find the closest ideal time to the current.
		*/
		bool bWasAffectedByTimeDilation;

		void ResetPerFrameData()
		{
			MotionBlurFraction = 0.f;
			FrameDeltaTime = 0.0;
			bWasAffectedByTimeDilation = false;
		}
	};

	FMoviePipelineFrameOutputState()
		: OutputFrameNumber(-1)
		, ShotCount(0)
		, TemporalSampleIndex(-1)
		, TemporalSampleCount(0)
	{
		// There is other code which relies on this starting on -1.
		check(OutputFrameNumber == -1);

		// Likewise, the code assumes you start on sample index -1.
		check(TemporalSampleIndex == -1);

		ResetPerFrameData();
		ResetPerShotData();
	}

	/** Is this the first temporal sample for the output frame? */
	FORCEINLINE bool IsFirstTemporalSample() const { return TemporalSampleIndex == 0; }
	FORCEINLINE bool IsLastTemporalSample() const { return TemporalSampleIndex == (TemporalSampleCount - 1); }

	/**
	* The expected output frame count that the render is working towards creating.
	* This number accurately tracks the number of frames we have produced even if
	* the file written to disk uses a different number (due to relative frame numbers
	* or offset frames being added.
	*/
	int32 OutputFrameNumber;

	/** Which shot is this output state for? */
	int32 ShotIndex;

	/** How many shots total will we be outputting? */
	int32 ShotCount;

	/** Which sub-frame are we on when using Accumulation Frame rendering. */
	int32 TemporalSampleIndex;

	/** How many temporal samples do we add together to produce one Output Frame? */
	int32 TemporalSampleCount;

	/** 
	* The expected output frame count for this current shot that we're working towards
	* creating. Like OutputFrameNumber but relative to this shot. This should get reset between shots. 
	*/
	int32 ShotOutputFrameNumber;

	/** The total number of samples (including warm ups) that have been sent to the GPU for this shot. */
	int32 ShotSamplesRendered;

	/** What time data should this frame use? Can vary between samples when TemporalSampleCount > 1. */
	FTimeData TimeData;

	/** The name of the currently active camera being rendered. May be empty. */
	FString CameraName;

	/** THe name of the currently active shot. May be empty if there is no shot track. */
	FString ShotName;

	/** INFORMATION BELOW HERE SHOULD NOT GET PERSISTED BETWEEN FRAMES */

	void ResetPerFrameData()
	{
		TimeData.ResetPerFrameData();
		bSkipRendering = false;
		bDiscardRenderResult = false;
		SourceFrameNumber = 0;
		SourceTimeCode = FTimecode();
		EffectiveFrameNumber = 0;
		EffectiveTimeCode = FTimecode();
		CurrentShotSourceFrameNumber = 0;
		CurrentShotSourceTimeCode = FTimecode();
	}

	void ResetPerShotData()
	{
		ShotOutputFrameNumber = -1;
		ShotSamplesRendered = 0;
		ShotIndex = 0;
		TemporalSampleCount = 0;
		CameraName.Reset();
		ShotName.Reset();
	}

	/**
	* If true, then the rendering for this frame should be skipped (ie: nothing submitted to the gpu, and the output merger not told to expect this frame).
	* This is used for rendering every Nth frame for rendering drafts. We still run the game thread logic for the skipped frames (which is relatively cheap)
	* and simply omit rendering them. This increases consistency with non-skipped renders, and will be useful for consistency when rendering on a farm.
	*/
	bool bSkipRendering;

	/**
	* If this is true, then the frame will be rendered but the results discarded and not sent to the accumulator. This is used for render warmup frames
	* or gpu-based feedback loops. Ignored if bSkipRendering is true.
	*/
	bool bDiscardRenderResult;


	/** The closest frame number (in Display Rate) on the Sequence. May be duplicates in the case of different output framerate or Play Rate tracks. */
	int32 SourceFrameNumber;

	/** The closest time code version of the SourceFrameNumber on the Sequence. May be a duplicate in the case of different output framerate or Play Rate tracks. */
	FTimecode SourceTimeCode;

	/** 
	* The closest frame number (in Display Rate) on the Sequence adjusted for the effective output rate. These numbers will not line up with the frame
	* in the source Sequence if the output frame rate differs from the Sequence display rate. May be a duplicate in the event of Play Rate tracks.
	*/
	int32 EffectiveFrameNumber;

	/** The closest time code version of the EffectiveFrameNumber. May be a duplicate in the event of Play Rate tracks. */
	FTimecode EffectiveTimeCode;


	int32 CurrentShotSourceFrameNumber;
	
	FTimecode CurrentShotSourceTimeCode;



	bool operator == (const FMoviePipelineFrameOutputState& InRHS) const
	{
		// ToDo: I don't think this is used anymore. Was used to make it so that the output state
		// for the first temporal sample (which is what the expected output was built off of) could
		// be equality against the last temporal sample when used in a TSet but better to do that explicitly.
		return
			OutputFrameNumber == InRHS.OutputFrameNumber &&
			// TemporalSampleIndex == InRHS.TemporalSampleIndex &&
			// FMath::IsNearlyEqual(MotionBlurFraction,InRHS.MotionBlurFraction) &&
			// FMath::IsNearlyEqual(FrameDeltaTime, InRHS.FrameDeltaTime) &&
			// FMath::IsNearlyEqual(WorldSeconds, InRHS.WorldSeconds) &&
			SourceFrameNumber == InRHS.SourceFrameNumber &&
			SourceTimeCode == InRHS.SourceTimeCode &&
			EffectiveFrameNumber == InRHS.EffectiveFrameNumber &&
			EffectiveTimeCode == InRHS.EffectiveTimeCode;
	}

	bool operator != (const FMoviePipelineFrameOutputState& InRHS) const
	{
		return !(*this == InRHS);
	}

	friend uint32 GetTypeHash(FMoviePipelineFrameOutputState OutputState)
	{
		return GetTypeHash(OutputState.OutputFrameNumber);
	}
	void GetFilenameFormatArguments(FMoviePipelineFormatArgs& InOutFormatArgs, const int32 InZeroPadCount, const int32 InFrameNumberOffset) const;
};

struct FMoviePipelineFormatArgs
{
	FMoviePipelineFormatArgs()
		: InJob(nullptr)
	{
	}

	/** A set of Key/Value pairs for format strings (without {}) and their values. */
	FStringFormatNamedArguments Arguments;

	/** Which job is this for? Some settings are specific to the level sequence being rendered. */
	class UMoviePipelineExecutorJob* InJob;
};

/**
* These parameters define a single sample that a render pass should render with.
*/
struct FMoviePipelineRenderPassMetrics
{
public:
	/** What FrameIndex should the FSceneView use for its internal index. Used to ensure samples have unique TAA/Raytracing/etc. sequences */
	int32 FrameIndex;

	/** Is the Scene View frozen so no changes are made as a result of this sample? */
	bool bWorldIsPaused;

	/** Do we pass the camera cut flag to the renderer to clear histories? */
	bool bCameraCut;

	/** Which anti-aliasing method should this view use? */
	EAntiAliasingMethod AntiAliasingMethod;

	/** What type of output should the Post Processing output? (HDR /w ToneCurve, HDR w/o ToneCurve) */
	ESceneCaptureSource SceneCaptureSource;

	/** How many tiles on X and Y are there total. */
	FIntPoint TileCounts;

	/** Of the TileCount, which X/Y Tile is this sample for. */
	FIntPoint TileIndexes;

	/** Get a 0-(TileCount-1) version of TileIndex. */
	FORCEINLINE int32 GetTileIndex() const
	{
		return (TileIndexes.Y * TileCounts.Y) + TileIndexes.X;
	}

	FORCEINLINE int32 GetTileCount() const
	{
		return (TileCounts.X * TileCounts.Y);
	}

	/** How big is the back buffer for this sample? This is the final target size divided by the number of tiles with padding added. */
	FIntPoint BackbufferSize;

	/** How big is an individual tile for this sample? This is the final target size divided by the number of tiles (without padding added) */
	FIntPoint TileSize;

	/** If true, we will discard this sample after rendering. Used to get history set up correctly. */
	bool bDiscardResult;

	/** If true, we will write this sample to disk (and also send it to the accumulator.) */
	bool bWriteSampleToDisk;

	/** How many spatial jitters will there be total for this particular temporal accumulation frame? */
	int32 SpatialSampleCount;
	
	/** Of the SpatialSampleCount, which index is this? */
	int32 SpatialSampleIndex;

	/** How many temporal jitters will there be total for this particular output frame? */
	int32 TemporalSampleCount;

	/** Of the TemporalSampleCount, which index is this? */
	int32 TemporalSampleIndex;

	/** How many pixels are we overlapping with adjacent tiles (on each side) */
	FIntPoint OverlappedPad;

	/** How much is this sample offset, taking padding into account. */
	FIntPoint OverlappedOffset;

	/** 
	* The gamma space to apply accumulation in. During accumulation, pow(x,AccumulationGamma) is applied
	* and pow(x,1/AccumulationGamma) is applied after accumulation is finished. 1.0 means no change."
	*/
	float AccumulationGamma;

	/**
	* The amount of screen-space shift used to replace TAA Projection Jitter, modified for each spatial sample
	* of the render.
	*/
	FVector2D ProjectionMatrixJitterAmount;

	/**
	* If set, forces the exposure compensation on a render. Useful when doing tiled renders where auto-exposure is disabled.
	*/
	TOptional<float> ExposureCompensation;

	/**
	* Any additional texture mip map bias that should be added when rendering. Can be used to force extra sharpness. A more negative number means more likely to use a higher quality mip map.
	*/
	float TextureSharpnessBias;

	/**
	* What screen percentage should each view be rendered at? 1.0 = standard size. Can gain more detail (at the cost of longer renders, more device timeouts), consider trying to use TextureSharpnessBias if possible instead.
	*/
	float GlobalScreenPercentageFraction;


	FMoviePipelineFrameOutputState OutputState;

	FVector2D OverlappedSubpixelShift;

	MoviePipeline::FTileWeight1D WeightFunctionX;
	MoviePipeline::FTileWeight1D WeightFunctionY;

	MoviePipeline::FMoviePipelineFrameInfo FrameInfo;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FMoviePipelineSampleReady, TArray<FFloat16Color>&, FMoviePipelineRenderPassMetrics);

namespace MoviePipeline
{
	struct FMoviePipelineRenderPassInitSettings
	{
	public:
		FMoviePipelineRenderPassInitSettings()
			: BackbufferResolution(0, 0)
			, TileCount(0, 0)
			, bAccumulateAlpha(false)
		{
		}

	public:
		/** This takes into account any padding needed for tiled rendering overlap. Different than the output resolution of the final image. */
		FIntPoint BackbufferResolution;

		/** How many tiles (in each direction) are we rendering with. */
		FIntPoint TileCount;

		/** If true, the alpha channel will be accumulated. This adds about 30% cost over the base to accumulation. */
		bool bAccumulateAlpha;
	};

	struct MOVIERENDERPIPELINECORE_API FMoviePipelineEnginePass
	{
		FMoviePipelineEnginePass(const FMoviePipelinePassIdentifier& InPassIdentifier)
			: PassIdentifier(InPassIdentifier)
		{
		}

		virtual ~FMoviePipelineEnginePass()
		{}

		virtual void Setup(TWeakObjectPtr<UMoviePipeline> InOwningPipeline, const FMoviePipelineRenderPassInitSettings& InInitSettings)
		{
			OwningPipeline = InOwningPipeline;
			InitSettings = InInitSettings;
		}

		virtual void RenderSample_GameThread(const FMoviePipelineRenderPassMetrics& InSampleState)
		{
		}

		virtual void Teardown()
		{
		}

		const FMoviePipelineRenderPassInitSettings& GetInitSettings() const { return InitSettings; }

	protected:
		UMoviePipeline* GetPipeline() const;

	public:
		/** A unique name for this engine pass. This is how an individual output pass specifies what data source it wants. */
		FMoviePipelinePassIdentifier PassIdentifier;

	protected:
		FMoviePipelineRenderPassInitSettings InitSettings;

	private:
		TWeakObjectPtr<UMoviePipeline> OwningPipeline;
	};

	struct FSampleRenderThreadParams
	{
		FMoviePipelineRenderPassMetrics SampleState;
		TSharedPtr<FImageOverlappedAccumulator, ESPMode::ThreadSafe> ImageAccumulator;
		TSharedPtr<FMoviePipelineOutputMerger, ESPMode::ThreadSafe> OutputMerger;
		FMoviePipelinePassIdentifier PassIdentifier;
		bool bAccumulateAlpha;
	};
}

struct FImagePixelDataPayload : IImagePixelDataPayload, public TSharedFromThis<FImagePixelDataPayload, ESPMode::ThreadSafe>
{
	FMoviePipelineFrameOutputState OutputState;
	FMoviePipelineRenderPassMetrics SampleState;

	FMoviePipelinePassIdentifier PassIdentifier;
	
	/** Does this output data have to be transparent to be useful? Overrides output format to one that supports transparency. */
	bool bRequireTransparentOutput;

	FImagePixelDataPayload()
		: bRequireTransparentOutput(false)
	{}

	/** Is this the first tile of an image and we should start accumulating? */
	FORCEINLINE bool IsFirstTile() const
	{
		return SampleState.TileIndexes.X == 0 && SampleState.TileIndexes.Y == 0 && SampleState.SpatialSampleIndex == 0;
	}

	/** Is this the last tile of an image and we should finish accumulating? */
	FORCEINLINE bool IsLastTile() const
	{
		return SampleState.TileIndexes.X == SampleState.TileCounts.X - 1 &&
			   SampleState.TileIndexes.Y == SampleState.TileCounts.Y - 1 &&
			   SampleState.SpatialSampleIndex == SampleState.SpatialSampleCount - 1;
	}

	FORCEINLINE bool IsFirstTemporalSample() const
	{
		return SampleState.TemporalSampleIndex == 0;
	}

	FORCEINLINE bool IsLastTemporalSample() const
	{
		return SampleState.TemporalSampleIndex == SampleState.TemporalSampleCount - 1;
	}
};

struct MOVIERENDERPIPELINECORE_API FMoviePipelineMergerOutputFrame
{
public:
	FMoviePipelineMergerOutputFrame() {}
	virtual ~FMoviePipelineMergerOutputFrame() {};

	FMoviePipelineMergerOutputFrame& operator=(FMoviePipelineMergerOutputFrame&& InOther)
	{
		FrameOutputState = InOther.FrameOutputState;
		ExpectedRenderPasses = InOther.ExpectedRenderPasses;
		ImageOutputData = MoveTemp(InOther.ImageOutputData);
	
		return *this;
	}
	FMoviePipelineMergerOutputFrame(FMoviePipelineMergerOutputFrame&& InOther)
		: FrameOutputState(InOther.FrameOutputState)
		, ExpectedRenderPasses(InOther.ExpectedRenderPasses)
		, ImageOutputData(MoveTemp(InOther.ImageOutputData))
	{
	}

private:
	// Explicitly delete copy operators since we own unique data.
	// void operator=(const FMoviePipelineMergerOutputFrame&);
	// FMoviePipelineMergerOutputFrame(const FMoviePipelineMergerOutputFrame&);

public:
	FMoviePipelineFrameOutputState FrameOutputState;

	TArray<FMoviePipelinePassIdentifier> ExpectedRenderPasses;

	TMap<FMoviePipelinePassIdentifier, TUniquePtr<FImagePixelData>> ImageOutputData;
};

namespace MoviePipeline
{
	struct FAudioState
	{
		struct FAudioSegment
		{
			FMoviePipelineFrameOutputState OutputState;

			float NumChannels;
			float SampleRate;

			Audio::AlignedFloatBuffer SegmentData;
		};

		FAudioState()
			: bIsRecordingAudio(false)
			, PrevUnfocusedAudioMultiplier(1.f)
			, PrevRenderEveryTickValue(1)
		{}

		bool bIsRecordingAudio;

		float PrevUnfocusedAudioMultiplier;
		int32 PrevRenderEveryTickValue;

		/** Float Buffers for Movie Segments we've finished rendering. Stored until shutdown. Fully available during Finalize stage. */
		TArray<FAudioSegment> FinishedSegments;

		/** An array of active submixes we are recording for this shot. Gets cleared when recording stops on a shot. */
		TArray<TWeakPtr<Audio::FMixerSubmix, ESPMode::ThreadSafe>> ActiveSubmixes;
	};
}