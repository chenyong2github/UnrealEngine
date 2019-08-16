// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	class FMPCDIWarpTexture : public FMPCDITexture
	{
	public:
		FMPCDIWarpTexture()
			: FMPCDITexture()
			, AABBox(FVector(FLT_MAX, FLT_MAX, FLT_MAX)
			, FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX))
		{ 
		}
		virtual ~FMPCDIWarpTexture()
		{
		}

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
		void CalcFrustum_simpleAABB(const FVector* AABBoxPts, const IMPCDI::FFrustum& Frustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const;
		void CalcFrustum_fullCPU(const IMPCDI::FFrustum& Frustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const;
		void CalcFrustum_TextureBOX(int DivX, int DivY, const IMPCDI::FFrustum& Frustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const;
		
		void CalcViewProjection(const FVector* AABBoxPts, const IMPCDI::FFrustum& Frustum, const FVector& ViewDirection, const FVector& ViewOrigin, const FVector& EyeOrigin, FMatrix& OutViewMatrix) const;

	private:
		FBox    AABBox;
		FVector SurfaceViewNormal; // Static surface average normal for this region

		mutable FCriticalSection DataGuard;
		mutable TArray<IMPCDI::FFrustum> FrustumCache;
		mutable TArray<int> TextureBoxCache;
	};
}
