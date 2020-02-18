// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MPCDIWarp.h"
#include "MPCDITexture.h"
#include "Blueprints/MPCDIContainers.h"


namespace mpcdi
{
	struct GeometryWarpFile;
	struct PFM;
};

class FMPCDIWarpTexture
	: public FMPCDITexture
	, public FMPCDIWarp
{
public:
	FMPCDIWarpTexture()
		: FMPCDITexture()
		, FMPCDIWarp()
	{ }

	virtual ~FMPCDIWarpTexture()
	{ }

	virtual EWarpGeometryType GetWarpGeometryType()  const  override
	{ return EWarpGeometryType::PFM_Texture; }

	virtual bool IsFrustumCacheDisabled() const override
	{ return false; }

	virtual void BeginRender(FRHICommandListImmediate& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit) override;
	virtual void FinishRender(FRHICommandListImmediate& RHICmdList) override;

public:
	void BuildAABBox();

	bool LoadCustom3DWarpMap(const TArray<FVector>& Points, int DimW, int DimH, IMPCDI::EMPCDIProfileType ProfileType, float WorldScale, bool bIsUnrealGameSpace);
	bool LoadPFMFile(mpcdi::PFM& SourcePFM, IMPCDI::EMPCDIProfileType ProfileType, float PFMScale, bool bIsUnrealGameSpace);
	bool LoadWarpMap(mpcdi::GeometryWarpFile *SourceWarpMap, IMPCDI::EMPCDIProfileType ProfileType);

	void ExportMeshData(FMPCDIGeometryExportData& Dst);
	void ImportMeshData(const FMPCDIGeometryImportData& Src);

private:
	virtual bool IsValidWarpData() const override
	{
		return IsValid();
	}

	virtual void BeginBuildFrustum(IMPCDI::FFrustum& OutFrustum) override;
	virtual bool CalcFrustum_fullCPU(const IMPCDI::FFrustum& Frustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const override;
	virtual bool CalcFrustum_TextureBOX(int DivX, int DivY, const IMPCDI::FFrustum& Frustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const override;

private:
	bool Is3DPointValid(int X, int Y) const;
	void BuildWarpMapData(IMPCDI::EMPCDIProfileType ProfileType);
	void ClearNoise(const FIntPoint& SearchXYDepth, const FIntPoint& AllowedXYDepthRules);
	int RemoveDetachedPoints(const FIntPoint& SearchXYDepth, const FIntPoint& AllowedXYDepthRules);

private:
	mutable TArray<int> TextureBoxCache;
};
