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
	FDisplayClusterProjectionMeshPolicy(const FString& ViewportId);
	virtual ~FDisplayClusterProjectionMeshPolicy();

	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool HandleAddViewport(const FIntPoint& ViewportSize, const uint32 ViewsAmount) override;

	virtual EWarpType GetWarpType() const override
	{ return EWarpType::mesh; }

	bool AssignWarpMesh(UStaticMeshComponent* MeshComponent, USceneComponent* OriginComponent);
};
