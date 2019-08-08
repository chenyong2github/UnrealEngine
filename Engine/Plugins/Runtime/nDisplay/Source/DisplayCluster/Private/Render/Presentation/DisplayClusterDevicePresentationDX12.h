// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FRHIViewport;


/**
 * Helper class to encapsulate DX12 frame presentation
 */
class FDisplayClusterDevicePresentationDX12
{
public:
	FDisplayClusterDevicePresentationDX12() = default;
	virtual ~FDisplayClusterDevicePresentationDX12() = default;

public:
	bool PresentImpl(FRHIViewport* Viewport, const int32 InSyncInterval);
};
