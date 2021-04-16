// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigEditor.h"
#include "Features/IModularFeatures.h"
#include "Engine/Engine.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"
#include "AssetToolsModule.h"
#include "PropertyEditorModule.h"
#include "IKRigDefinitionDetails.h"
#include "PropertyEditorDelegates.h"
#include "EditorModeRegistry.h"
#include "AnimGraphNode_IKRig.h"
#include "AssetTypeActions_IKRigDefinition.h"
#include "AssetTypeActions_IKRetargeter.h"
#include "IKRigDefinition.h"
#include "IKRigEditMode.h"

IMPLEMENT_MODULE(FIKRigEditor, IKRigEditor)

DEFINE_LOG_CATEGORY_STATIC(LogIKRigEditor, Log, All);

#define LOCTEXT_NAMESPACE "IKRigEditor"

void FIKRigEditor::StartupModule()
{
	// register IKRigDefinition asset type
	IKRigDefinitionAssetAction = MakeShareable(new FAssetTypeActions_IKRigDefinition);
	FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().RegisterAssetTypeActions(IKRigDefinitionAssetAction.ToSharedRef());

	// register IKRetargeter asset type
	IKRetargeterAssetAction = MakeShareable(new FAssetTypeActions_IKRetargeter);
	FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().RegisterAssetTypeActions(IKRetargeterAssetAction.ToSharedRef());

	// register details panel customization
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(UIKRigDefinition::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FIKRigDefinitionDetails::MakeInstance));

	// register custom editor mode (TBD)
	//FEditorModeRegistry::Get().RegisterMode<FIKRigEditMode>(UAnimGraphNode_IKRig::AnimModeName, LOCTEXT("IKRigEditMode", "IKRig"), FSlateIcon(), false);
}

void FIKRigEditor::ShutdownModule()
{
	FEditorModeRegistry::Get().UnregisterMode(UAnimGraphNode_IKRig::AnimModeName);

	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.UnregisterCustomPropertyTypeLayout("IKRigEffector");

	if (IKRigDefinitionAssetAction.IsValid())
	{
		if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
		{
			FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get().UnregisterAssetTypeActions(IKRigDefinitionAssetAction.ToSharedRef());
		}
		IKRigDefinitionAssetAction.Reset();
	}

}

#undef LOCTEXT_NAMESPACE