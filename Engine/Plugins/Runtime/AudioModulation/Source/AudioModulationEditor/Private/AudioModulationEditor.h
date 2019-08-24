// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"


class FAudioModulationEditorModule : public IModuleInterface
{
public:
	FAudioModulationEditorModule();

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void SetIcon(const FString& ClassName);
};
