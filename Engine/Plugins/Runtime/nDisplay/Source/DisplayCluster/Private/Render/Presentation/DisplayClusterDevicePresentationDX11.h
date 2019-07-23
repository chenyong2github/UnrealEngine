// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FRHIViewport;


/**
 * Helper class to encapsulate DX11 frame presentation
 */
class FDisplayClusterDevicePresentationDX11
{
public:
	FDisplayClusterDevicePresentationDX11() = default;
	virtual ~FDisplayClusterDevicePresentationDX11() = default;

public:
	bool PresentImpl(FRHIViewport* Viewport, const int32 InSyncInterval);
};
