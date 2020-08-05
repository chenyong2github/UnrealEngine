// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"

class FLevelAssetEditorCommands : public TCommands<FLevelAssetEditorCommands>
{
public:

	FLevelAssetEditorCommands()
		: TCommands<FLevelAssetEditorCommands>(TEXT("LevelAssetEditor"), NSLOCTEXT("Contexts", "FLevelAssetEditorModule", "Level Asset Editor Plugin"), NAME_None, FEditorStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > OpenPluginWindow;
};