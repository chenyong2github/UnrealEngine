// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

PRESETEDITOR_API extern const FName PresetEditorTabName;

class PRESETEDITOR_API IPresetEditorModule : public IModuleInterface
{
public:

	static IPresetEditorModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IPresetEditorModule>("PresetEditor");
	}

	virtual void ExecuteOpenPresetEditor() = 0;
};
