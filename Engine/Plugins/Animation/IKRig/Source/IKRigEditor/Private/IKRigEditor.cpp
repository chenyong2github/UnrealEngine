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
#include "RetargetEditor/AssetTypeActions_RetargetPose.h"
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
	IAssetTools& ToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	//
	IKRigDefinitionAssetAction = MakeShareable(new FAssetTypeActions_IKRigDefinition);
	ToolsModule.RegisterAssetTypeActions(IKRigDefinitionAssetAction.ToSharedRef());
	//
	IKRetargeterAssetAction = MakeShareable(new FAssetTypeActions_IKRetargeter);
	ToolsModule.RegisterAssetTypeActions(IKRetargeterAssetAction.ToSharedRef());
	//
	RetargetPoseAssetAction = MakeShareable(new FAssetTypeActions_RetargetPose);
	ToolsModule.RegisterAssetTypeActions(RetargetPoseAssetAction.ToSharedRef());

	// extend the content browser menu
	FAssetTypeActions_IKRetargeter::ExtendAnimSequenceToolMenu();

	// register custom editor modes
	FEditorModeRegistry::Get().RegisterMode<FIKRigEditMode>(FIKRigEditMode::ModeName, LOCTEXT("IKRigEditMode", "IKRig"), FSlateIcon(), false);
	FEditorModeRegistry::Get().RegisterMode<FIKRetargetDefaultMode>(FIKRetargetDefaultMode::ModeName, LOCTEXT("IKRetargetDefaultMode", "IKRetargetDefault"), FSlateIcon(), false);
	FEditorModeRegistry::Get().RegisterMode<FIKRetargetEditPoseMode>(FIKRetargetEditPoseMode::ModeName, LOCTEXT("IKRetargetEditMode", "IKRetargetEditPose"), FSlateIcon(), false);

	// register detail customizations
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	// custom IK rig bone widget
	PropertyEditorModule.RegisterCustomClassLayout(UIKRigBoneDetails::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FIKRigGenericDetailCustomization::MakeInstance));
	ClassesToUnregisterOnShutdown.Add(UIKRigBoneDetails::StaticClass()->GetFName());
	// custom IK goal widget
	PropertyEditorModule.RegisterCustomClassLayout(UIKRigEffectorGoal::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FIKRigGenericDetailCustomization::MakeInstance));
	ClassesToUnregisterOnShutdown.Add(UIKRigEffectorGoal::StaticClass()->GetFName());
	// custom retargeter bone widget
	PropertyEditorModule.RegisterCustomClassLayout(UIKRetargetBoneDetails::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FIKRetargetBoneDetailCustomization::MakeInstance));
	ClassesToUnregisterOnShutdown.Add(UIKRetargetBoneDetails::StaticClass()->GetFName());
}

void FIKRigEditor::ShutdownModule()
{
	FIKRigCommands::Unregister();
	FIKRigSkeletonCommands::Unregister();
	FIKRetargetCommands::Unregister();
	
	FEditorModeRegistry::Get().UnregisterMode(FIKRigEditMode::ModeName);
	FEditorModeRegistry::Get().UnregisterMode(FIKRetargetDefaultMode::ModeName);
	FEditorModeRegistry::Get().UnregisterMode(FIKRetargetEditPoseMode::ModeName);

	// unregister asset actions
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& ToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		
		if (IKRigDefinitionAssetAction.IsValid())
		{
			ToolsModule.UnregisterAssetTypeActions(IKRigDefinitionAssetAction.ToSharedRef());
		}
		
		if (IKRetargeterAssetAction.IsValid())
		{
			ToolsModule.UnregisterAssetTypeActions(IKRetargeterAssetAction.ToSharedRef());
		}
		
		if (RetargetPoseAssetAction.IsValid())
		{
			ToolsModule.UnregisterAssetTypeActions(RetargetPoseAssetAction.ToSharedRef());
		}
	}
}

#undef LOCTEXT_NAMESPACE