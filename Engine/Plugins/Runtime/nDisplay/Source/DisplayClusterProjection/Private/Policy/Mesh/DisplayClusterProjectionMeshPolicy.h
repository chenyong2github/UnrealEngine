// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/MPCDI/DisplayClusterProjectionMPCDIPolicy.h"

class IDisplayClusterViewport;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class USceneComponent;

/*
 * Mesh projection policy
 * Supported geometry sources - StaticMeshComponent, ProceduralMeshComponent
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
		// StaticMesh component with source geometry
		UStaticMeshComponent*     StaticMeshComponent = nullptr;
		// StaticMesh geometry LOD
		int32 StaticMeshComponentLODIndex = 0;

		// ProceduralMesh component with source geometry
		UProceduralMeshComponent* ProceduralMeshComponent = nullptr;
		// ProceduralMesh section index
		int32 ProceduralMeshComponentSectionIndex = 0;

		// Customize source geometry UV channels
		int32 BaseUVIndex = INDEX_NONE;
		int32 ChromakeyUVIndex = INDEX_NONE;
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
