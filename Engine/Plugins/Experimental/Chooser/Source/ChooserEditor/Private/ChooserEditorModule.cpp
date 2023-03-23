// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChooserEditorModule.h"
#include "IAssetTools.h"
#include "ChooserTableEditor.h"
#include "BoolColumnEditor.h"
#include "EnumColumnEditor.h"
#include "FloatRangeColumnEditor.h"
#include "GameplayTagColumnEditor.h"
#include "ObjectColumnEditor.h"
#include "ChooserTableEditorCommands.h"
#include "ChooserPropertyAccess.h"
#include "PropertyEditorModule.h"

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
	RegisterObjectWidgets();
	
	FChooserTableEditorCommands::Register();

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	
	PropertyModule.RegisterCustomPropertyTypeLayout(FChooserPropertyBinding::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateLambda([] { return MakeShared<FPropertyAccessChainCustomization>(); }));
	PropertyModule.RegisterCustomPropertyTypeLayout(FChooserEnumPropertyBinding::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateLambda([] { return MakeShared<FPropertyAccessChainCustomization>(); }));

}

void FModule::ShutdownModule()
{
	FChooserTableEditorCommands::Unregister();
}

}

IMPLEMENT_MODULE(UE::ChooserEditor::FModule, ChooserEditor);

#undef LOCTEXT_NAMESPACE