// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "AssetTypeActions_Base.h"
#include "CurveEditorTypes.h"
#include "Modules/ModuleManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"


class FAudioModulationEditorModule : public IModuleInterface
{
public:
	FAudioModulationEditorModule();

	TSharedPtr<FExtensibilityManager> GetModulationSettingsMenuExtensibilityManager();
	TSharedPtr<FExtensibilityManager> GetModulationSettingsToolbarExtensibilityManager();

	virtual void StartupModule() override;

	void RegisterCustomPropertyLayouts();

	virtual void ShutdownModule() override;

private:
	void SetIcon(const FString& ClassName);

	TArray<TSharedPtr<FAssetTypeActions_Base>> AssetActions;

	TSharedPtr<FExtensibilityManager> ModulationSettingsMenuExtensibilityManager;
	TSharedPtr<FExtensibilityManager> ModulationSettingsToolBarExtensibilityManager;

	TSharedPtr<FSlateStyleSet> StyleSet;
};
