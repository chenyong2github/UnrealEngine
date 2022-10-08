// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChooserEditorModule.h"
#include "IAssetTools.h"
#include "AssetTypeActions_Chooser.h"
#include "ChooserTableEditor.h"

#define LOCTEXT_NAMESPACE "ChooserEditorModule"

namespace UE::ChooserEditor
{

void FModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTypeActions_ChooserTable = MakeShared<FAssetTypeActions_ChooserTable>();
	AssetTools.RegisterAssetTypeActions(AssetTypeActions_ChooserTable.ToSharedRef());
	FChooserTableEditor::RegisterWidgets();
	
	InterfacePropertyTypeIdentifier = MakeShared<UE::ChooserEditor::FPropertyTypeIdentifier>();
	
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FInterfaceProperty::StaticClass()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateLambda([] { return MakeShared<UE::ChooserEditor::FInterfacePropertyTypeCustomization>(UChooserParameterBool::StaticClass()); }),
		InterfacePropertyTypeIdentifier);
}
		 

void FModule::ShutdownModule()
{
	if(FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		AssetTools.UnregisterAssetTypeActions(AssetTypeActions_ChooserTable.ToSharedRef());
	}
}

}

IMPLEMENT_MODULE(UE::ChooserEditor::FModule, ChooserEditor);

#undef LOCTEXT_NAMESPACE