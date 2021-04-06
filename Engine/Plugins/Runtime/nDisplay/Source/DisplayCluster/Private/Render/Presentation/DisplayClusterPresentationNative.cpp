// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Presentation/DisplayClusterPresentationNative.h"
#include "Render/Synchronization/IDisplayClusterRenderSyncPolicy.h"


FDisplayClusterPresentationNative::FDisplayClusterPresentationNative(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy)
	: FDisplayClusterPresentationBase(Viewport, SyncPolicy)
{
}

FDisplayClusterPresentationNative::~FDisplayClusterPresentationNative()
{
}
