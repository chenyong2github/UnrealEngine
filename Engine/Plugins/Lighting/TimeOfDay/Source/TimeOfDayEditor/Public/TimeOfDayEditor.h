// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FToolBarBuilder;
class FMenuBuilder;
class SDockTab;
class FSpawnTabArgs;
class FPropertySection;
class FPropertyEditorModule;

class FTimeOfDayEditorModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	void OnOpenTimeOfDayEditor();

	TSharedPtr<FPropertySection> RegisterPropertySection(FPropertyEditorModule& PropertyModule, FName ClassName, FName SectionName, FText DisplayName);
	void RegisterModulePropertySections();
	void DeregisterModulePropertySections();

private:
	void RegisterMenus();

	TSharedRef<SDockTab> CreateTimeOfDayTab(const FSpawnTabArgs& Args);

	TSharedPtr<class FUICommandList> PluginCommands;

	TMultiMap<FName, FName> RegisteredPropertySections;
};
