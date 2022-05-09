// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FContextualAnimAssetEditorCommands : public TCommands<FContextualAnimAssetEditorCommands>
{
public:
	FContextualAnimAssetEditorCommands()
		: TCommands<FContextualAnimAssetEditorCommands>(TEXT("ContextualAnimAssetEditor"), NSLOCTEXT("Contexts", "ContextualAnim", "Contextual Anim"), NAME_None, FAppStyle::GetStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

public:

	TSharedPtr<FUICommandInfo> ResetPreviewScene;

	TSharedPtr<FUICommandInfo> ShowIKTargetsDrawSelected;

	TSharedPtr<FUICommandInfo> ShowIKTargetsDrawAll;

	TSharedPtr<FUICommandInfo> ShowIKTargetsDrawNone;

	TSharedPtr<FUICommandInfo> Simulate;
};
