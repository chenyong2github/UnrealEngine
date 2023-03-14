// Copyright Epic Games, Inc. All Rights Reserved.

#include "GraphEditorModule.h"
#include "IAssetTools.h"
#include "AssetTypeActions.h"
#include "PropertyTypeCustomization.h"

namespace UE::AnimNext::GraphEditor
{

void FModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTypeActions_AnimNextGraph = MakeShared<FAssetTypeActions_AnimNextGraph>();
	AssetTools.RegisterAssetTypeActions(AssetTypeActions_AnimNextGraph.ToSharedRef());

	AnimNextPropertyTypeIdentifier = MakeShared<FPropertyTypeIdentifier>();
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FInterfaceProperty::StaticClass()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateLambda([] { return MakeShared<FPropertyTypeCustomization>(); }),
		AnimNextPropertyTypeIdentifier);
}

void FModule::ShutdownModule()
{
	if(FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		AssetTools.UnregisterAssetTypeActions(AssetTypeActions_AnimNextGraph.ToSharedRef());
	}
	
	if(FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomPropertyTypeLayout(FInterfaceProperty::StaticClass()->GetFName(), AnimNextPropertyTypeIdentifier);
	}
}

}

IMPLEMENT_MODULE(UE::AnimNext::GraphEditor::FModule, AnimNextGraphEditor);