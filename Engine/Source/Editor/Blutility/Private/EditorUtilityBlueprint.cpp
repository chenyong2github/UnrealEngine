// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorUtilityBlueprint.h"
#include "Modules/ModuleManager.h"

/////////////////////////////////////////////////////
// UEditorUtilityBlueprint

UEditorUtilityBlueprint::UEditorUtilityBlueprint(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UEditorUtilityBlueprint::SupportedByDefaultBlueprintFactory() const
{
	return false;
}

bool UEditorUtilityBlueprint::AlwaysCompileOnLoad() const
{
	return true;
}

bool UEditorUtilityBlueprint::CanRecompileWhilePlayingInEditor() const
{
	return false;
}