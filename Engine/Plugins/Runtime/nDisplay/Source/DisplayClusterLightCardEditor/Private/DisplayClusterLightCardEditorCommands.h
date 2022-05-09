// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"


class FDisplayClusterLightCardEditorCommands 
	: public TCommands<FDisplayClusterLightCardEditorCommands>
{
public:
	FDisplayClusterLightCardEditorCommands()
		: TCommands<FDisplayClusterLightCardEditorCommands>(TEXT("DisplayClusterLightCardEditor"), 
			NSLOCTEXT("Contexts", "DisplayClusterLightCardEditor", "Display Cluster LightCard Editor"), NAME_None, FAppStyle::GetAppStyleSetName())
	{ }

	virtual void RegisterCommands() override;

public:
	// Viewport commands
	TSharedPtr<FUICommandInfo> ResetCamera;

	TSharedPtr<FUICommandInfo> PerspectiveProjection;
	TSharedPtr<FUICommandInfo> AzimuthalProjection;

	TSharedPtr<FUICommandInfo> AddNewLightCard;
	TSharedPtr<FUICommandInfo> AddExistingLightCard;
	TSharedPtr<FUICommandInfo> RemoveLightCard;
};
