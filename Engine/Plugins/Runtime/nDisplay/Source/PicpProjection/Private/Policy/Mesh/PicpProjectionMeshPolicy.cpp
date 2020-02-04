// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/Mesh/PicpProjectionMeshPolicy.h"

#include "PicpProjectionHelpers.h"
#include "PicpProjectionLog.h"
#include "PicpProjectionStrings.h"

#include "DisplayClusterProjectionHelpers.h"
#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"
#include "PicpProjectionStrings.h"

#include "Config/DisplayClusterConfigTypes.h"
#include "Game/IDisplayClusterGameManager.h"
#include "Render/IDisplayClusterRenderManager.h"

#include "Engine/RendererSettings.h"
#include "Misc/Paths.h"


FPicpProjectionMeshPolicy::FPicpProjectionMeshPolicy(const FString& ViewportId)
	: FPicpProjectionMPCDIPolicy(ViewportId)
{
}

FPicpProjectionMeshPolicy::~FPicpProjectionMeshPolicy()
{
}

bool FPicpProjectionMeshPolicy::HandleAddViewport(const FIntPoint& InViewportSize, const uint32 InViewsAmount)
{
	check(IsInGameThread());
	check(InViewsAmount > 0);

	// required link to mesh data from BP
	UE_LOG(LogPicpProjectionMesh, Log, TEXT("PICP Mesh policy has been initialized. Wait for BP setup for viewport '%s'"), *GetViewportId());

	// Finally, initialize internal views data container
	Views.AddDefaulted(InViewsAmount);
	ViewportSize = InViewportSize;

	return true;
}

bool FPicpProjectionMeshPolicy::AssignWarpMesh(UStaticMeshComponent* MeshComponent, USceneComponent* OriginComponent)
{
	bool bResult = false;

	IMPCDI& MpcdiModule = IMPCDI::Get();

	if (MpcdiModule.CreateCustomRegion(PicpProjectionStrings::cfg::data::projection::mesh::FileID, PicpProjectionStrings::cfg::data::projection::mesh::BufferID, GetViewportId(), WarpRef))
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
		//@todo: Handle error
		return false;
	}

	UE_LOG(LogPicpProjectionMesh, Log, TEXT("PICP Mesh policy BP setup for viewport '%s' has been initialized."), *GetViewportId());

	return true;
}
