// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "ITranslationEditor.h"

#define LOCTEXT_NAMESPACE "FMainFrameTranslationEditorMenu"


/**
 * Static helper class for populating the "Cook Content" menu.
 */
class FMainFrameTranslationEditorMenu
{
public:

	// Handles clicking a menu entry.
	static void HandleOpenTranslationPicker()
	{
		FModuleManager::Get().LoadModuleChecked("TranslationEditor");
		ITranslationEditor::OpenTranslationPicker();
	}

};


#undef LOCTEXT_NAMESPACE
