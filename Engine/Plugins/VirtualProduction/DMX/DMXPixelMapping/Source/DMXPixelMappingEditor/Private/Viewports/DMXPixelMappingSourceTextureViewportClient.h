// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewportClient.h"


class SDMXPixelMappingSourceTextureViewport;
class FDMXPixelMappingToolkit;

class FDMXPixelMappingSourceTextureViewportClient
	: public FViewportClient
{
public:
	/** Constructor */
	FDMXPixelMappingSourceTextureViewportClient(const TSharedPtr<FDMXPixelMappingToolkit>& Toolkit, TWeakPtr<SDMXPixelMappingSourceTextureViewport> InViewport);
	
	/**
	 * Returns true if it only the visible rectangle is drawn, instead of the whole texture.
	 * This is favorable when zoomed in closely and the viewport size gets close to, or exceeds GMaxTextureDimensions.
	 */
	bool DrawOnlyVisibleRect() const;

	/** Returns the visible texture size in graph space */
	FBox2D GetVisibleTextureBoxGraphSpace() const;

protected:
	//~ Begin FViewportClient Interface
	virtual void Draw(FViewport* Viewport, FCanvas* Canvas) override;
	//~ End FViewportClient Interface

private:
	TWeakPtr<FDMXPixelMappingToolkit> WeakToolkit;
	TWeakPtr<SDMXPixelMappingSourceTextureViewport> WeakSourceTextureViewport;
};
