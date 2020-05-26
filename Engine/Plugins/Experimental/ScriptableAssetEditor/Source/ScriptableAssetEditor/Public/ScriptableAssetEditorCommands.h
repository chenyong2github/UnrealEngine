// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"

class FScriptableAssetEditorCommands : public TCommands<FScriptableAssetEditorCommands>
{
public:

	FScriptableAssetEditorCommands()
		: TCommands<FScriptableAssetEditorCommands>(TEXT("ScriptableAssetEditor"), NSLOCTEXT("Contexts", "ScriptableAssetEditor", "ScriptableAssetEditor Plugin"), NAME_None, FEditorStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > OpenPluginWindow;
};