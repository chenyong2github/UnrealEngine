// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Mesh.h"
#include "MuR/Image.h"

namespace mu
{
	class Image;
	struct FProjector;
	template <int NUM_INTERPOLATORS> class RasterVertex;

    struct SCRATCH_IMAGE_PROJECT
    {
        TArray< RasterVertex<4> > vertices;
		TArray<uint8> culledVertex;
    };

    extern void ImageRasterProjectedPlanar( const Mesh* pMesh, Image* pTargetImage,
		const Image* pSource, const Image* pMask,
		bool bIsRGBFadingEnabled, bool bIsAlphaFadingEnabled,
		ESamplingMethod SamplingMethod,
		float fadeStart, float fadeEnd,
		int layout, int block,
		UE::Math::TIntVector2<uint16> CropMin, UE::Math::TIntVector2<uint16> UncroppedSize,
		SCRATCH_IMAGE_PROJECT* scratch );

    extern void ImageRasterProjectedCylindrical( const Mesh* pMesh, Image* pTargetImage,
		const Image* pSource, const Image* pMask,
		bool bIsRGBFadingEnabled, bool bIsAlphaFadingEnabled,
		float fadeStart, float fadeEnd,
		int layout,
		float projectionAngle,
		UE::Math::TIntVector2<uint16> CropMin, UE::Math::TIntVector2<uint16> UncroppedSize,
		SCRATCH_IMAGE_PROJECT* scratch );

    extern void ImageRasterProjectedWrapping( const Mesh* pMesh, Image* pTargetImage,
		const Image* pSource, const Image* pMask,
		bool bIsRGBFadingEnabled, bool bIsAlphaFadingEnabled,
		ESamplingMethod SamplingMethod,
		float fadeStart, float fadeEnd,
		int layout, int block,
		UE::Math::TIntVector2<uint16> CropMin, UE::Math::TIntVector2<uint16> UncroppedSize,
		SCRATCH_IMAGE_PROJECT* scratch );

	extern int32 ComputeProjectedFootprintBestMip(
			const Mesh* pMesh, const FProjector& Projector, const FVector2f& TargetSize, const FVector2f& SourceSize);

    extern MeshPtr MeshProject( const Mesh* pMesh,
                                const FProjector& projector );

	MUTABLERUNTIME_API extern MeshPtr CreateMeshOptimisedForProjection( int layout );
	MUTABLERUNTIME_API extern MeshPtr CreateMeshOptimisedForWrappingProjection( int layout );

}
