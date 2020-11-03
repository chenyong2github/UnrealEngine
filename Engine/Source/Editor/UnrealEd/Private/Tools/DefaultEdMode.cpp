// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/DefaultEdMode.h"

#include "EditorModes.h"
#include "EditorStyleSet.h"
#include "Textures/SlateIcon.h"

UEdModeDefault::UEdModeDefault()
{
	Info = FEditorModeInfo(
		FBuiltinEditorModes::EM_Default,
		NSLOCTEXT("DefaultMode", "DisplayName", "Select"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.SelectMode", "LevelEditor.SelectMode.Small"),
		true, 0);
}

bool UEdModeDefault::UsesPropertyWidgets() const
{
	return true;
}

bool UEdModeDefault::UsesToolkits() const
{
	return false;
}
