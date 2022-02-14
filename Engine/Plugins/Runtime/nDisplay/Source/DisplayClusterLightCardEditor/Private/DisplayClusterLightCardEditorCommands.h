// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"


class FDisplayClusterLightCardEditorCommands 
	: public TCommands<FDisplayClusterLightCardEditorCommands>
{
public:
	FDisplayClusterLightCardEditorCommands()
		: TCommands<FDisplayClusterLightCardEditorCommands>(TEXT("DisplayClusterLightCardEditor"), 
			NSLOCTEXT("Contexts", "DisplayClusterLightCardEditor", "Display Cluster LightCard Editor"), NAME_None, FEditorStyle::GetStyleSetName())
	{ }

	virtual void RegisterCommands() override;

public:
	// Viewport commands
	TSharedPtr<FUICommandInfo> ResetCamera;

	TSharedPtr<FUICommandInfo> PerspectiveProjection;
	TSharedPtr<FUICommandInfo> AzimuthalProjection;
};
