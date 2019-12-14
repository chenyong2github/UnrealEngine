// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Set.h"
#include "Modules/ModuleInterface.h"

SOUNDCUETEMPLATESEDITOR_API DECLARE_LOG_CATEGORY_EXTERN(SoundCueTemplatesEditor, Log, All);

class FSoundCueTemplatesEditorModule :	public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterAssetActions();
};
