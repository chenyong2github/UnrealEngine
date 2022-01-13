// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/LargeWorldRenderPosition.h"
#include "EngineDefines.h"

#if !UE_LARGE_WORLD_COORDINATES_DISABLED

#define UE_LWC_RENDER_TILE_SIZE_MIN (262144.0)	// LWC_TODO: Make this smaller?
#define UE_LWC_RENDER_TILE_SIZE (FMath::Max(UE_LWC_RENDER_TILE_SIZE_MIN,  HALF_WORLD_MAX / 16777216.0))	// LWC_TODO: Tweak render tile divisor to maximise range.
static_assert(UE_LWC_RENDER_TILE_SIZE <= 1048576.0, "Current WORLD_MAX is too large and is likely to adversely affect world space coordinate precision within shaders!");	// LWC_TODO: Set this to something reasonable.

//#define UE_LWC_RENDER_TILE_SIZE (262144.0)
//#define UE_LWC_RENDER_TILE_SIZE (1048576.0)
//#define UE_LWC_RENDER_TILE_SIZE (2097152.0)

// This is the max size we allow for LWC offsets relative to the tile
// Value chosen to ensure sufficient precision when stored in single precision float
// Normally offsets should be within +/-TileSizeDivideBy2, but we often rebase multiple quantities off a single tile origin
#define UE_LWC_RENDER_MAX_OFFSET (2097152.0)

static FMatrix44f SafeCastMatrix(const FMatrix& Matrix)
{
	const FVector Origin = Matrix.GetOrigin();
	const double OriginX = FMath::Abs(Origin.X);
	const double OriginY = FMath::Abs(Origin.Y);
	const double OriginZ = FMath::Abs(Origin.Z);
	if (OriginX > UE_LWC_RENDER_MAX_OFFSET || OriginY > UE_LWC_RENDER_MAX_OFFSET || OriginZ > UE_LWC_RENDER_MAX_OFFSET)
	{
		ensure(false);
	}

	return FMatrix44f(Matrix);
}

double FLargeWorldRenderScalar::GetTileSize()
{
	return UE_LWC_RENDER_TILE_SIZE;
}

FVector3f FLargeWorldRenderScalar::GetTileFor(FVector InPosition)
{
	if constexpr (UE_LWC_RENDER_TILE_SIZE == 0)
	{
		return FVector3f::ZeroVector;
	}
	FVector3f LWCTile = InPosition / UE_LWC_RENDER_TILE_SIZE + 0.5;

	// normalize the tile
	LWCTile.X = FMath::FloorToFloat(LWCTile.X);
	LWCTile.Y = FMath::FloorToFloat(LWCTile.Y);
	LWCTile.Z = FMath::FloorToFloat(LWCTile.Z);

	return LWCTile;
}

FMatrix44f FLargeWorldRenderPosition::MakeToRelativeWorldMatrix(const FMatrix& ToWorld) const
{
	return SafeCastMatrix(ToWorld * FTranslationMatrix(-GetTileOffset()));
}

FMatrix44f FLargeWorldRenderPosition::MakeClampedToRelativeWorldMatrix(const FMatrix& ToWorld) const
{
	// Clamp the relative matrix, avoid allowing the relative translation to get too far away from the tile origin
	const FVector RelativeOrigin = ToWorld.GetOrigin() - GetTileOffset();
	FVector ClampedRelativeOrigin = RelativeOrigin;
	ClampedRelativeOrigin.X = FMath::Clamp(ClampedRelativeOrigin.X, -UE_LWC_RENDER_MAX_OFFSET, UE_LWC_RENDER_MAX_OFFSET);
	ClampedRelativeOrigin.Y = FMath::Clamp(ClampedRelativeOrigin.Y, -UE_LWC_RENDER_MAX_OFFSET, UE_LWC_RENDER_MAX_OFFSET);
	ClampedRelativeOrigin.Z = FMath::Clamp(ClampedRelativeOrigin.Z, -UE_LWC_RENDER_MAX_OFFSET, UE_LWC_RENDER_MAX_OFFSET);

	FMatrix ClampedToRelativeWorld(ToWorld);
	ClampedToRelativeWorld.SetOrigin(ClampedRelativeOrigin);
	return FMatrix44f(ClampedToRelativeWorld);
}

FMatrix44f FLargeWorldRenderPosition::MakeFromRelativeWorldMatrix(const FMatrix& FromWorld) const
{
	return SafeCastMatrix(FTranslationMatrix(GetTileOffset()) * FromWorld);
}

void FLargeWorldRenderScalar::Validate(double InAbsolute)
{
	const double Tolerance = 0.01; // TODO LWC - How precise do we need to be?
	const double CheckAbsolute = GetAbsolute();
	const double Delta = FMath::Abs(CheckAbsolute - InAbsolute);

	ensureMsgf(Delta < Tolerance, TEXT("Bad FLargeWorldRenderScalar (%g) vs (%g)"),
		InAbsolute, CheckAbsolute);
}

#endif // !UE_LARGE_WORLD_COORDINATES_DISABLED
