// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

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
#include "IKRigDefinition.h"

IMPLEMENT_MODULE(FIKRigEditor, IKRigEditor)

DEFINE_LOG_CATEGORY_STATIC(LogIKRigEditor, Log, All);

#define LOCTEXT_NAMESPACE "IKRigEditor"

void FIKRigEditor::StartupModule()
{
	IKRigDefinitionAssetAction = MakeShareable(new FAssetTypeActions_IKRigDefinition);
	FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().RegisterAssetTypeActions(IKRigDefinitionAssetAction.ToSharedRef());

	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(UIKRigDefinition::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FIKRigDefinitionDetails::MakeInstance));

//	FEditorModeRegistry::Get().RegisterMode<FIKRigEditMode>(UAnimGraphNode_IKRig::AnimModeName, LOCTEXT("IKRigEditMode", "IKRig"), FSlateIcon(), false);
}

void FIKRigEditor::ShutdownModule()
{
	//	FEditorModeRegistry::Get().UnregisterMode(UAnimGraphNode_IKRig::AnimModeName);

	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.UnregisterCustomClassLayout(UIKRigDefinition::StaticClass()->GetFName());

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