// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMPCDI.h"
#include "MPCDIWarp.h"

class USceneComponent;
class UStaticMeshComponent;


class FMPCDIWarpMesh
	: public FMPCDIWarp
{
public:
	FMPCDIWarpMesh()
		: FMPCDIWarp()
		, MeshComponent(nullptr)
		, OriginComponent(nullptr)
	{ }

	virtual ~FMPCDIWarpMesh()
	{ }

	virtual EWarpGeometryType GetWarpGeometryType()  const  override
	{
		return EWarpGeometryType::UE_StaticMesh;
	}

	virtual bool IsFrustumCacheDisabled() const override
	{
		// Frustum cache not used with mesh warp for now
		return true;
	}

	bool SetStaticMeshWarp(UStaticMeshComponent* InMeshComponent, USceneComponent* InOriginComponent);

	virtual void BeginRender(FRHICommandListImmediate& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit) override;
	virtual void FinishRender(FRHICommandListImmediate& RHICmdList) override;

private:
	virtual void BeginBuildFrustum(IMPCDI::FFrustum& OutFrustum) override;
	virtual bool IsValidWarpData() const override;

	virtual bool CalcFrustum_fullCPU(const IMPCDI::FFrustum& Frustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const override;
	virtual bool CalcFrustum_TextureBOX(int DivX, int DivY, const IMPCDI::FFrustum& Frustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const override;

	void BuildAABBox();

	void CreateRHIResources();
	void ReleaseRHIResources();

	void BuildMeshAABBox();
	void ResetMeshAABB()
	{
		MeshAABBox = FBox(FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX));
	}

private:
	mutable FCriticalSection MeshDataGuard;

	UStaticMeshComponent* MeshComponent;
	USceneComponent*      OriginComponent;

	bool bIsDirtyFrustumData = false;
	bool bIsValidFrustumData = false;
	
	bool bIsValidRHI = false;
	bool bIsDirtyRHI = false;

	FBox    MeshAABBox;

	FVector MeshSurfaceViewNormal; // Static surface average normal for this region
	FVector MeshSurfaceViewPlane;  // Static surface average normal from 4 corner points

	FTransform MeshToOrigin;

	FVertexBufferRHIRef VertexBufferRHI;
	FIndexBufferRHIRef  IndexBufferRHI;

	uint32 NumTriangles;
	uint32 NumVertices;
};
