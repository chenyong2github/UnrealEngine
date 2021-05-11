// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

#include "DisplayClusterConfiguratorViewModelMacros.h"

class UDisplayClusterConfigurationViewport;
struct FDisplayClusterConfigurationRectangle;

class FDisplayClusterConfiguratorViewportViewModel
{
public:
	FDisplayClusterConfiguratorViewportViewModel(UDisplayClusterConfigurationViewport* Viewport);

	void SetRegion(const FDisplayClusterConfigurationRectangle& NewRegion);

private:
	TWeakObjectPtr<UDisplayClusterConfigurationViewport> ViewportPtr;

	PROPERTY_HANDLE(Region);
};