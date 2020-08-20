// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FExtender;
class FToolBarBuilder;
class FDMXPixelMappingToolkit;
class SWidget;

/**
 * Custom Toolbar for DMX Pixel Mapping Editor
 */
class FDMXPixelMappingToolbar :
	public TSharedFromThis<FDMXPixelMappingToolbar>
{
public:
	/** Default Constructor */
	FDMXPixelMappingToolbar(TSharedPtr<FDMXPixelMappingToolkit> InToolkit);

	/** Virtual Destructor */
	virtual ~FDMXPixelMappingToolbar() {}

	void BuildToolbar(TSharedPtr<FExtender> Extender);

private:
	void Build(FToolBarBuilder& ToolbarBuilder);

	void AddHelpersSection(FToolBarBuilder& ToolbarBuilder);
	void AddPlayAndStopSection(FToolBarBuilder& ToolbarBuilder);
	TSharedRef<SWidget> FillPlayMenu();

public:
	TWeakPtr<FDMXPixelMappingToolkit> ToolkitWeakPtr;
};

