// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once


#include "CoreMinimal.h"

#include "ImagePixelData.h"

/* Types
 *****************************************************************************/

/**
 * Structure for a single overlapped image.
 */
struct MOVIERENDERPIPELINECORE_API FImageOverlappedPlane
{
public:
	/**
	 * Default constructor.
	 */
	FImageOverlappedPlane()
		: SizeX(0)
		, SizeY(0)
	{
	}

	/**
	 * Initialize the memory. Before using the memory, we also will need a call to ZeroPlane.
	 *
	 * @param InSizeX - Horizontal size of the tile.
	 * @param InSizeY - Vertical size of the tile.
	 */
	void Init(int32 InSizeX, int32 InSizeY);

	/**
	 * Zeroes the accumulation. Assumes the data is already allocated.
	 */
	void ZeroPlane();

	/**
	 * Frees the memory and resets the sizes
	 */
	void Reset();

	/**
	 * Accumulate a single tile to this plane. The raw data will in general be smaller than the full plane.
	 * Addtionally, we are only going to be using part of the input plane because the input plane will
	 * have unused border areas. The SubRect parameters describe the area inside the source plane.
	 *
	 * @param InRawData - Raw data to accumulate
	 * @param InRawSizeX - Width of the tile. Must exactly match.
	 * @param InRawSizeY - Height of the tile. Must exactly match.
	 * @param InSampleOffsetX - Pixel offset of the tile (in X)
	 * @param InSampleOffsetY - Pixel offset of the tile (in Y)
	 */
	void AccumulateSinglePlane(const TArray64<float>& InRawData, const TArray64<float>& InWeightData, int32 InSizeX, int32 InSizeY, int32 InOffsetX, int32 InOffsetY,
								float SubpixelOffsetX, float SubpixelOffsetY,
								int32 SubRectOffsetX, int32 SubRectOffsetY,
								int32 SubRectSizeX, int32 SubRectSizeY);

	/** Actual channel data*/
	TArray64<float> ChannelData;

	/** Width of the image. */
	int32 SizeX;

	/** Height of the image. */
	int32 SizeY;
};

/**
 * Contains all the image planes for the tiles.
 */
struct MOVIERENDERPIPELINECORE_API FImageOverlappedAccumulator
{
public:
	/**
	 * Default constructor.
	 */
	FImageOverlappedAccumulator()
		: PlaneSizeX(0)
		, PlaneSizeY(0)
		, NumChannels(0)
		, AccumulationGamma(1.0f)
		, TileMaskSizeX(0)
		, TileMaskSizeY(0)
	{
	}

	/**
	 * Allocates memory.
	 *
	 * @param InTileSizeX - Horizontal tile size.
	 * @param InTileSizeY - Vertical tile size.
	 * @param InNumTilesX - Num horizontal tiles.
	 * @param InNumTilesY - Num vertical tiles.
	 * @param InNumChannels - Num Channels.
	 */
	void InitMemory(int InPlaneSizeX, int InPlaneSizeY, int InNumChannels);

	/**
	 * Initializes memory.
	 *
	 * Resets the memory to 0s so that we can start a new frame.
	 */
	void ZeroPlanes();

	/**
	 * Resets the memory.
	 */
	void Reset();


	static void GenerateTileWeight(TArray64<float>& Weights, int32 SizeX, int32 SizeY);

	static void GetTileSubRect(int32 & SubRectOffsetX, int32 & SubRectOffsetY, int32 & SubRectSizeX, int32 & SubRectSizeY, const TArray64<float>& Weights, const int32 SizeX, const int32 SizeY);

	static void CheckTileSubRect(const TArray64<float>& Weights, const int32 SizeX, const int32 SizeY, int32 SubRectOffsetX, int32 SubRectOffsetY, int32 SubRectSizeX, int32 SubRectSizeY);

	/**
	 * Given a rendered tile, accumulate the data to the full size image.
	 * 
	 * @param InPixelData - Raw pixel data.
	 * @param InTileX - Tile index in X.
	 * @param InTileY - Tile index in Y.
	 * @param InSubPixelOffset - SubPixel offset, should be in the range [0,1)
	 */
	void AccumulatePixelData(const FImagePixelData& InPixelData, int32 InTileOffsetX, int32 InTileOffsetY, FVector2D InSubpixelOffset);

	/**
	 * After accumulation is finished, fetch the final image as bytes. In theory we don't need this, because we could
	 * just fetch as LinearColor and convert to bytes. But the largest size asked for is 45k x 22.5k, which is 1B pixels.
	 * So fetching as LinearColor would create a 16GB intermediary image, so it's worth having an option to fetch
	 * straight to FColors.
	 * 
	 * @param FImagePixelData - Finished pixel data.
	 */
	void FetchFinalPixelDataByte(TArray<FColor>& OutPixelData) const;

	/**
	 * After accumulation is finished, fetch the final image as linear colors
	 * 
	 * @param FImagePixelData - Finished pixel data.
	 */
	void FetchFinalPixelDataLinearColor(TArray<FLinearColor>& OutPixelData) const;

	/**
	 * Grab a single pixel from the full res tile and scale it by the appropriate Scale value.
	 * 
	 * @param Rgba - Found pixel value.
	 * @param PlaneScale - The scales of each channel plane.
	 * @param FullX - X position of the full res image.
	 * @param FullY - Y position of the full res image.
	 */
	void FetchFullImageValue(float Rgba[4], int32 FullX, int32 FullY) const;

public:
	/** Width of each tile in pixels */
	int32 PlaneSizeX;

	/** Height of each tile in pixels */
	int32 PlaneSizeY;

	/** Number of channels in the tiles. Typical will be 3 (RGB). */
	int32 NumChannels;

	/** Gamma for accumulation. Typical values are 1.0 and 2.2. */
	float AccumulationGamma;

	TArray64<FImageOverlappedPlane> ChannelPlanes;
	
	FImageOverlappedPlane WeightPlane;

	/** Weights image. Recalculate if it changes. */
	TArray64<float> TileMaskData;

	/** Width of weight image. */
	int32 TileMaskSizeX;

	/** Height of weight image. */
	int32 TileMaskSizeY;
};


