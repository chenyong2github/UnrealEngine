// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VectorTypes.h"
#include "IntVectorTypes.h"

namespace UE
{
namespace Geometry
{

/**
 * FImageDimensions provides various functions for working with size/indices/coordinates of a
 * 2D image, as well as standard UV spaces
 */
class FImageDimensions
{
protected:
	int32 Width = 0;
	int32 Height = 0;

	bool bIsTile = false;
	int32 SourceWidth = 0;
	int32 SourceHeight = 0;
	FVector2i SourceOffset = FVector2i::Zero();

public:
	FImageDimensions(int32 WidthIn = 0, int32 HeightIn = 0)
		: SourceWidth(WidthIn), SourceHeight(HeightIn)
	{
		SetDimensions(WidthIn, HeightIn);
	}

	/** Tile constructor. */
	FImageDimensions(int32 WidthIn, int32 HeightIn, int32 SourceWidthIn, int32 SourceHeightIn, FVector2i SourceOffsetIn)
		: bIsTile(true), SourceWidth(SourceWidthIn), SourceHeight(SourceHeightIn), SourceOffset(SourceOffsetIn)
	{
		SetDimensions(WidthIn, HeightIn);
		checkSlow(Width >= 0 && Width <= SourceWidth && SourceOffset.X >= 0 && Width + SourceOffset.X <= SourceWidth);
		checkSlow(Height >= 0 && Height <= SourceHeight && SourceOffset.Y >= 0 && Height + SourceOffset.Y <= SourceHeight);
	}

	/** Set the dimensions of this image. */
	void SetDimensions(int32 WidthIn, int32 HeightIn)
	{
		check(WidthIn >= 0 && HeightIn >= 0);
		Width = WidthIn;
		Height = HeightIn;
	}

	/** @return width of image */
	int32 GetWidth() const { return Width; }
	/** @return height of image */
	int32 GetHeight() const { return Height; }
	/** @return number of elements in image */
	int64 Num() const { return (int64)Width * (int64)Height; }

	/** @return true if image is square */
	bool IsSquare() const { return bIsTile ? SourceWidth == SourceHeight : Width == Height; }

	/** @return true if coordinates are valid, ie in-bounds of image dimensions */
	bool IsValidCoords(const FVector2i& Coords) const
	{
		return (Coords.X >= 0 && Coords.X < Width && Coords.Y >= 0 && Coords.Y < Height);
	}

	/** Clamp input coordinates to valid range of image coordinates */
	void Clamp(int32& X, int32& Y) const
	{
		X = FMath::Clamp(X, 0, Width - 1);
		Y = FMath::Clamp(Y, 0, Height - 1);
	}

	/** Clamp input coordinates to valid range of image coordinates */
	void Clamp(FVector2i& Coords) const
	{
		Coords.X = FMath::Clamp(Coords.X, 0, Width - 1);
		Coords.Y = FMath::Clamp(Coords.Y, 0, Height - 1);
	}

	/** @return linear index into image from 2D coordinates */
	int64 GetIndex(int32 X, int32 Y) const
	{
		return (Y * Width) + X;
	}

	/** @return linear index into image from 2D coordinates */
	int64 GetIndex(const FVector2i& Coords) const
	{
		checkSlow(IsValidCoords(Coords));
		return ((int64)Coords.Y * (int64)Width) + (int64)Coords.X;
	}

	/** @return linear index into image from 2D coordinates, optionally flipped around X and Y axes */
	int64 GetIndexMirrored(const FVector2i& Coords, bool bFlipX, bool bFlipY) const
	{
		checkSlow(IsValidCoords(Coords));
		int64 UseX = (bFlipX) ? (Width - 1 - Coords.X) : Coords.X;
		int64 UseY = (bFlipY) ? (Height - 1 - Coords.Y) : Coords.Y;
		return (UseY * (int64)Width) + UseX;
	}

	/** @return 2D image coordinates from linear index */
	FVector2i GetCoords(int64 LinearIndex) const
	{
		checkSlow(LinearIndex >= 0 && LinearIndex < Num());
		return FVector2i((int32)(LinearIndex % (int64)Width), (int32)(LinearIndex / (int64)Width));
	}

	/** @return Real-valued dimensions of a pixel/texel in the image, relative to default UV space [0..1]^2 */
	FVector2d GetTexelSize() const
	{
		return FVector2d(1.0 / (double)SourceWidth, 1.0 / (double)SourceHeight);
	}

	/** @return Real-valued position of given texel center in default UV-space [0..1]^2 */
	FVector2d GetTexelUV(const FVector2i& Coords) const
	{
		if (bIsTile)
		{
			return FVector2d(
				((double)SourceOffset.X + (double)Coords.X + 0.5) / (double)SourceWidth,
				((double)SourceOffset.Y + (double)Coords.Y + 0.5) / (double)SourceWidth);
		}
		else
		{
			return FVector2d(
				((double)Coords.X + 0.5) / (double)Width,
				((double)Coords.Y + 0.5) / (double)Height);
		}
	}

	/** @return Real-valued position of given texel center in default UV-space [0..1]^2 */
	FVector2d GetTexelUV(int64 LinearIndex) const
	{
		return GetTexelUV(GetCoords(LinearIndex));
	}

