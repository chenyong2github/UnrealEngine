// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/MPCDI/DisplayClusterProjectionMPCDIPolicy.h"
#include "Engine/Classes/Components/StaticMeshComponent.h"


/**
 * Adapter for the Mesh warp
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
	virtual bool HandleStartScene(class IDisplayClusterViewport* InViewport) override;
	
	virtual EWarpType GetWarpType() const override
	{ return EWarpType::mesh; }

	/** Parse the config data for a mesh id and try to retrieve it from the root actor. */
	bool CreateWarpMeshInterface(class IDisplayClusterViewport* InViewport);

private:
	bool GetWarpMeshAndOrigin(class IDisplayClusterViewport* InViewport, class UStaticMeshComponent* &OutMeshComponent, class USceneComponent* & OutOriginComponent);

#if WITH_EDITOR
protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicyPreview
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool HasPreviewMesh() override
	{
		return true;
	}

	virtual class UMeshComponent* GetOrCreatePreviewMeshComponent(class IDisplayClusterViewport* InViewport, bool& bOutIsRootActorComponent) override;
#endif
};
