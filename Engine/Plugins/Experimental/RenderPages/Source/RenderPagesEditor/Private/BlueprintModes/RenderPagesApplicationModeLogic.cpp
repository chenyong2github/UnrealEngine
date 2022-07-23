// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintModes/RenderPagesApplicationModeLogic.h"
#include "BlueprintModes/RenderPagesApplicationModes.h"
#include "IRenderPageCollectionEditor.h"

#include "BlueprintEditorTabs.h"
#include "SBlueprintEditorToolbar.h"
#include "Toolkit/RenderPageCollectionEditorToolbar.h"

#define LOCTEXT_NAMESPACE "RenderPagesLogicMode"


UE::RenderPages::Private::FRenderPagesApplicationModeLogic::FRenderPagesApplicationModeLogic(TSharedPtr<IRenderPageCollectionEditor> InRenderPagesEditor)
	: FRenderPagesApplicationModeBase(InRenderPagesEditor, FRenderPagesApplicationModes::LogicMode)
{
	// Override the default created category here since "Logic Editor" sounds awkward
	WorkspaceMenuCategory = FWorkspaceItem::NewGroup(LOCTEXT("WorkspaceMenu_RenderPagesLogic", "Render Pages Logic"));

	TabLayout = FTabManager::NewLayout("RenderPagesBlueprintEditor_Logic_Layout_v1")
		->AddArea(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split(
				FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
				->Split(
					FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.15f)
					->Split(
						FTabManager::NewStack()->SetSizeCoefficient(0.5f)
						->AddTab(FBlueprintEditorTabs::MyBlueprintID, ETabState::OpenedTab)
					)
					->Split(
						FTabManager::NewStack()->SetSizeCoefficient(0.5f)
						->AddTab(FBlueprintEditorTabs::DetailsID, ETabState::OpenedTab)
					)
				)
				->Split(
					FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.70f)
					->Split(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.80f)
						->AddTab("Document", ETabState::ClosedTab)
					)
					->Split(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.20f)
						->AddTab(FBlueprintEditorTabs::CompilerResultsID, ETabState::ClosedTab)
						->AddTab(FBlueprintEditorTabs::FindResultsID, ETabState::ClosedTab)
					)
				)
				->Split(
					FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.15f)
					->Split(
						FTabManager::NewStack()
						->AddTab(FBlueprintEditorTabs::PaletteID, ETabState::ClosedTab)
					)
				)
			)
		);

	//Make sure we start with new one
	ToolbarExtender = MakeShareable(new FExtender);

	InRenderPagesEditor->GetRenderPagesToolbarBuilder()->AddRenderPagesBlueprintEditorModesToolbar(ToolbarExtender);

	if (UToolMenu* Toolbar = InRenderPagesEditor->RegisterModeToolbarIfUnregistered(GetModeName()))
	{
		InRenderPagesEditor->GetRenderPagesToolbarBuilder()->AddLogicModeToolbar(Toolbar);

		InRenderPagesEditor->GetToolbarBuilder()->AddCompileToolbar(Toolbar);
		InRenderPagesEditor->GetToolbarBuilder()->AddScriptingToolbar(Toolbar);
		// disabled: InRenderPagesEditor->GetToolbarBuilder()->AddBlueprintGlobalOptionsToolbar(Toolbar);
	}
}

void UE::RenderPages::Private::FRenderPagesApplicationModeLogic::RegisterTabFactories(TSharedPtr<FTabManager> InTabManager)
{
	TSharedPtr<FBlueprintEditor> BP = GetBlueprintEditor();

	BP->RegisterToolbarTab(InTabManager.ToSharedRef());
	BP->PushTabFactories(CoreTabFactories);
	BP->PushTabFactories(BlueprintEditorTabFactories);
	BP->PushTabFactories(TabFactories);
}

void UE::RenderPages::Private::FRenderPagesApplicationModeLogic::PreDeactivateMode()
{
	// prevents: FRenderPagesApplicationModeBase::PreDeactivateMode();
}

void UE::RenderPages::Private::FRenderPagesApplicationModeLogic::PostActivateMode()
{
	FRenderPagesApplicationModeBase::PostActivateMode();
}


#undef LOCTEXT_NAMESPACE
