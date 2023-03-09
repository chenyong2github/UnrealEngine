// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Graph/MovieGraphDataTypes.h"
#include "PixelFormat.h"
#include "MovieGraphDefaultRenderer.generated.h"

// Forward Declares
class UMovieGraphRenderPassNode;
class UTextureRenderTarget2D;

/**
* This class 
*/
UCLASS(BlueprintType)
class MOVIERENDERPIPELINECORE_API UMovieGraphDefaultRenderer : public UMovieGraphRendererBase
{
	GENERATED_BODY()

public:
	struct FRenderTargetInitParams
	{
		FRenderTargetInitParams()
			: Size(FIntPoint(0, 0))
			, TargetGamma(0.f)
			, PixelFormat(EPixelFormat::PF_Unknown)
		{
		}

		FIntPoint Size;
		float TargetGamma;
		EPixelFormat PixelFormat;

		bool operator == (const FRenderTargetInitParams& InRHS) const
		{
			return Size == InRHS.Size && TargetGamma == InRHS.TargetGamma && PixelFormat == InRHS.PixelFormat;
		}

		bool operator != (const FRenderTargetInitParams& InRHS) const
		{
			return !(*this == InRHS);
		}

		friend uint32 GetTypeHash(FRenderTargetInitParams Params)
		{
			return HashCombineFast(GetTypeHash(Params.Size), HashCombineFast(GetTypeHash(Params.TargetGamma), GetTypeHash(Params.PixelFormat)));
		}
	};

public:
	virtual void Render(const FMovieGraphTimeStepData& InTimeData) override;
	virtual void SetupRenderingPipelineForShot(UMoviePipelineExecutorShot* InShot) override;
	virtual void TeardownRenderingPipelineForShot(UMoviePipelineExecutorShot* InShot) override;

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	UTextureRenderTarget2D* GetOrCreateViewRenderTarget(const FRenderTargetInitParams& InInitParams);

protected:
	TObjectPtr<UTextureRenderTarget2D> CreateViewRenderTarget(const FRenderTargetInitParams& InInitParams) const;

protected:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMovieGraphRenderPassNode>> RenderPassesInUse;

	TMap<FRenderTargetInitParams, TObjectPtr<UTextureRenderTarget2D>> PooledViewRenderTargets;
};