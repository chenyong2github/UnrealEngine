// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterWarpBlendLoader_MeshComponent.h"

#include "DisplayClusterShadersLog.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/FileHelper.h"

#include "Stats/Stats.h"
#include "Engine/Engine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

#include "Render/Containers/DisplayClusterRender_MeshComponentProxy.h"

#include "WarpBlend/Loader/DisplayClusterWarpBlendLoader_Texture.h"
#include "WarpBlend/DisplayClusterWarpBlend.h"
#include "WarpBlend/DisplayClusterWarpBlend_GeometryContext.h"
#include "WarpBlend/DisplayClusterWarpBlend_GeometryProxy.h"

bool FDisplayClusterWarpBlendLoader_MeshComponent::Load(const FDisplayClusterWarpBlendConstruct::FAssignWarpMesh& InParameters, TSharedPtr<IDisplayClusterWarpBlend>& OutWarpBlend)
{
	if (InParameters.MeshComponent != nullptr)
	{
		//ok, Create and initialize warpblend interface:
		TSharedPtr<FDisplayClusterWarpBlend> WarpBlend = MakeShared<FDisplayClusterWarpBlend>();
		WarpBlend->GeometryContext.GeometryProxy.GeometryType = EDisplayClusterWarpGeometryType::WarpMesh;
		WarpBlend->GeometryContext.ProfileType = EDisplayClusterWarpProfileType::warp_A3D;

		FDisplayClusterWarpBlend_GeometryProxy& Proxy = WarpBlend->GeometryContext.GeometryProxy;
		Proxy.WarpMesh = new FDisplayClusterRender_MeshComponentProxy();
		Proxy.WarpMesh->AssignMeshRefs(InParameters.MeshComponent, InParameters.OriginComponent);

		OutWarpBlend = WarpBlend;
		return true;
	}

	return false;
}



