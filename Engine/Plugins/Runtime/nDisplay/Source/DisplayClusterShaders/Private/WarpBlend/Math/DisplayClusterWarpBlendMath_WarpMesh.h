// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "StaticMeshResources.h"


class FDisplayClusterWarpBlendMath_WarpMesh
{
public:
	FDisplayClusterWarpBlendMath_WarpMesh(const FStaticMeshLODResources& InMeshLODResources)
		: MeshLODResources(InMeshLODResources)
	{ }

public:
	FBox CalcAABBox()
	{
		FBox AABBox = FBox(FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX));

		const FPositionVertexBuffer& VertexPosition = MeshLODResources.VertexBuffers.PositionVertexBuffer;
		for (uint32 VertIdx = 0; VertIdx < VertexPosition.GetNumVertices(); ++VertIdx)
		{
			const FVector Pts = VertexPosition.VertexPosition(VertIdx);

			AABBox.Min.X = FMath::Min(AABBox.Min.X, Pts.X);
			AABBox.Min.Y = FMath::Min(AABBox.Min.Y, Pts.Y);
			AABBox.Min.Z = FMath::Min(AABBox.Min.Z, Pts.Z);

			AABBox.Max.X = FMath::Max(AABBox.Max.X, Pts.X);
			AABBox.Max.Y = FMath::Max(AABBox.Max.Y, Pts.Y);
			AABBox.Max.Z = FMath::Max(AABBox.Max.Z, Pts.Z);
		}

		return AABBox;
	}

	void CalcSurfaceVectors(FVector& OutSurfaceViewNormal, FVector& OutSurfaceViewPlane)
	{
		// Calc static normal and plane
		const int32 IdxNum = MeshLODResources.IndexBuffer.GetNumIndices();
		const int32 TriNum = IdxNum / 3;

		if (TriNum <= 0)
		{
			return;
		}

		double Nxyz[3] = { 0, 0, 0 };

		const FPositionVertexBuffer& VertexPosition = MeshLODResources.VertexBuffers.PositionVertexBuffer;
		for (int32 TriIdx = 0; TriIdx < TriNum; ++TriIdx)
		{
			const int32 Index0 = MeshLODResources.IndexBuffer.GetIndex(TriIdx * 3 + 0);
			const int32 Index1 = MeshLODResources.IndexBuffer.GetIndex(TriIdx * 3 + 1);
			const int32 Index2 = MeshLODResources.IndexBuffer.GetIndex(TriIdx * 3 + 2);

			const FVector& Pts1 = VertexPosition.VertexPosition(Index0);
			const FVector& Pts0 = VertexPosition.VertexPosition(Index1);
			const FVector& Pts2 = VertexPosition.VertexPosition(Index2);

			const FVector N1 = Pts1 - Pts0;
			const FVector N2 = Pts2 - Pts0;
			const FVector N = FVector::CrossProduct(N2, N1).GetSafeNormal();

			for (int32 j = 0; j < 3; j++)
			{
				Nxyz[j] += N[j];
			}
		}

		double Scale = double(1) / TriNum;
		for (int32 i = 0; i < 3; i++)
		{
			Nxyz[i] *= Scale;
		}

		OutSurfaceViewNormal = FVector(Nxyz[0], Nxyz[1], Nxyz[2]).GetSafeNormal();

		//@todo: MeshSurfaceViewPlane not implemented, use MeshSurfaceViewNormal
		OutSurfaceViewPlane = OutSurfaceViewNormal;
	}

private:
	const FStaticMeshLODResources& MeshLODResources;
};
