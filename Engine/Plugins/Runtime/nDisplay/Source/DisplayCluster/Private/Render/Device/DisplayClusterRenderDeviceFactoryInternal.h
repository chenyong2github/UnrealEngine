// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/IDisplayClusterRenderDeviceFactory.h"


/**
 * Factory for internal rendering devices
 */
class FDisplayClusterRenderDeviceFactoryInternal
	: public IDisplayClusterRenderDeviceFactory
{
public:
	FDisplayClusterRenderDeviceFactoryInternal();
	virtual ~FDisplayClusterRenderDeviceFactoryInternal();

public:
	virtual TSharedPtr<IDisplayClusterRenderDevice, ESPMode::ThreadSafe> Create(const FString& InDeviceType, const FString& InRHIName) override;
};
