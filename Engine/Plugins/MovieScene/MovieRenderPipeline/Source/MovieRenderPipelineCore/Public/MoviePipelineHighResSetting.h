// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineSetting.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MoviePipelineHighResSetting.generated.h"

UCLASS(Blueprintable)
class MOVIERENDERPIPELINECORE_API UMoviePipelineHighResSetting : public UMoviePipelineSetting
{
	GENERATED_BODY()
public:
	UMoviePipelineHighResSetting()
		: TileCount(1)
		, TextureSharpnessBias(0.f)
		, OverlapRatio(0.f)
		, bWriteAllSamples(false)
	{
	}
	
	FIntPoint CalculatePaddedBackbufferSize(const FIntPoint& InTileSize)
	{
		int32 OverlappedPadX = FMath::CeilToInt(InTileSize.X * OverlapRatio);
		int32 OverlappedPadY = FMath::CeilToInt(InTileSize.Y * OverlapRatio);

		return InTileSize + FIntPoint(2 * OverlappedPadX, 2 * OverlappedPadY);
	}

public:
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "HighResSettingDisplayName", "High Resolution"); }
#endif
	virtual bool IsValidOnShots() const override { return true; }
	virtual bool IsValidOnMaster() const override { return true; }
	virtual void ValidateStateImpl() override
	{
		Super::ValidateStateImpl();

		if (TileCount > 1)
		{
			ValidationResults.Add(NSLOCTEXT("MovieRenderPipeline", "HighRes_UnsupportedFeatures", "Tiling does not support TAA or Auto Exposure. These will be forced off when rendering. Use Spatial/Temporal sampling and Manual Camera Exposure /w Exposure Compensation instead."));
			if (FMath::IsNearlyEqual(OverlapRatio, 0.f))
			{
				ValidationResults.Add(NSLOCTEXT("MovieRenderPipeline", "HighRes_OverlapNeeded", "Increase the overlap amount to avoid seams in the final image. More overlap results in longer renders so start at 0.1 and increase as needed."));
			}
			ValidationState = EMoviePipelineValidationState::Warnings;
		}
	}

	virtual void GetFilenameFormatArguments(FMoviePipelineFormatArgs& InOutFormatArgs) const override
	{
		InOutFormatArgs.Arguments.Add(TEXT("tile_count"), TileCount);
		InOutFormatArgs.Arguments.Add(TEXT("overlap_percent"), OverlapRatio);
	}

public:
	/**
	* How many tiles should the resulting movie render be broken into? A tile should be no larger than
	* the maximum texture resolution supported by your graphics card (likely 16k), so NumTiles should be
	* ceil(Width/MaxTextureSize). More tiles mean more individual passes over the whole scene at a smaller
	* resolution which may help with gpu timeouts. Requires at least 1 tile. Tiling is applied evenly to
	* both X and Y.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = 1, ClampMin = 1), Category = "Movie Pipeline")
	int32 TileCount;
	
	/**
	* This bias encourages the engine to use a higher detail texture when it would normally use a lower detail
	* texture due to the size of the texture on screen. A more negative number means overall sharper images
	* (up to the detail resolution of your texture). Too much sharpness will cause visual grain/noise in the
	* resulting image, but this can be mitigated with more spatial samples.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = -1, UIMax = 0), Category = "Movie Pipeline")
	float TextureSharpnessBias;
	
	/**
	* How much should each tile overlap each other (0-0.5). Decreasing the overlap will result in smaller individual
	* tiles (which means faster renders) but increases the likelyhood of edge-of-screen artifacts showing up which
	* will become visible in the final image as a "grid" of repeated problem areas.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = 0, ClampMin = 0, UIMax = 0.5, ClampMax=1), Category = "Movie Pipeline")
	float OverlapRatio;
	
	/**
	* If true, we will write all samples that get generated to disk individually. This can be useful for debugging or if you need to accumulate
	* render passes differently than provided.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Movie Pipeline")
	bool bWriteAllSamples;

};