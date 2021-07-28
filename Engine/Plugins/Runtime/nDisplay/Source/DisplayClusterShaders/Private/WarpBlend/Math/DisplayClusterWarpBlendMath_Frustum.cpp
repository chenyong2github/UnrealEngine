// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WarpBlend/Math/DisplayClusterWarpBlendMath_Frustum.h"

#include "Render/IDisplayClusterRenderTexture.h"
#include "Render/Containers/DisplayClusterRender_MeshComponent.h"

#include "Misc/DisplayClusterHelpers.h"

#include "Render/Viewport/IDisplayClusterViewport.h"

#include "StaticMeshResources.h"

FMatrix GetProjectionMatrixAssymetric(IDisplayClusterViewport* InViewport, const uint32 InContextNum, const FDisplayClusterWarpEye& InEye, const FDisplayClusterWarpContext& InContext)
{
	const float Left   = InContext.ProjectionAngles.Left;
	const float Right  = InContext.ProjectionAngles.Right;
	const float Top    = InContext.ProjectionAngles.Top;
	const float Bottom = InContext.ProjectionAngles.Bottom;

	InViewport->CalculateProjectionMatrix(InContextNum, Left, Right, Top, Bottom, InEye.ZNear, InEye.ZFar, false);

	return InViewport->GetContexts()[InContextNum].ProjectionMatrix;
}

void FDisplayClusterWarpBlendMath_Frustum::ImplBuildFrustum(IDisplayClusterViewport* InViewport, const uint32 InContextNum)
{
	// These matrices were copied from LocalPlayer.cpp.
	// They change the coordinate system from the Unreal "Game" coordinate system to the Unreal "Render" coordinate system
	static const FMatrix Game2Render(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));

	static const FMatrix Render2Game = Game2Render.Inverse();

	// Compute warp projection matrix
	Frustum.OutCameraRotation = Local2World.Rotator();
	Frustum.OutCameraOrigin = Local2World.GetOrigin();

	Frustum.TextureMatrix = ImplGetTextureMatrix();
	Frustum.RegionMatrix = GeometryContext.RegionMatrix;

	Frustum.ProjectionMatrix = GetProjectionMatrixAssymetric(InViewport, InContextNum, Eye, Frustum);

	Frustum.UVMatrix = World2Local * Game2Render * Frustum.ProjectionMatrix;

	Frustum.MeshToStageMatrix = GeometryContext.GeometryToOrigin;
}


bool FDisplayClusterWarpBlendMath_Frustum::ImplCalcFrustum_FULL_WarpMap()
{
	if(GeometryContext.GeometryProxy.WarpMap == nullptr)
	{
		return false;
	}

	bool bAllPointsInFrustum = true;

	int PointsAmmount = GeometryContext.GeometryProxy.WarpMap->GetTotalPoints();
	FVector4* PointsSouce = (FVector4*)(GeometryContext.GeometryProxy.WarpMap->GetData());

	// Search a camera space frustum
	for (int i = 0; i < PointsAmmount; ++i)
	{
		if (GetProjectionClip(PointsSouce[i]) == false)
		{
			bAllPointsInFrustum = false;
		}
	}

	return bAllPointsInFrustum;
}

bool FDisplayClusterWarpBlendMath_Frustum::ImplCalcFrustum_FULL_WarpMesh()
{
	if (!GeometryContext.GeometryProxy.WarpMesh)
	{
		return false;
	}

	bool bAllPointsInFrustum = true;

	const FStaticMeshLODResources* WarpMeshResource = GeometryContext.GeometryProxy.WarpMesh->GetStaticMeshLODResource();
	if (WarpMeshResource == nullptr)
	{
		return false;
	}

	const FPositionVertexBuffer& VertexPosition = WarpMeshResource->VertexBuffers.PositionVertexBuffer;
	for (uint32 i = 0; i < VertexPosition.GetNumVertices(); i++)
	{
		if (GetProjectionClip(FVector4(VertexPosition.VertexPosition(i), 1)) == false)
		{
			bAllPointsInFrustum = false;
		}
	}

	return bAllPointsInFrustum;
}

// Calculate better performance for PFM
bool FDisplayClusterWarpBlendMath_Frustum::ImplCalcFrustum_LOD_WarpMap()
{
	if (GeometryContext.GeometryProxy.WarpMap == nullptr)
	{
		return false;
	}

	bool bAllPointsInFrustum = true;

	const TArray<int>& IndexLOD = GeometryContext.GeometryProxy.GeometryCache.IndexLOD;

	if (IndexLOD.Num() == 0)
	{
		check(WarpMapLODRatio > 0);

		GeometryContext.GeometryProxy.UpdateGeometryLOD(FIntPoint(WarpMapLODRatio));
	}

	int PointsAmmount = GeometryContext.GeometryProxy.WarpMap->GetTotalPoints();
	FVector4* PointsSouce = (FVector4*)(GeometryContext.GeometryProxy.WarpMap->GetData());

	// Search a camera space frustum
	for (const int& It: IndexLOD)
	{
		if (GetProjectionClip(PointsSouce[It]) == false)
		{
			bAllPointsInFrustum = false;
		}
	}

	return bAllPointsInFrustum;
}

