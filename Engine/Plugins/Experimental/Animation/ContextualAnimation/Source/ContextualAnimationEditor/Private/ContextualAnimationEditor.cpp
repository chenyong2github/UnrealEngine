// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimationEditor.h"
#include "ContextualAnimEdMode.h"
#include "ContextualAnimEditorStyle.h"

#define LOCTEXT_NAMESPACE "FContextualAnimationEditorModule"

void FContextualAnimationEditorModule::StartupModule()
{
	FContextualAnimEditorStyle::Initialize();
	FContextualAnimEditorStyle::ReloadTextures();

	FEditorModeRegistry::Get().RegisterMode<FContextualAnimEdMode>(FContextualAnimEdMode::EM_ContextualAnimEdModeId, 
		LOCTEXT("ContextualAnimEdModeEdModeName", "ContextualAnim"), 
		FSlateIcon(FContextualAnimEditorStyle::GetStyleSetName(), "ContextualAnimEditor.Icon", "ContextualAnimEditor.Icon"),
		true);
}

void FContextualAnimationEditorModule::ShutdownModule()
{
	FContextualAnimEditorStyle::Shutdown();

	FEditorModeRegistry::Get().UnregisterMode(FContextualAnimEdMode::EM_ContextualAnimEdModeId);
}

FContextualAnimationEditorModule& FContextualAnimationEditorModule::Get()
{
	return FModuleManager::Get().GetModuleChecked<FContextualAnimationEditorModule>("ContextualAnimationEditor");
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FContextualAnimationEditorModule, ContextualAnimationEditor)