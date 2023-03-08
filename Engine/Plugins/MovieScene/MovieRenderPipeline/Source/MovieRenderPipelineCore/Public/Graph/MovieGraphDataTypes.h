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
};

UCLASS(BlueprintType, Abstract)
class MOVIERENDERPIPELINECORE_API UMovieGraphTimeStepBase : public UObject
{
	GENERATED_BODY()
public:
	virtual void CalculateTimeStep(FMovieGraphTimeStepData& OutTimeData)
	{}
	virtual void ResetPerTickData() {}
	virtual void InitializeShot(UMoviePipelineExecutorShot* InShot, FMovieGraphTimeStepData& OutTimeStepData) {}

protected:
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


protected:
	UMovieGraphPipeline* GetOwningGraph() const;
};

UCLASS(BlueprintType, Abstract)
class MOVIERENDERPIPELINECORE_API UMovieGraphTimeRangeBuilderBase : public UObject
{
	GENERATED_BODY()
public:
	virtual void BuildTimeRanges()
	{

	}


protected:
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
	virtual void InitializeShot(UMoviePipelineExecutorShot* InShot, const FMovieGraphTimeStepData& InTimeStepData) {}

protected:
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