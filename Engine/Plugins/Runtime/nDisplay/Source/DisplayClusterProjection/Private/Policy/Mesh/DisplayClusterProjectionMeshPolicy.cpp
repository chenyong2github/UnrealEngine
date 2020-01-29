// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/Mesh/DisplayClusterProjectionMeshPolicy.h"
#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"


FDisplayClusterProjectionMeshPolicy::FDisplayClusterProjectionMeshPolicy(const FString& ViewportId)
	: FDisplayClusterProjectionMPCDIPolicy(ViewportId)
{
}

FDisplayClusterProjectionMeshPolicy::~FDisplayClusterProjectionMeshPolicy()
{
}

bool FDisplayClusterProjectionMeshPolicy::HandleAddViewport(const FIntPoint& InViewportSize, const uint32 InViewsAmount)
{
	check(IsInGameThread());
	check(InViewsAmount > 0);

	// policy wait for link to mesh data from BP
	// check this later, on render pass

	UE_LOG(LogDisplayClusterProjectionMesh, Log, TEXT("Mesh policy has been initialized. Wait for BP setup for viewport '%s'"), *GetViewportId());

	// Finally, initialize internal views data container
	Views.AddDefaulted(InViewsAmount);
	ViewportSize = InViewportSize;

	return true;
}

bool FDisplayClusterProjectionMeshPolicy::AssignWarpMesh(UStaticMeshComponent* MeshComponent, USceneComponent* OriginComponent)
{
	bool bResult = false;

	IMPCDI& MpcdiModule = IMPCDI::Get();

	if (MpcdiModule.CreateCustomRegion(DisplayClusterStrings::cfg::data::projection::mesh::FileID, DisplayClusterStrings::cfg::data::projection::mesh::BufferID, GetViewportId(), WarpRef))
	{
		// Always use advanced 3d profile from ext mesh as warp source
		MpcdiModule.SetMPCDIProfileType(WarpRef, IMPCDI::EMPCDIProfileType::mpcdi_A3D);
		bResult = true;
	}

	if (bResult)
	{
		bResult = MpcdiModule.SetStaticMeshWarp(WarpRef, MeshComponent, OriginComponent);
	}

	if (!bResult)
	{
		UE_LOG(LogDisplayClusterProjectionMesh, Error, TEXT("Couldn't assign warp mesh"));
		return false;
	}

	UE_LOG(LogDisplayClusterProjectionMesh, Log, TEXT("Mesh policy BP setup for viewport '%s' has been initialized."), *GetViewportId());
	return true;
}
