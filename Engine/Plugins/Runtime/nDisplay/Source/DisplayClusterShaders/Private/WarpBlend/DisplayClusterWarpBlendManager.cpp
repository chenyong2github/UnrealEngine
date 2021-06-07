// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DisplayClusterWarpBlendManager.h"

#include "WarpBlend/Loader/DisplayClusterWarpBlendLoader_MPCDI.h"
#include "WarpBlend/Loader/DisplayClusterWarpBlendLoader_MeshComponent.h"

bool FDisplayClusterWarpBlendManager::Create(const FDisplayClusterWarpBlendConstruct::FLoadMPCDIFile& InConstructParameters, TSharedPtr<IDisplayClusterWarpBlend, ESPMode::ThreadSafe>& OutWarpBlend) const
{
	return FDisplayClusterWarpBlendLoader_MPCDI::Load(InConstructParameters, OutWarpBlend);
}

bool FDisplayClusterWarpBlendManager::Create(const FDisplayClusterWarpBlendConstruct::FLoadPFMFile& InConstructParameters, TSharedPtr<IDisplayClusterWarpBlend, ESPMode::ThreadSafe>& OutWarpBlend) const
{
	return FDisplayClusterWarpBlendLoader_MPCDI::Load(InConstructParameters, OutWarpBlend);
}

bool  FDisplayClusterWarpBlendManager::Create(const FDisplayClusterWarpBlendConstruct::FAssignWarpMesh& InConstructParameters, TSharedPtr<IDisplayClusterWarpBlend, ESPMode::ThreadSafe>& OutWarpBlend) const
{
	return FDisplayClusterWarpBlendLoader_MeshComponent::Load(InConstructParameters, OutWarpBlend);
};

