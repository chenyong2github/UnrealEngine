// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"


class FDisplayClusterConfiguratorCommands 
	: public TCommands<FDisplayClusterConfiguratorCommands>
{
public:
	FDisplayClusterConfiguratorCommands()
		: TCommands<FDisplayClusterConfiguratorCommands>(TEXT("DisplayClusterConfigurator"), 
			NSLOCTEXT("Contexts", "DisplayClusterConfigurator", "Display Cluster Configurator"), NAME_None, FEditorStyle::GetStyleSetName())
	{ }

	virtual void RegisterCommands() override;

public:
	TSharedPtr<FUICommandInfo> Import;
	TSharedPtr<FUICommandInfo> SaveToFile;
	TSharedPtr<FUICommandInfo> EditConfig;
	TSharedPtr<FUICommandInfo> ToggleWindowInfo;
	TSharedPtr<FUICommandInfo> ToggleWindowCornerImage;
	TSharedPtr<FUICommandInfo> ToggleOutsideViewports;
	TSharedPtr<FUICommandInfo> ZoomToFit;
	TSharedPtr<FUICommandInfo> BrowseDocumentation;
};
