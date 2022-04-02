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
#include "RetargetEditor/IKRetargetCommands.h"
#include "RetargetEditor/IKRetargetDefaultMode.h"
#include "RetargetEditor/IKRetargetDetails.h"
#include "RetargetEditor/IKRetargetEditPoseMode.h"
#include "RigEditor/IKRigCommands.h"
#include "RigEditor/IKRigEditMode.h"
#include "RigEditor/IKRigSkeletonCommands.h"
#include "RigEditor/IKRigDetailCustomizations.h"
#include "RigEditor/IKRigEditorController.h"

DEFINE_LOG_CATEGORY(LogIKRigEditor);

IMPLEMENT_MODULE(FIKRigEditor, IKRigEditor)

#define LOCTEXT_NAMESPACE "IKRigEditor"

void FIKRigEditor::StartupModule()
{
	// register commands
	FIKRigCommands::Register();
	FIKRigSkeletonCommands::Register();
	FIKRetargetCommands::Register();
	
	// register custom asset type actions
	IKRigDefinitionAssetAction = MakeShareable(new FAssetTypeActions_IKRigDefinition);
	FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().RegisterAssetTypeActions(IKRigDefinitionAssetAction.ToSharedRef());
	//
	IKRetargeterAssetAction = MakeShareable(new FAssetTypeActions_IKRetargeter);
	FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().RegisterAssetTypeActions(IKRetargeterAssetAction.ToSharedRef());

	// extend the content browser menu
	FAssetTypeActions_IKRetargeter::ExtendAnimSequenceToolMenu();

	// register custom editor modes
	FEditorModeRegistry::Get().RegisterMode<FIKRigEditMode>(FIKRigEditMode::ModeName, LOCTEXT("IKRigEditMode", "IKRig"), FSlateIcon(), false);
	FEditorModeRegistry::Get().RegisterMode<FIKRetargetDefaultMode>(FIKRetargetDefaultMode::ModeName, LOCTEXT("IKRetargetDefaultMode", "IKRetargetDefault"), FSlateIcon(), false);
	FEditorModeRegistry::Get().RegisterMode<FIKRetargetEditPoseMode>(FIKRetargetEditPoseMode::ModeName, LOCTEXT("IKRetargetEditMode", "IKRetargetEditPose"), FSlateIcon(), false);

	// register detail customizations
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	ClassesToUnregisterOnShutdown.Add(UIKRigBoneDetails::StaticClass()->GetFName());
	PropertyEditorModule.RegisterCustomClassLayout(ClassesToUnregisterOnShutdown.Last(), FOnGetDetailCustomizationInstance::CreateStatic(&FIKRigGenericDetailCustomization::MakeInstance));
	ClassesToUnregisterOnShutdown.Add(UIKRigEffectorGoal::StaticClass()->GetFName());
	PropertyEditorModule.RegisterCustomClassLayout(ClassesToUnregisterOnShutdown.Last(), FOnGetDetailCustomizationInstance::CreateStatic(&FIKRigGenericDetailCustomization::MakeInstance));
	ClassesToUnregisterOnShutdown.Add(UIKRetargeter::StaticClass()->GetFName());
	PropertyEditorModule.RegisterCustomClassLayout(ClassesToUnregisterOnShutdown.Last(), FOnGetDetailCustomizationInstance::CreateStatic(&FIKRetargeterDetails::MakeInstance));
}

void FIKRigEditor::ShutdownModule()
{
	FIKRigCommands::Unregister();
	FIKRigSkeletonCommands::Unregister();
	FIKRetargetCommands::Unregister();
	
	FEditorModeRegistry::Get().UnregisterMode(FIKRigEditMode::ModeName);
	FEditorModeRegistry::Get().UnregisterMode(FIKRetargetDefaultMode::ModeName);
	FEditorModeRegistry::Get().UnregisterMode(FIKRetargetEditPoseMode::ModeName);

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