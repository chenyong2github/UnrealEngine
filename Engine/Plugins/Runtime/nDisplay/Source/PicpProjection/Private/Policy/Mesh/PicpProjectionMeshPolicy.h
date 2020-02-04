// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "../MPCDI/PicpProjectionMPCDIPolicy.h"


/**
 * Adapter for the Picp Mesh warp
 */
class FPicpProjectionMeshPolicy
	: public FPicpProjectionMPCDIPolicy
{
public:
	FPicpProjectionMeshPolicy(const FString& ViewportId);
	virtual ~FPicpProjectionMeshPolicy();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool HandleAddViewport(const FIntPoint& ViewportSize, const uint32 ViewsAmount) override;

	virtual EWarpType GetWarpType() const override
	{ return EWarpType::Mesh; }

	bool AssignWarpMesh(UStaticMeshComponent* MeshComponent, USceneComponent* OriginComponent);
};
