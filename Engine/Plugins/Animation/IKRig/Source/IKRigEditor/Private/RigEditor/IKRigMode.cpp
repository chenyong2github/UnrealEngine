// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditor/IKRigMode.h"
#include "RigEditor/IKRigToolkit.h"
#include "IPersonaPreviewScene.h"
#include "PersonaModule.h"
#include "ISkeletonEditorModule.h"
#include "Modules/ModuleManager.h"
#include "PersonaTabs.h"
#include "RigEditor/IKRigSkeletonTabSummoner.h"
#include "RigEditor/IKRigSolverStackTabSummoner.h"

#define LOCTEXT_NAMESPACE "IKRigMode"

static const FName IKRigSolverStackName("IKRigSolverStack");

FIKRigMode::FIKRigMode(
	TSharedRef<FWorkflowCentricApplication> InHostingApp,  
	TSharedRef<IPersonaPreviewScene> InPreviewScene)
	: FApplicationMode(IKRigEditorModes::IKRigEditorMode)
{
	IKRigEditorPtr = StaticCastSharedRef<FIKRigEditorToolkit>(InHostingApp);
	TSharedRef<FIKRigEditorToolkit> IKRigEditor = StaticCastSharedRef<FIKRigEditorToolkit>(InHostingApp);

	FPersonaViewportArgs ViewportArgs(InPreviewScene);
	ViewportArgs.bAlwaysShowTransformToolbar = true;
	ViewportArgs.bShowStats = false;
	ViewportArgs.bShowTurnTable = false;
	ViewportArgs.ContextName = TEXT("IKRigEditor.Viewport");

	// register Persona tabs
	FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");
	TabFactories.RegisterFactory(PersonaModule.CreatePersonaViewportTabFactory(InHostingApp, ViewportArgs));
	TabFactories.RegisterFactory(PersonaModule.CreateAdvancedPreviewSceneTabFactory(InHostingApp, InPreviewScene));
	TabFactories.RegisterFactory(PersonaModule.CreateDetailsTabFactory(InHostingApp, FOnDetailsCreated::CreateSP(&IKRigEditor.Get(), &FIKRigEditorToolkit::HandleDetailsCreated)));

	// register custom tabs
	TabFactories.RegisterFactory(MakeShared<FIKRigSkeletonTabSummoner>(IKRigEditor));
	TabFactories.RegisterFactory(MakeShared<FIKRigSolverStackTabSummoner>(IKRigEditor));

	// create tab layout
	TabLayout = FTabManager::NewLayout("Standalone_IKRigEditor_Layout_v1.118")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewSplitter()
				->SetSizeCoefficient(0.9f)
				->SetOrientation(Orient_Horizontal)
				->Split
				(
				    FTabManager::NewSplitter()
				    ->SetSizeCoefficient(0.2f)
				    ->SetOrientation(Orient_Vertical)
				    ->Split
				    (
					    FTabManager::NewStack()
					    ->SetSizeCoefficient(0.6f)
					    ->AddTab(FIKRigSkeletonTabSummoner::TabID, ETabState::OpenedTab)
					)
					->Split
					(
					    FTabManager::NewStack()
					    ->SetSizeCoefficient(0.4f)
					    ->AddTab(IKRigSolverStackName, ETabState::OpenedTab)
					)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.6f)
					->SetHideTabWell(true)
					->AddTab(FPersonaTabs::PreviewViewportID, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
                    ->SetSizeCoefficient(0.6f)
                    ->AddTab(FPersonaTabs::DetailsID, ETabState::OpenedTab)
                    ->AddTab(FPersonaTabs::AdvancedPreviewSceneSettingsID, ETabState::OpenedTab)
                    ->SetForegroundTab(FPersonaTabs::DetailsID)
				)
			)
		);

	PersonaModule.OnRegisterTabs().Broadcast(TabFactories, InHostingApp);
	LayoutExtender = MakeShared<FLayoutExtender>();
	PersonaModule.OnRegisterLayoutExtensions().Broadcast(*LayoutExtender.Get());
	TabLayout->ProcessExtensions(*LayoutExtender.Get());
}

void FIKRigMode::RegisterTabFactories(TSharedPtr<FTabManager> InTabManager)
{
	TSharedPtr<FIKRigEditorToolkit> IKRigEditor = IKRigEditorPtr.Pin();
	IKRigEditor->RegisterTabSpawners(InTabManager.ToSharedRef());
	IKRigEditor->PushTabFactories(TabFactories);
	FApplicationMode::RegisterTabFactories(InTabManager);
}

#undef LOCTEXT_NAMESPACE
