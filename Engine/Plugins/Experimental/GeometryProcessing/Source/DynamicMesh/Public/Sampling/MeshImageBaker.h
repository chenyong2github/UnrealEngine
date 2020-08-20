// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sampling/MeshImageBakingCache.h"


class DYNAMICMESH_API FMeshImageBaker
{
public:
	virtual ~FMeshImageBaker() {}

	void SetCache(const FMeshImageBakingCache* CacheIn)
	{
		ImageBakeCache = CacheIn;
	}

	const FMeshImageBakingCache* GetCache() const
	{
		return ImageBakeCache;
	}


	virtual void Bake() = 0;


protected:

	const FMeshImageBakingCache* ImageBakeCache;

};