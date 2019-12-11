// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Presentation/DisplayClusterPresentationBase.h"


/**
 * Helper class to encapsulate DX11 frame presentation
 */
class FDisplayClusterPresentationDX11 : public FDisplayClusterPresentationBase
{
public:
	FDisplayClusterPresentationDX11(FViewport* const InViewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& InSyncPolicy);
	virtual ~FDisplayClusterPresentationDX11();
};
