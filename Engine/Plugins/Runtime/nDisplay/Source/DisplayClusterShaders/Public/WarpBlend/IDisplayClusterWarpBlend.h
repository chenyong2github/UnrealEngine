// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DisplayClusterWarpEnums.h"
#include "DisplayClusterWarpContext.h"

class IDisplayClusterRenderTexture;
class UStaticMeshComponent;
class USceneComponent;
struct FMPCDIGeometryExportData;

class IDisplayClusterWarpBlend
{
public:
	virtual ~IDisplayClusterWarpBlend() = default;

public:
	/**
	* Calculate warp context data for new eye
	*
	* @param InEye           - Current eye and scene
	* @param OutWarpContext  - Output context
	*
	* @return - true if the context calculated successfully
	*/
	virtual bool CalcFrustumContext(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, const FDisplayClusterWarpEye& InEye, FDisplayClusterWarpContext& OutWarpContext) = 0;

	// Access to resources
	virtual class FRHITexture* GetTexture(EDisplayClusterWarpBlendTextureType TextureType) const = 0;
	virtual float              GetAlphaMapEmbeddedGamma() const = 0;

	virtual const class FDisplayClusterRender_MeshComponent* GetWarpMesh() const = 0;

	// Return current warp profile type
	virtual EDisplayClusterWarpProfileType  GetWarpProfileType() const = 0;

	// Return current warp resource type
	virtual EDisplayClusterWarpGeometryType GetWarpGeometryType() const = 0;

	virtual bool ExportWarpMapGeometry(FMPCDIGeometryExportData* OutMeshData, uint32 InMaxDimension = 0) const = 0;
};
