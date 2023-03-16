// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "MovieGraphDataTypes.generated.h"

// Forward Declares
class UMovieGraphPipeline;
class UMovieGraphTimeStepBase;
class UMovieGraphRendererBase;
class UMovieGraphTimeRangeBuilderBase;
class UMovieGraphDataCachingBase;
class UMoviePipelineExecutorJob;
class UMoviePipelineExecutorShot;

USTRUCT(BlueprintType)
struct MOVIERENDERPIPELINECORE_API FMovieGraphInitConfig
{
	GENERATED_BODY()
	
	FMovieGraphInitConfig();
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Graph")
	TSubclassOf<UMovieGraphTimeStepBase> TimeStepClass;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Graph")
	TSubclassOf<UMovieGraphRendererBase> RendererClass;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Graph")
	TSubclassOf<UMovieGraphTimeRangeBuilderBase> TimeRangeBuilderClass;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Graph")
	TSubclassOf<UMovieGraphDataCachingBase> DataCachingClass;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Graph")
	bool bRenderViewport;
};


USTRUCT(BlueprintType)
struct MOVIERENDERPIPELINECORE_API FMovieGraphTimeStepData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Graph")
	int32 OutputFrameNumber;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Graph")
	float FrameDeltaTime;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Graph")
	float WorldTimeDilation;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Graph")
	float WorldSeconds;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Graph")
	float MotionBlurFraction;
};

UCLASS(BlueprintType, Abstract)
class MOVIERENDERPIPELINECORE_API UMovieGraphTimeStepBase : public UObject
{
	GENERATED_BODY()
public:
	virtual void TickProducingFrames() {}
	virtual FMovieGraphTimeStepData GetCalculatedTimeData() const { return FMovieGraphTimeStepData(); }
	UMovieGraphPipeline* GetOwningGraph() const;
};

UCLASS(BlueprintType, Abstract)
class MOVIERENDERPIPELINECORE_API UMovieGraphRendererBase : public UObject
{
	GENERATED_BODY()
public:
	virtual void Render(const FMovieGraphTimeStepData& InTimeData) {}
	virtual void SetupRenderingPipelineForShot(UMoviePipelineExecutorShot* InShot) {}
	virtual void TeardownRenderingPipelineForShot(UMoviePipelineExecutorShot* InShot) {}
	UMovieGraphPipeline* GetOwningGraph() const;
};

UCLASS(BlueprintType, Abstract)
class MOVIERENDERPIPELINECORE_API UMovieGraphTimeRangeBuilderBase : public UObject
{
	GENERATED_BODY()
public:
	virtual void BuildTimeRanges() { }
	UMovieGraphPipeline* GetOwningGraph() const;
};

UCLASS(BlueprintType, Abstract)
class MOVIERENDERPIPELINECORE_API UMovieGraphDataCachingBase : public UObject
{
	GENERATED_BODY()
public:
	virtual void CacheDataPreJob(const FMovieGraphInitConfig& InInitConfig) {}
	virtual void RestoreCachedDataPostJob() {}
	virtual void UpdateShotList() {}
	virtual void InitializeShot(UMoviePipelineExecutorShot* InShot) {}
	UMovieGraphPipeline* GetOwningGraph() const;
};

struct FMovieGraphRenderPassLayerData
{
	FString LayerName;
};

struct FMovieGraphRenderPassSetupData
{
	TWeakObjectPtr<class UMovieGraphDefaultRenderer> Renderer;
	TArray<FMovieGraphRenderPassLayerData> Layers;
};

USTRUCT(BlueprintType)
struct FMovieGraphRenderDataIdentifier
{
	GENERATED_BODY()

	FMovieGraphRenderDataIdentifier()
	{}

	FMovieGraphRenderDataIdentifier(const FString& InRenderLayerName, const FString& InRendererName,
		const FString& InSubRenderResourceName, const FString& InCameraName)
		: RenderLayerName(InRenderLayerName)
		, RendererName(InRendererName)
		, SubResourceName(InSubRenderResourceName)
		, CameraName(InCameraName)
	{
	}

	bool operator == (const FMovieGraphRenderDataIdentifier& InRHS) const
	{
		return RenderLayerName == InRHS.RenderLayerName && 
			RendererName == InRHS.RendererName &&
			SubResourceName == InRHS.SubResourceName &&
			CameraName == InRHS.CameraName;
	}

	bool operator != (const FMovieGraphRenderDataIdentifier& InRHS) const
	{
		return !(*this == InRHS);
	}

	friend uint32 GetTypeHash(FMovieGraphRenderDataIdentifier OutputState)
	{
		return HashCombineFast(GetTypeHash(OutputState.RenderLayerName),
			HashCombineFast(GetTypeHash(OutputState.RendererName),
				HashCombineFast(GetTypeHash(OutputState.SubResourceName),
					GetTypeHash(OutputState.CameraName))));
	}

public:
	// The user provided name for the whole Render Layer ("character", "background", etc.)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	FString RenderLayerName;

	// Which renderer was used to produce this image ("panoramic" "deferred" "path tracer", etc.)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	FString RendererName;

	// A sub-resource name for the renderer (ie: "beauty", "object id", "depth", etc.)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	FString SubResourceName;

	// The name of the camera being used for this render.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	FString CameraName;
};


namespace UE
{
namespace MovieGraph
{
	struct FMovieGraphSampleState
	{
		// We need to capture the state of MRQ at the time a sample was rendered so that we can
		// resolve filenames, etc. This is partially:
		// Output Frame Data (which output frame, temporal sample, spatial sample)
		// Traversal Context (layer name, current shot, etc.)

		// The name of the render resource this state was captured for
		FMovieGraphRenderDataIdentifier RenderDataIdentifier;

		// The backbuffer resolution of this render resource (ie: the resolution the sample was rendered at)
		FIntPoint BackbufferResolution;

		// The time data (output frame, delta times, etc.)
		FMovieGraphTimeStepData Time;

	};

}
}
