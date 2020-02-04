// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMPCDI.h"


enum class EProjectionType : uint8
{
	StaticSurfaceNormal = 0,
	StaticSurfacePlane,
	DynamicAABBCenter,

	RuntimeProjectionModes,
	RuntimeStaticSurfaceNormalInverted,
	RuntimeStaticSurfacePlaneInverted,
};

enum class EFrustumType : uint8
{
	AABB = 0,
	PerfectCPU,
	TextureBOX,
#if 0
	PerfectGPU, // optimization purpose, project warp texture to one-pixel rendertarget, in min\max colorop pass
#endif
};

enum class EStereoMode : uint8
{
	AsymmetricAABB = 0,
	SymmetricAABB,
};

enum class EWarpGeometryType : uint8
{
	PFM_Texture = 0,
	UE_StaticMesh
};

class FMPCDIWarp
{
public:
	FMPCDIWarp()
	{
		ResetAABB();
	}

	virtual ~FMPCDIWarp()
	{ }

	virtual EWarpGeometryType GetWarpGeometryType() const = 0;
	virtual bool IsFrustumCacheDisabled() const = 0;

public:
	virtual void BeginRender(FRHICommandListImmediate& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit) = 0;
	virtual void FinishRender(FRHICommandListImmediate& RHICmdList) = 0;

	bool GetFrustum_A3D(IMPCDI::FFrustum &OutFrustum, float WorldScale, float ZNear, float ZFar);

	inline const FBox& GetAABB() const
	{ return AABBox; }

protected:
	virtual bool IsValidWarpData() const = 0;

	virtual void BeginBuildFrustum(IMPCDI::FFrustum& OutFrustum) = 0;

	virtual bool CalcFrustum_fullCPU(const IMPCDI::FFrustum& Frustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const = 0;
	virtual bool CalcFrustum_TextureBOX(int DivX, int DivY, const IMPCDI::FFrustum& Frustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const = 0;

protected:
	bool CalcFrustum_simpleAABB(const IMPCDI::FFrustum& Frustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const;

	void CalcView(EStereoMode StereoMode, IMPCDI::FFrustum& OutFrustum, FVector& OutViewDirection, FVector& OutViewOrigin, FVector& OutEyeOrigin) const;
	bool CalcFrustum(EFrustumType FrustumType, IMPCDI::FFrustum& OutFrustum, const FMatrix& World2Local) const;
	void CalcViewProjection(EProjectionType ProjectionType, const IMPCDI::FFrustum& Frustum, const FVector& ViewDirection, const FVector& ViewOrigin, const FVector& EyeOrigin, FMatrix& OutViewMatrix) const;
	bool UpdateProjectionType(EProjectionType& ProjectionType) const;

	void ResetAABB()
	{
		AABBox = FBox(FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX));
	}

protected:
	FBox    AABBox;
	FVector SurfaceViewNormal; // Static surface average normal for this region
	FVector SurfaceViewPlane;  // Static surface average normal from 4 corner points

	mutable FCriticalSection DataGuard;
	mutable TArray<IMPCDI::FFrustum> FrustumCache;
};
