// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/MPCDI/DisplayClusterProjectionMPCDIPolicy.h"

class IDisplayClusterViewport;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class USceneComponent;

/**
 * Adapter for the Mesh and ProceduralMesh warp
 */
class FDisplayClusterProjectionMeshPolicy
	: public FDisplayClusterProjectionMPCDIPolicy
{
public:
	FDisplayClusterProjectionMeshPolicy(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy);
	virtual ~FDisplayClusterProjectionMeshPolicy() = default;

	virtual const FString GetTypeId() const
	{ return DisplayClusterProjectionStrings::projection::Mesh; }

	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool HandleStartScene(IDisplayClusterViewport* InViewport) override;
	
	virtual EWarpType GetWarpType() const override
	{ return EWarpType::mesh; }

	/** Parse the config data for a mesh id and try to retrieve it from the root actor. */
	bool CreateWarpMeshInterface(IDisplayClusterViewport* InViewport);

private:
	struct FWarpMeshConfiguration
	{
		USceneComponent*          OriginComponent = nullptr;
		UStaticMeshComponent*     StaticMeshComponent = nullptr;
		UProceduralMeshComponent* ProceduralMeshComponent = nullptr;
		int32 SectionIndex = 0;
	};

	bool GetWarpMeshConfiguration(IDisplayClusterViewport* InViewport, FWarpMeshConfiguration& OutWarpCfg);

#if WITH_EDITOR
protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicyPreview
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool HasPreviewMesh() override
	{
		return true;
	}

	virtual class UMeshComponent* GetOrCreatePreviewMeshComponent(IDisplayClusterViewport* InViewport, bool& bOutIsRootActorComponent) override;
#endif
};
