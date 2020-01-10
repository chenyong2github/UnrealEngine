// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MPCDITexture.h"
#include "IMPCDI.h"

#include "Blueprints/MPCDIContainers.h"


namespace mpcdi
{
	struct GeometryWarpFile;
	struct PFM;
}


namespace MPCDI
{
	enum class EProjectionType : uint8
	{
		StaticSurfaceNormal = 0,
		StaticSurfacePlane,
		DynamicAABBCenter,
		DynamicAxisAligned,

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


	class FMPCDIWarpTexture : public FMPCDITexture
	{
	public:
		FMPCDIWarpTexture()
			: FMPCDITexture()
			, AABBox(FVector(FLT_MAX, FLT_MAX, FLT_MAX)
			, FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX))
		{ }

		virtual ~FMPCDIWarpTexture()
		{ }

	public:
		bool GetFrustum_A3D(IMPCDI::FFrustum &OutFrustum, float WorldScale, float ZNear, float ZFar) const;

		bool LoadCustom3DWarpMap(const TArray<FVector>& Points, int DimW, int DimH, IMPCDI::EMPCDIProfileType ProfileType, float WorldScale, bool bIsUnrealGameSpace);
		bool LoadPFMFile(mpcdi::PFM& SourcePFM, IMPCDI::EMPCDIProfileType ProfileType, float PFMScale, bool bIsUnrealGameSpace);

		bool LoadWarpMap(mpcdi::GeometryWarpFile *SourceWarpMap, IMPCDI::EMPCDIProfileType ProfileType);

		inline const FBox& GetAABB() const
		{ 
			return AABBox; 
		}

		inline void AppendAABB(FBox& TargetAABBox) const
		{ 
			TargetAABBox += AABBox; 
		}

		void ExportMeshData(FMPCDIGeometryExportData& Dst);
		void ImportMeshData(const FMPCDIGeometryImportData& Src);

	private:
		bool Is3DPointValid(int X, int Y) const;
		void BuildWarpMapData(IMPCDI::EMPCDIProfileType ProfileType);
		void BuildAABBox();
		void ClearNoise(const FIntPoint& SearchXYDepth, const FIntPoint& AllowedXYDepthRules);
		int RemoveDetachedPoints(const FIntPoint& SearchXYDepth, const FIntPoint& AllowedXYDepthRules);

	private:
		void CalcView(EStereoMode StereoMode, IMPCDI::FFrustum& OutFrustum, FVector& OutViewDirection, FVector& OutViewOrigin, FVector& OutEyeOrigin) const;

		bool CalcFrustum(EFrustumType FrustumType, IMPCDI::FFrustum& OutFrustum, const FMatrix& World2Local) const;
		bool CalcFrustum_simpleAABB(const IMPCDI::FFrustum& Frustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const;
		bool CalcFrustum_fullCPU(const IMPCDI::FFrustum& Frustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const;
		bool CalcFrustum_TextureBOX(int DivX, int DivY, const IMPCDI::FFrustum& Frustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const;

		void CalcViewProjection(EProjectionType ProjectionType, const IMPCDI::FFrustum& Frustum, const FVector& ViewDirection, const FVector& ViewOrigin, const FVector& EyeOrigin, FMatrix& OutViewMatrix) const;

		bool UpdateProjectionType(EProjectionType& ProjectionType) const;

	private:
		FBox    AABBox;
		FVector SurfaceViewNormal; // Static surface average normal for this region
		FVector SurfaceViewPlane;  // Static surface average normal from 4 corner points

		mutable FCriticalSection DataGuard;
		mutable TArray<IMPCDI::FFrustum> FrustumCache;
		mutable TArray<int> TextureBoxCache;
	};
}
