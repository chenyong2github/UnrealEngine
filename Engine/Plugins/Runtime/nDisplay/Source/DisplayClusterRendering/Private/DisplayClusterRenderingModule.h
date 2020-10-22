// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDisplayClusterRendering.h"


class FDisplayClusterRenderingModule
	: public IDisplayClusterRendering
{
public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterRendering
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void RenderSceneToTexture(const FDisplayClusterRenderingParameters& RenderParams) override;
};
