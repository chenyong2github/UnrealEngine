// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicVector.h"

#include "Async/ParallelFor.h"

/*
 * Basic cache of per-triangle information for a mesh
 */
struct FMeshTriInfoCache
{
	TDynamicVector<FVector3d> Centroids;
	TDynamicVector<FVector3d> Normals;
	TDynamicVector<double> Areas;

	void GetTriInfo(int TriangleID, FVector3d& NormalOut, double& AreaOut, FVector3d& CentroidOut) const
	{
		NormalOut = Normals[TriangleID];
		AreaOut = Areas[TriangleID];
		CentroidOut = Centroids[TriangleID];
	}

	template<class TriangleMeshType>
	static FMeshTriInfoCache BuildTriInfoCache(const TriangleMeshType& Mesh)
	{
		FMeshTriInfoCache Cache;
		int NT = Mesh.TriangleCount();
		Cache.Centroids.Resize(NT);
		Cache.Normals.Resize(NT);
		Cache.Areas.Resize(NT);

		ParallelFor(NT, [&](int TriIdx)
		{
			TMeshQueries<TriangleMeshType>::GetTriNormalAreaCentroid(Mesh, TriIdx, Cache.Normals[TriIdx], Cache.Areas[TriIdx], Cache.Centroids[TriIdx]);
		});

		return Cache;
	}
};

