// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimationEditor.h"
#include "ContextualAnimEdMode.h"
#include "ContextualAnimEditorStyle.h"
#include "Customizations/MathStructCustomizations.h"

#define LOCTEXT_NAMESPACE "FContextualAnimationEditorModule"

void FContextualAnimationEditorModule::StartupModule()
{
	FContextualAnimEditorStyle::Initialize();
	FContextualAnimEditorStyle::ReloadTextures();

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout("ContextualAnimDistanceParam", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FMathStructCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("ContextualAnimAngleParam", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FMathStructCustomization::MakeInstance));

	FEditorModeRegistry::Get().RegisterMode<FContextualAnimEdMode>(FContextualAnimEdMode::EM_ContextualAnimEdModeId, 
		LOCTEXT("ContextualAnimEdModeEdModeName", "ContextualAnim"), 
		FSlateIcon(FContextualAnimEditorStyle::GetStyleSetName(), "ContextualAnimEditor.Icon", "ContextualAnimEditor.Icon"),
		true);
}

void FContextualAnimationEditorModule::ShutdownModule()
{
	FContextualAnimEditorStyle::Shutdown();

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.UnregisterCustomPropertyTypeLayout("ContextualAnimDistanceParam");
	PropertyModule.UnregisterCustomPropertyTypeLayout("ContextualAnimAngleParam");

	FEditorModeRegistry::Get().UnregisterMode(FContextualAnimEdMode::EM_ContextualAnimEdModeId);
}

FContextualAnimationEditorModule& FContextualAnimationEditorModule::Get()
{
	return FModuleManager::Get().GetModuleChecked<FContextualAnimationEditorModule>("ContextualAnimationEditor");
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FContextualAnimationEditorModule, ContextualAnimationEditor)