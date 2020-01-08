// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMeshPaintMode.h"

class FModeToolkit;

/**
 * Mesh Paint editor mode
 */
class FEdModeMeshPaint : public IMeshPaintEdMode
{
public:
	/** Constructor */
	FEdModeMeshPaint() {}

	/** Destructor */
	virtual ~FEdModeMeshPaint() {}
	virtual void Initialize() override;
	virtual TSharedPtr< FModeToolkit> GetToolkit() override;

	// IMeshPaintEdMode interface.
	virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent) override;

	bool IsEditingEnabled() const;
};