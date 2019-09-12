// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/InteractiveToolsCommands.h"


/**
 * TInteractiveToolCommands implementation for this module that provides standard Editor hotkey support
 */
class FModelingToolActionCommands : public TInteractiveToolCommands<FModelingToolActionCommands>
{
public:
	FModelingToolActionCommands();

	virtual void GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs) override;
};

