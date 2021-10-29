// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetMode.h"

#include "IPersonaPreviewScene.h"
#include "PersonaModule.h"
#include "Modules/ModuleManager.h"
#include "PersonaTabs.h"
#include "RetargetEditor/IKRetargetAssetBrowserTabSummoner.h"

#include "RetargetEditor/IKRetargetChainTabSummoner.h"
#include "RetargetEditor/IKRetargetEditor.h"

#define LOCTEXT_NAMESPACE "IKRetargetMode"


FIKRetargetMode::FIKRetargetMode(
	TSharedRef<FWorkflowCentricApplication> InHostingApp,  
	TSharedRef<IPersonaPreviewScene> InPreviewScene)
	: FApplicationMode(IKRetargetEditorModes::IKRetargetEditorMode)
{
	IKRetargetEditorPtr = StaticCastSharedRef<FIKRetargetEditor>(InHostingApp);
	TSharedRef<FIKRetargetEditor> IKRetargetEditor = StaticCastSharedRef<FIKRetargetEditor>(InHostingApp);

	FPersonaViewportArgs ViewportArgs(InPreviewScene);
	ViewportArgs.bAlwaysShowTransformToolbar = true;
	ViewportArgs.bShowStats = false;
	ViewportArgs.bShowTurnTable = false;
	ViewportArgs.ContextName = TEXT("IKRetargetEditor.Viewport");

	// register Persona tabs
	FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");
	TabFactories.RegisterFactory(PersonaModule.CreatePersonaViewportTabFactory(InHostingApp, ViewportArgs));
	TabFactories.RegisterFactory(PersonaModule.CreateDetailsTabFactory(InHostingApp, FOnDetailsCreated::CreateSP(&IKRetargetEditor.Get(), &FIKRetargetEditor::HandleDetailsCreated)));

	// register custom tabs
	TabFactories.RegisterFactory(MakeShared<FIKRetargetChainTabSummoner>(IKRetargetEditor));
	TabFactories.RegisterFactory(MakeShared<FIKRetargetAssetBrowserTabSummoner>(IKRetargetEditor));

	// create tab layout
	TabLayout = FTabManager::NewLayout("Standalone_IKRetargetEditor_Layout_v1.007")
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
					FTabManager::NewStack()
					->SetSizeCoefficient(0.6f)
					->SetHideTabWell(true)
					->AddTab(FPersonaTabs::PreviewViewportID, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewSplitter()
					->SetSizeCoefficient(0.9f)
					->SetOrientation(Orient_Vertical)
					->Split
					(
					FTabManager::NewStack()
						->SetSizeCoefficient(0.6f)
						->AddTab(FPersonaTabs::DetailsID, ETabState::OpenedTab)
						->SetForegroundTab(FPersonaTabs::DetailsID)
					)
					->Split
					(
					FTabManager::NewStack()
						->SetSizeCoefficient(0.6f)
						->AddTab(FIKRetargetChainTabSummoner::TabID, ETabState::OpenedTab)
						->AddTab(FIKRetargetAssetBrowserTabSummoner::TabID, ETabState::OpenedTab)
						->SetForegroundTab(FIKRetargetChainTabSummoner::TabID)
					)
					
				)
			)
		);

	PersonaModule.OnRegisterTabs().Broadcast(TabFactories, InHostingApp);
	LayoutExtender = MakeShared<FLayoutExtender>();
	PersonaModule.OnRegisterLayoutExtensions().Broadcast(*LayoutExtender.Get());
	TabLayout->ProcessExtensions(*LayoutExtender.Get());
}

void FIKRetargetMode::RegisterTabFactories(TSharedPtr<FTabManager> InTabManager)
{
	TSharedPtr<FIKRetargetEditor> IKRigEditor = IKRetargetEditorPtr.Pin();
	IKRigEditor->RegisterTabSpawners(InTabManager.ToSharedRef());
	IKRigEditor->PushTabFactories(TabFactories);
	FApplicationMode::RegisterTabFactories(InTabManager);
}

#undef LOCTEXT_NAMESPACE
