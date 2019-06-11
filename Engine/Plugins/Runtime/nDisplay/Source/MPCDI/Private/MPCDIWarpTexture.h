// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MPCDITexture.h"
#include "MPCDITypes.h"
#include "IMPCDI.h"

#include "Blueprints/MPCDIContainers.h"


namespace mpcdi
{
	struct GeometryWarpFile;
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
		{ }

	public:
		bool GetFrustum_A3D(IMPCDI::FFrustum &OutFrustum, float WorldScale, float ZNear, float ZFar) const;

		void LoadCustom3DWarpMap(const TArray<FVector>& Points, int DimW, int DimH);
		void LoadWarpMap(mpcdi::GeometryWarpFile *SourceWarpMap, EMPCDIProfileType ProfileType);

		inline const FBox& GetAABB() const
		{ return AABBox; }

		inline void AppendAABB(FBox& TargetAABBox)
		{ TargetAABBox += AABBox; }

		void ExportMeshData(FMPCDIGeometryExportData& Dst);

	private:
		bool Is3DPointValid(int X, int Y) const;
		void BuildWarpMapData(EMPCDIProfileType ProfileType);
		void BuildAABBox();
		void ClearNoise(const FIntPoint& SearchXYDepth, const FIntPoint& AllowedXYDepthRules);
		int RemoveDetachedPoints(const FIntPoint& SearchXYDepth, const FIntPoint& AllowedXYDepthRules);

	private:

		void CalcFrustum_simpleAABB(const IMPCDI::FFrustum& Frustum, const FMatrix& world2local, float& top, float& bottom, float& left, float& right) const;
		void CalcFrustum_fullCPU(const IMPCDI::FFrustum& Frustum, const FMatrix& world2local, float& top, float& bottom, float& left, float& right) const;

		void CalcViewProjection(const IMPCDI::FFrustum& Frustum, const FVector& ViewDirection, const FVector& ViewOrigin, const FVector& EyeOrigin, FMatrix& OutViewMatrix) const;

	private:
		FBox AABBox;

		// Orthogonal to a projection plane
		FRotator ViewRotation = FRotator::ZeroRotator;
	};
}
