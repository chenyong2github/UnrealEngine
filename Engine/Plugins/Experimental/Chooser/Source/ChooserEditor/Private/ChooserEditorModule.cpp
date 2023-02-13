// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChooserEditorModule.h"
#include "IAssetTools.h"
#include "ChooserTableEditor.h"
#include "BoolColumnEditor.h"
#include "EnumColumnEditor.h"
#include "FloatRangeColumnEditor.h"
#include "GameplayTagColumnEditor.h"
#include "ChooserTableEditorCommands.h"

#define LOCTEXT_NAMESPACE "ChooserEditorModule"

namespace UE::ChooserEditor
{

void FModule::StartupModule()
{
	FChooserTableEditor::RegisterWidgets();
	RegisterGameplayTagWidgets();
	RegisterFloatRangeWidgets();
	RegisterBoolWidgets();
	RegisterEnumWidgets();
	
	FChooserTableEditorCommands::Register();

}

void FModule::ShutdownModule()
{
	FChooserTableEditorCommands::Unregister();
}

}

IMPLEMENT_MODULE(UE::ChooserEditor::FModule, ChooserEditor);

#undef LOCTEXT_NAMESPACE