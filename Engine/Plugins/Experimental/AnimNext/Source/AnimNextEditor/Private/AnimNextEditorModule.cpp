// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextEditorModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Graph/AssetTypeActions.h"
#include "Graph/PropertyTypeCustomization.h"
#include "Param/ParamTypePropertyCustomization.h"
#include "Param/ParameterPickerArgs.h"
#include "Param/SParameterPicker.h"
#include "Param/ParamType.h"

namespace UE::AnimNext::Editor
{

void FModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTypeActions_AnimNextGraph = MakeShared<FAssetTypeActions_AnimNextGraph>();
	AssetTools.RegisterAssetTypeActions(AssetTypeActions_AnimNextGraph.ToSharedRef());

	AnimNextPropertyTypeIdentifier = MakeShared<FPropertyTypeIdentifier>();
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout(
		"InterfaceProperty",
		FOnGetPropertyTypeCustomizationInstance::CreateLambda([] { return MakeShared<FPropertyTypeCustomization>(); }),
		AnimNextPropertyTypeIdentifier);

	PropertyModule.RegisterCustomPropertyTypeLayout(
		"AnimNextParamType",
		FOnGetPropertyTypeCustomizationInstance::CreateLambda([] { return MakeShared<FParamTypePropertyTypeCustomization>(); }));
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
		PropertyModule.UnregisterCustomPropertyTypeLayout("InterfaceProperty", AnimNextPropertyTypeIdentifier);
		PropertyModule.UnregisterCustomPropertyTypeLayout("AnimNextParamType");
	}
}

TSharedPtr<SWidget> FModule::CreateParameterPicker(const FParameterPickerArgs& InArgs)
{
	return SNew(SParameterPicker)
		.Args(InArgs);
}

}

IMPLEMENT_MODULE(UE::AnimNext::Editor::FModule, AnimNextEditor);