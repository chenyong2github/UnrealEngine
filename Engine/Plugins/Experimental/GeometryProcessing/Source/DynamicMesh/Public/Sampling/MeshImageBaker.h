// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sampling/MeshImageBakingCache.h"
#include "Image/ImageBuilder.h"

namespace UE
{
namespace Geometry
{

class FMeshMapBaker;

class DYNAMICMESH_API FMeshImageBaker
{
public:
	virtual ~FMeshImageBaker() {}

	//
	// Old Baker interface
	//
	void SetCache(const FMeshImageBakingCache* CacheIn)
	{
		ImageBakeCache = CacheIn;
	}

	const FMeshImageBakingCache* GetCache() const
	{
		return ImageBakeCache;
	}

	virtual void Bake()
	{
	}

protected:
	const FMeshImageBakingCache* ImageBakeCache;

public:
	//
	// New Baker interface
	//
	struct FCorrespondenceSample
	{
		FMeshUVSampleInfo BaseSample;
		FVector3d BaseNormal;

		int32 DetailTriID;
		FVector3d DetailBaryCoords;
	};

	/** Invoked at start of bake to initialize baker. */
	virtual void PreEvaluate(const FMeshMapBaker& Baker) {}

	/** Evaluate the sample at this mesh correspondence point. */
	virtual FVector4f EvaluateSample(const FMeshMapBaker& Baker, const FCorrespondenceSample& Sample) { return DefaultSample(); }

	/** Invoked at the end of the bake to finalize the bake result. */
	virtual void PostEvaluate(const FMeshMapBaker& Baker, TImageBuilder<FVector4f>& Result) {}

	/** @return the default sample value for the baker. */
	virtual FVector4f DefaultSample() const { return FVector4f::Zero(); }

	/** @return true if this baker supports multisampling. */
	virtual bool SupportsMultisampling() const { return true; }
};

} // end namespace UE::Geometry
} // end namespace UE
