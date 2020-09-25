// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorStyleSet.h"
#include "Framework/Commands/Commands.h"


/** */
class FDisplayClusterConfiguratorTreeViewCommands
	: public TCommands<FDisplayClusterConfiguratorTreeViewCommands>
{
public:
	FDisplayClusterConfiguratorTreeViewCommands()
		: TCommands<FDisplayClusterConfiguratorTreeViewCommands>
		(
			TEXT("ConfiguratorViewTree"), // Context name for fast lookup
			NSLOCTEXT("Contexts", "ConfiguratorViewTree", "Configurator View Tree"), // Localized context name for displaying
			NAME_None, // Parent context name.  
			FEditorStyle::GetStyleSetName() // Icon Style Set
		)
	{ }

public:
	/** Initialize commands */
	virtual void RegisterCommands() override;

	/** Show all nodes in the tree */
	TSharedPtr< FUICommandInfo > ShowAllNodes;
};
