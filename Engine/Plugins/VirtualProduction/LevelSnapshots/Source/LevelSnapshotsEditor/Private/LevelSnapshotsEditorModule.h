// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ILevelSnapshotsEditorModule.h"

class ULevelSnapshotsEditorData;

class FLevelSnapshotsEditorModule : public ILevelSnapshotsEditorModule
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();

	void CreateLevelSnapshotsEditor();

	ULevelSnapshotsEditorData* AllocateTransientPreset();
};
