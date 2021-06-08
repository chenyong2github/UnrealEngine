// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WarpBlend/IDisplayClusterWarpBlendManager.h"

class FDisplayClusterWarpBlendManager
	: public IDisplayClusterWarpBlendManager
{
public:
	virtual bool Create(const FDisplayClusterWarpBlendConstruct::FLoadMPCDIFile& InConstructParameters,  TSharedPtr<IDisplayClusterWarpBlend, ESPMode::ThreadSafe>& OutWarpBlend) const override;
	virtual bool Create(const FDisplayClusterWarpBlendConstruct::FLoadPFMFile& InConstructParameters,    TSharedPtr<IDisplayClusterWarpBlend, ESPMode::ThreadSafe>& OutWarpBlend) const override;
	virtual bool Create(const FDisplayClusterWarpBlendConstruct::FAssignWarpMesh& InConstructParameters, TSharedPtr<IDisplayClusterWarpBlend, ESPMode::ThreadSafe>& OutWarpBlend) const override;
};
