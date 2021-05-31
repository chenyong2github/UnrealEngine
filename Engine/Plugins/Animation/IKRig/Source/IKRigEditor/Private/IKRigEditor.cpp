// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigEditor.h"
#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"
#include "AssetToolsModule.h"
#include "PropertyEditorModule.h"
#include "PropertyEditorDelegates.h"
#include "EditorModeRegistry.h"
#include "IKRigDefinition.h"
#include "RigEditor/AssetTypeActions_IKRigDefinition.h"
#include "RetargetEditor/AssetTypeActions_IKRetargeter.h"
#include "RigEditor/IKRigCommands.h"
#include "RigEditor/IKRigEditMode.h"
#include "RigEditor/IKRigSkeletonCommands.h"

IMPLEMENT_MODULE(FIKRigEditor, IKRigEditor)

DEFINE_LOG_CATEGORY_STATIC(LogIKRigEditor, Log, All);

#define LOCTEXT_NAMESPACE "IKRigEditor"

void FIKRigEditor::StartupModule()
{
	FIKRigCommands::Register();
	FIKRigSkeletonCommands::Register();
	
	// register IKRigDefinition asset type
	IKRigDefinitionAssetAction = MakeShareable(new FAssetTypeActions_IKRigDefinition);
	FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().RegisterAssetTypeActions(IKRigDefinitionAssetAction.ToSharedRef());

	// register IKRetargeter asset type
	IKRetargeterAssetAction = MakeShareable(new FAssetTypeActions_IKRetargeter);
	FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().RegisterAssetTypeActions(IKRetargeterAssetAction.ToSharedRef());

	// register custom editor mode
	FEditorModeRegistry::Get().RegisterMode<FIKRigEditMode>(FIKRigEditMode::ModeName, LOCTEXT("IKRigEditMode", "IKRig"), FSlateIcon(), false);
}

void FIKRigEditor::ShutdownModule()
{
	FIKRigCommands::Unregister();
	FIKRigSkeletonCommands::Unregister();
	
	FEditorModeRegistry::Get().UnregisterMode(FIKRigEditMode::ModeName);

	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.UnregisterCustomPropertyTypeLayout("IKRigEffector");

	// unregister IKRigDefinition asset action
	if (IKRigDefinitionAssetAction.IsValid())
	{
		if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
		{
			FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get().UnregisterAssetTypeActions(IKRigDefinitionAssetAction.ToSharedRef());
		}
		IKRigDefinitionAssetAction.Reset();
	}

	// unregister IKRetargeter asset action
	if (IKRetargeterAssetAction.IsValid())
	{
		if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
		{
			FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get().UnregisterAssetTypeActions(IKRetargeterAssetAction.ToSharedRef());
		}
		IKRetargeterAssetAction.Reset();
	}

}

#undef LOCTEXT_NAMESPACE