	/** @return integer XY coordinates for real-valued XY coordinates (ie texel that contains value, if texel origin is in bottom-left */
	FVector2i PixelToCoords(const FVector2d& PixelPosition) const
	{
		if (bIsTile)
		{
			int32 X = FMath::Clamp((int32)PixelPosition.X, 0, SourceWidth - 1);
			int32 Y = FMath::Clamp((int32)PixelPosition.Y, 0, SourceHeight - 1);
			return FVector2i(X, Y);
		}
		else
		{
			int32 X = FMath::Clamp((int32)PixelPosition.X, 0, Width - 1);
			int32 Y = FMath::Clamp((int32)PixelPosition.Y, 0, Height - 1);
			return FVector2i(X, Y);
		}
	}

	/** @return integer XY coordinates for UV coordinates, assuming default UV space [0..1]^2 */
	FVector2i UVToCoords(const FVector2d& UVPosition) const
	{
		if (bIsTile)
		{
			double PixelX = (UVPosition.X * (double)SourceWidth /*- 0.5*/);
			double PixelY = (UVPosition.Y * (double)SourceHeight /*- 0.5*/);
			return PixelToCoords(FVector2d(PixelX, PixelY));
		}
		else
		{
			double PixelX = (UVPosition.X * (double)Width /*- 0.5*/);
			double PixelY = (UVPosition.Y * (double)Height /*- 0.5*/);
			return PixelToCoords(FVector2d(PixelX, PixelY));
		}
	}

	bool operator==(const FImageDimensions& Other) const
	{
		return Width == Other.Width && Height == Other.Height &&
			bIsTile == Other.bIsTile &&
			SourceWidth == Other.SourceWidth &&
			SourceHeight == Other.SourceHeight &&
			SourceOffset == Other.SourceOffset;
	}

	bool operator!=(const FImageDimensions& Other) const
	{
		return !(*this == Other);
	}

	// Tiling

	/** @return true if this image is a tile. */
	bool IsTile() const
	{
		return bIsTile;
	}

	/** @return source image dimensions for this tile. */
	FImageDimensions GetSourceDimensions() const
	{
		return FImageDimensions(SourceWidth, SourceHeight);
	}

	/** @return source image linear index from tile coordinates. */
	int64 GetSourceIndex(const FVector2i& Coords) const
	{
		FVector2i SourceCoords = GetSourceCoords(Coords.X, Coords.Y);
		return GetSourceDimensions().GetIndex(SourceCoords);
	}

	/** @return source image coordinates from tile coordinates. */
	FVector2i GetSourceCoords(int32 TileX, int32 TileY) const
	{
		checkSlow(IsValidCoords(FVector2i(TileX, TileY)));
		return FVector2i(SourceOffset.X + TileX, SourceOffset.Y + TileY);
	}
};

class FImageTiling
{
private:
	FImageDimensions Dimensions;
	int32 TileWidth = 32;
	int32 TileHeight = 32;
	int32 TilePadding = 0;

public:
	FImageTiling(FImageDimensions DimensionsIn, int32 TileWidthIn, int32 TileHeightIn, int32 TilePaddingIn = 0)
	{
		SetTiling(DimensionsIn, TileWidthIn, TileHeightIn, TilePaddingIn);
	}

	void SetTiling(FImageDimensions DimensionsIn, int32 TileWidthIn, int32 TileHeightIn, int32 TilePaddingIn)
	{
		checkSlow(TileWidthIn >= 1 && TileHeightIn >= 1 && TilePaddingIn >= 0);
		Dimensions = DimensionsIn;
		TileWidth = FMath::Clamp(TileWidthIn, 1, Dimensions.GetWidth());
		TileHeight = FMath::Clamp(TileHeightIn, 1, Dimensions.GetHeight());
		TilePadding = TilePaddingIn < 0 ? 0 : TilePaddingIn;
	}

	int32 NumTilesX() const
	{
		return Dimensions.GetWidth() / TileWidth + (Dimensions.GetWidth() % TileWidth == 0 ? 0 : 1);
	}

	int32 NumTilesY() const
	{
		return Dimensions.GetHeight() / TileHeight + (Dimensions.GetHeight() % TileHeight == 0 ? 0 : 1);
	}

	int32 Num() const
	{
		return NumTilesX() * NumTilesY();
	}

	/** @return the tile corresponding to the linear index [0, NumTiles()-1]. */
	FImageDimensions GetTile(int32 LinearTileIndex) const
	{
		FVector2i TileIndex(LinearTileIndex % NumTilesX(), LinearTileIndex / NumTilesX());
		FVector2i TileOffset(TileIndex.X * TileWidth, TileIndex.Y * TileHeight);
		int32 ClampedWidth = FMath::Clamp(TileWidth, 0, Dimensions.GetWidth() - TileIndex.X * TileWidth);
		int32 ClampedHeight = FMath::Clamp(TileHeight, 0, Dimensions.GetHeight() - TileIndex.Y * TileHeight);
		// TODO: Padding support
		return FImageDimensions(ClampedWidth, ClampedHeight, Dimensions.GetWidth(), Dimensions.GetHeight(), TileOffset);
	}
};

} // end namespace UE::Geometry
} // end namespace UE


