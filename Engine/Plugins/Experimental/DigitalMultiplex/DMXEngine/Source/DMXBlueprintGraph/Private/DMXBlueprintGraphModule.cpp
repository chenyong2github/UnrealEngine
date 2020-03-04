// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXBlueprintGraphModule.h"
#include "DMXGraphPanelPinFactory.h"
#include "K2Node_GetDMXActiveModeFunctionValues.h"
#include "Customizations/K2Node_GetDMXActiveModeFunctionValuesCustomization.h"
#include "DMXBlueprintGraphLog.h"

#include "AssetToolsModule.h"
#include "PropertyEditorModule.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"

#define LOCTEXT_NAMESPACE "DMXBlueprintGraphModule"

DEFINE_LOG_CATEGORY(LogDMXBlueprintGraph);

void FDMXBlueprintGraphModule::StartupModule()
{
	DMXGraphPanelPinFactory = MakeShared<FDMXGraphPanelPinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(DMXGraphPanelPinFactory);

	RegisterObjectCustomizations();
}

void FDMXBlueprintGraphModule::ShutdownModule()
{
	FEdGraphUtilities::UnregisterVisualPinFactory(DMXGraphPanelPinFactory);


	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		// Unregister all classes customized by name
		for (const FName& ClassName : RegisteredClassNames)
		{
			PropertyModule.UnregisterCustomClassLayout(ClassName);
		}

		PropertyModule.NotifyCustomizationModuleChanged();
	}
}

void FDMXBlueprintGraphModule::RegisterObjectCustomizations()
{
	RegisterCustomClassLayout(UK2Node_GetDMXActiveModeFunctionValues::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&K2Node_GetDMXActiveModeFunctionValuesCustomization::MakeInstance));
}

void FDMXBlueprintGraphModule::RegisterCustomClassLayout(FName ClassName, FOnGetDetailCustomizationInstance DetailLayoutDelegate)
{
	check(ClassName != NAME_None);

	RegisteredClassNames.Add(ClassName);

	static FName PropertyEditor("PropertyEditor");
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(PropertyEditor);
	PropertyModule.RegisterCustomClassLayout(ClassName, DetailLayoutDelegate);
}


IMPLEMENT_MODULE(FDMXBlueprintGraphModule, DMXBlueprintGraph)

#undef LOCTEXT_NAMESPACE
