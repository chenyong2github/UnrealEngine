// Copyright Epic Games, Inc. All Rights Reserved.

#include "WarpBlend/DisplayClusterWarpBlend_GeometryProxy.h"

#include "Render/DisplayClusterRenderTexture.h"
#include "Render/Containers/DisplayClusterRender_MeshComponent.h"

#include "WarpBlend/Math/DisplayClusterWarpBlendMath_WarpMesh.h"
#include "WarpBlend/Math/DisplayClusterWarpBlendMath_WarpMap.h"

/////////////////////////////////////////////////////////////////////////////////
/// FDisplayClusterWarpBlend_GeometryProxy
/////////////////////////////////////////////////////////////////////////////////

bool FDisplayClusterWarpBlend_GeometryProxy::UpdateGeometry()
{
	switch (GeometryType)
	{
	case EDisplayClusterWarpGeometryType::WarpMesh:
		return ImplUpdateGeometry_WarpMesh();
	case EDisplayClusterWarpGeometryType::WarpMap:
		return ImplUpdateGeometry_WarpMap();
	default:
		break;
	}

	bIsGeometryValid = false;
	return false;
}

bool FDisplayClusterWarpBlend_GeometryProxy::ImplUpdateGeometry_WarpMap()
{
	bIsGeometryValid = false;

	if (WarpMap && WarpMap->IsValid())
	{
		// Update caches
		if ((bIsGeometryValid = bIsGeometryCacheValid) == false)
		{
			bIsGeometryValid = ImplUpdateGeometryCache_WarpMap();
		}

		GeometryCache.GeometryToOrigin = FTransform::Identity;
	}

	return bIsGeometryValid;
}

bool FDisplayClusterWarpBlend_GeometryProxy::ImplUpdateGeometry_WarpMesh()
{
	bIsGeometryValid = false;

	if (WarpMesh == nullptr)
	{
		return false;
	}

	UStaticMeshComponent* MeshComponent = WarpMesh->MeshComponentRef.GetOrFindMeshComponent();
	USceneComponent*    OriginComponent = WarpMesh->OriginComponentRef.GetOrFindSceneComponent();

	if (MeshComponent == nullptr)
	{
		// mesh deleted?
		WarpMesh->UpdateDefferedRef();
		bIsWarpMeshComponentLost = true;
		return false;
	};

	UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
	if (StaticMesh == nullptr)
	{
		// mesh geometry not assigned?
		return false;
	}

	// If StaticMesh geometry changed, update mpcdi math and RHI resources
	if (WarpMesh->MeshComponentRef.IsMeshComponentChanged() || bIsWarpMeshComponentLost)
	{
		AssignWarpMesh(MeshComponent, OriginComponent);
		bIsWarpMeshComponentLost = false;
	}
	
	// Update caches
	if ((bIsGeometryValid = bIsGeometryCacheValid) == false)
	{
		bIsGeometryValid = ImplUpdateGeometryCache_WarpMesh();
	}

	if (OriginComponent)
	{
		FMatrix MeshToWorldMatrix = MeshComponent->GetComponentTransform().ToMatrixWithScale();
		FMatrix WorldToOriginMatrix = OriginComponent->GetComponentTransform().ToInverseMatrixWithScale();

		GeometryCache.GeometryToOrigin.SetFromMatrix(MeshToWorldMatrix * WorldToOriginMatrix);
	}
	else
	{
		GeometryCache.GeometryToOrigin = MeshComponent->GetRelativeTransform();
	}

	return bIsGeometryValid;
}

bool FDisplayClusterWarpBlend_GeometryProxy::AssignWarpMesh(UStaticMeshComponent* MeshComponent, USceneComponent* OriginComponent)
{
	check(IsInGameThread());

	GeometryType = EDisplayClusterWarpGeometryType::WarpMesh;

	if (WarpMesh)
	{
		WarpMesh->AssignMeshRefs(MeshComponent, OriginComponent);

		return ImplUpdateGeometryCache_WarpMesh();
	}

	return false;
}

bool FDisplayClusterWarpBlend_GeometryProxy::ImplUpdateGeometryCache_WarpMesh()
{
	bIsGeometryValid = false;

	if (WarpMesh)
	{
		const FStaticMeshLODResources* WarpMeshResource = WarpMesh->GetStaticMeshLODResource();
		if (WarpMeshResource != nullptr)
		{
			FDisplayClusterWarpBlendMath_WarpMesh MeshHelper(*WarpMeshResource);

			GeometryCache.AABBox = MeshHelper.CalcAABBox();
			MeshHelper.CalcSurfaceVectors(GeometryCache.SurfaceViewNormal, GeometryCache.SurfaceViewPlane);

			bIsGeometryCacheValid = true;
			bIsGeometryValid = true;
		}
	}

	return bIsGeometryValid;
}

bool FDisplayClusterWarpBlend_GeometryProxy::ImplUpdateGeometryCache_WarpMap()
{
	// WarpMap must be initialized before, outside
	bIsGeometryValid = false;
	
	if (WarpMap && WarpMap->IsValid())
	{
		FDisplayClusterWarpBlendMath_WarpMap DataHelper(*WarpMap);
		GeometryCache.AABBox = DataHelper.GetAABBox();
		GeometryCache.SurfaceViewNormal = DataHelper.GetSurfaceViewNormal();
		GeometryCache.SurfaceViewPlane = DataHelper.GetSurfaceViewPlane();

		bIsGeometryCacheValid = true;
		bIsGeometryValid = true;
	}

	return bIsGeometryValid;
}

bool FDisplayClusterWarpBlend_GeometryProxy::UpdateGeometryLOD(const FIntPoint& InSizeLOD)
{
	check(InSizeLOD.X > 0 && InSizeLOD.Y > 0);

	GeometryCache.IndexLOD.Empty();

	if (WarpMap && WarpMap->IsValid())
	{
		switch (GeometryType)
		{
		case EDisplayClusterWarpGeometryType::WarpMap:
		{
			// Generate valid points for texturebox method:
			FDisplayClusterWarpBlendMath_WarpMap DataHelper(*WarpMap);
			DataHelper.BuildIndexLOD(InSizeLOD.X, InSizeLOD.Y, GeometryCache.IndexLOD);

			return true;
		}
		default:
			break;
		}
	}

	return false;
}

void FDisplayClusterWarpBlend_GeometryProxy::ImplReleaseResources()
{
	if (WarpMap)
	{
		delete WarpMap;
		WarpMap = nullptr;
	}

	if (AlphaMap)
	{
		delete AlphaMap;
		AlphaMap = nullptr;
	}

	if (BetaMap)
	{
		delete BetaMap;
		BetaMap = nullptr;
	}

	if (WarpMesh)
	{
		delete WarpMesh;
		WarpMesh = nullptr;
	}
}

