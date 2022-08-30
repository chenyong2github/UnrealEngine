// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusToolsModule.h"

#include "OptimusToolsCommands.h"
#include "OptimusToolsSkelMeshEditorMode.h"
#include "OptimusToolsStyle.h"

#include "EditorModeManager.h"
#include "EditorModeRegistry.h"
#include "ISkeletalMeshEditor.h"
#include "ISkeletalMeshEditorModule.h"
#include "Modules/ModuleManager.h"
#include "SkeletalMeshToolMenuContext.h"
#include "Toolkits/BaseToolkit.h"
#include "ToolMenus.h"
#include "WorkflowOrientedApp/ApplicationMode.h"


#define LOCTEXT_NAMESPACE "OptimusToolsModule"

DEFINE_LOG_CATEGORY(LogOptimusTools);

IMPLEMENT_MODULE(FOptimusToolsModule, OptimusTools);



void FOptimusToolsModule::StartupModule()
{
	FOptimusToolsStyle::Register();
	FOptimusToolsCommands::Register();

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FOptimusToolsModule::RegisterMenusAndToolbars));

	ISkeletalMeshEditorModule& SkelMeshEditorModule = FModuleManager::Get().LoadModuleChecked<ISkeletalMeshEditorModule>("SkeletalMeshEditor");
	TArray<ISkeletalMeshEditorModule::FSkeletalMeshEditorToolbarExtender>& ToolbarExtenders = SkelMeshEditorModule.GetAllSkeletalMeshEditorToolbarExtenders();

	ToolbarExtenders.Add(ISkeletalMeshEditorModule::FSkeletalMeshEditorToolbarExtender::CreateRaw(this, &FOptimusToolsModule::ExtendSkelMeshEditorToolbar));
	SkelMeshEditorExtenderHandle = ToolbarExtenders.Last().GetHandle();
}

void FOptimusToolsModule::ShutdownModule()
{
	ISkeletalMeshEditorModule* SkelMeshEditorModule = FModuleManager::GetModulePtr<ISkeletalMeshEditorModule>("SkeletalMeshEditor");
	if (SkelMeshEditorModule)
	{
		TArray<ISkeletalMeshEditorModule::FSkeletalMeshEditorToolbarExtender>& Extenders = SkelMeshEditorModule->GetAllSkeletalMeshEditorToolbarExtenders();

		Extenders.RemoveAll([=](const ISkeletalMeshEditorModule::FSkeletalMeshEditorToolbarExtender& InDelegate) { return InDelegate.GetHandle() == SkelMeshEditorExtenderHandle; });
	}

	UToolMenus::UnregisterOwner(this);

	FOptimusToolsCommands::Unregister();
	FOptimusToolsStyle::Unregister();
}


void FOptimusToolsModule::RegisterMenusAndToolbars()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu("AssetEditor.SkeletalMeshEditor.ToolBar");
	{
		FToolMenuSection& Section = Toolbar->FindOrAddSection("SkeletalMesh");
		Section.AddDynamicEntry("ToggleModelingToolsMode", FNewToolMenuSectionDelegate::CreateLambda([this](FToolMenuSection& InSection) {
			USkeletalMeshToolMenuContext* Context = InSection.FindContext<USkeletalMeshToolMenuContext>();
			if (Context && Context->SkeletalMeshEditor.IsValid())
			{
				InSection.AddEntry(FToolMenuEntry::InitToolBarButton(
				    FOptimusToolsCommands::Get().ToggleModelingToolsMode,
					LOCTEXT("SkeletalMeshEditorModelingMode", "Modeling Tools"),
				    LOCTEXT("SkeletalMeshEditorModelingModeTooltip", "Opens the Modeling Tools palette that provides selected mesh modification tools."),
				    FSlateIcon("ModelingToolsStyle", "LevelEditor.ModelingToolsMode")));
			}
		}));
	}
}


TSharedRef<FExtender> FOptimusToolsModule::ExtendSkelMeshEditorToolbar(const TSharedRef<FUICommandList> InCommandList, TSharedRef<ISkeletalMeshEditor> InSkeletalMeshEditor)
{
	// Add toolbar extender
	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);

	TWeakPtr<ISkeletalMeshEditor> Ptr(InSkeletalMeshEditor);

	InCommandList->MapAction(FOptimusToolsCommands::Get().ToggleModelingToolsMode,
	    FExecuteAction::CreateRaw(this, &FOptimusToolsModule::OnToggleModelingToolsMode, Ptr),
	    FCanExecuteAction(),
	    FIsActionChecked::CreateRaw(this, &FOptimusToolsModule::IsModelingToolModeActive, Ptr));

	return ToolbarExtender.ToSharedRef();
}


bool FOptimusToolsModule::IsModelingToolModeActive(TWeakPtr<ISkeletalMeshEditor> InSkeletalMeshEditor) const
{
	TSharedPtr<ISkeletalMeshEditor> SkeletalMeshEditor = InSkeletalMeshEditor.Pin();
	return SkeletalMeshEditor.IsValid() && SkeletalMeshEditor->GetEditorModeManager().IsModeActive(UOptimusToolsSkelMeshEditorMode::Id);
}


void FOptimusToolsModule::OnToggleModelingToolsMode(TWeakPtr<ISkeletalMeshEditor> InSkeletalMeshEditor)
{
	TSharedPtr<ISkeletalMeshEditor> SkeletalMeshEditor = InSkeletalMeshEditor.Pin();
	if (SkeletalMeshEditor.IsValid())
	{
		if (!IsModelingToolModeActive(InSkeletalMeshEditor))
		{
			SkeletalMeshEditor->GetEditorModeManager().ActivateMode(UOptimusToolsSkelMeshEditorMode::Id, true);
		}
		else
		{
			SkeletalMeshEditor->GetEditorModeManager().ActivateDefaultMode();
		}
	}
}


#undef LOCTEXT_NAMESPACE
