// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WarpBlend/DisplayClusterWarpEnums.h"

class IDisplayClusterRenderTexture;
class FDisplayClusterRender_MeshComponent;
class UStaticMeshComponent;
class USceneComponent;

class FDisplayClusterWarpBlend_GeometryProxy
{
public:
	FDisplayClusterWarpBlend_GeometryProxy()
	{ }

	~FDisplayClusterWarpBlend_GeometryProxy()
	{ ImplReleaseResources(); }

public:
	const IDisplayClusterRenderTexture* ImplGetTexture(EDisplayClusterWarpBlendTextureType TextureType) const
	{
		switch (TextureType)
		{
		case EDisplayClusterWarpBlendTextureType::WarpMap:
			return WarpMap;

		case EDisplayClusterWarpBlendTextureType::AlphaMap:
			return AlphaMap;

		case EDisplayClusterWarpBlendTextureType::BetaMap:
			return BetaMap;

		default:
			break;
		}
		return nullptr;
	}

	bool UpdateGeometry();
	bool UpdateGeometryLOD(const FIntPoint& InSizeLOD);

	bool AssignWarpMesh(UStaticMeshComponent* MeshComponent, USceneComponent* OriginComponent);

private:
	bool ImplUpdateGeometry_WarpMesh();
	bool ImplUpdateGeometry_WarpMap();

public:
	// for huge warp mesh geometry, change this value, and use LOD geometry in frustum math
	int WarpMeshLODIndex = 0;

	bool bIsGeometryCacheValid = false;
	bool bIsGeometryValid = false;
	EDisplayClusterWarpGeometryType GeometryType = EDisplayClusterWarpGeometryType::Invalid;
	
private:
	void ImplReleaseResources();

public:
	// Render resources:
	IDisplayClusterRenderTexture* WarpMap  = nullptr;
	IDisplayClusterRenderTexture* AlphaMap = nullptr;
	IDisplayClusterRenderTexture* BetaMap  = nullptr;

	FDisplayClusterRender_MeshComponent* WarpMesh = nullptr;

	float AlphaMapEmbeddedGamma = 1.f;

private:
	bool ImplUpdateGeometryCache_WarpMesh();
	bool ImplUpdateGeometryCache_WarpMap();

public:
	// Cached values for geometry (updated only if geometry changed)
	class FDisplayClusterWarpBlend_GeometryCache
	{
	public:
		FTransform GeometryToOrigin;

		FBox       AABBox;
		FVector    SurfaceViewNormal;
		FVector    SurfaceViewPlane;

		TArray<int> IndexLOD;
	};

	FDisplayClusterWarpBlend_GeometryCache GeometryCache;

private:
	bool bIsWarpMeshComponentLost = false;
};

