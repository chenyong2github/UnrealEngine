// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintModes/RenderPagesApplicationModeListing.h"
#include "BlueprintModes/RenderPagesApplicationModes.h"

#include "BlueprintEditorSharedTabFactories.h"
#include "BlueprintEditorTabs.h"

#include "IRenderPageCollectionEditor.h"
#include "IRenderPagesEditorModule.h"

#include "SBlueprintEditorToolbar.h"

#include "TabFactory/PageListTabSummoner.h"
#include "TabFactory/CollectionPropertiesTabSummoner.h"
#include "TabFactory/PagePropertiesTabSummoner.h"
#include "TabFactory/PageViewerTabSummoner.h"

#include "Toolkit/RenderPageCollectionEditorToolbar.h"

#define LOCTEXT_NAMESPACE "RenderPagesListingMode"


UE::RenderPages::Private::FRenderPagesApplicationModeListing::FRenderPagesApplicationModeListing(TSharedPtr<IRenderPageCollectionEditor> InRenderPagesEditor)
	: FRenderPagesApplicationModeBase(InRenderPagesEditor, FRenderPagesApplicationModes::ListingMode)
{
	// Override the default created category here since "Listing Editor" sounds awkward
	WorkspaceMenuCategory = FWorkspaceItem::NewGroup(LOCTEXT("WorkspaceMenu_RenderPagesListing", "Render Pages Listing"));

	TabLayout = FTabManager::NewLayout("RenderPagesBlueprintEditor_Listing_Layout_v1_000")
		->AddArea(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split(
				FTabManager::NewSplitter()
				->SetSizeCoefficient(0.5f)
				->SetOrientation(Orient_Horizontal)
				->Split(
					FTabManager::NewStack()
					->SetHideTabWell(true)
					->SetSizeCoefficient(1.f)
					->SetForegroundTab(FPageListTabSummoner::TabID)
					->AddTab(FPageListTabSummoner::TabID, ETabState::OpenedTab)
				)
			)
			->Split(
				FTabManager::NewSplitter()
				->SetSizeCoefficient(0.5f)
				->SetOrientation(Orient_Horizontal)
				->Split(
					FTabManager::NewStack()
					->SetHideTabWell(true)
					->SetSizeCoefficient(0.25f)
					->SetForegroundTab(FCollectionPropertiesTabSummoner::TabID)
					->AddTab(FCollectionPropertiesTabSummoner::TabID, ETabState::OpenedTab)
				)
				->Split(
					FTabManager::NewStack()
					->SetHideTabWell(true)
					->SetSizeCoefficient(0.5f)
					->SetForegroundTab(FPageViewerTabSummoner::TabID)
					->AddTab(FPageViewerTabSummoner::TabID, ETabState::OpenedTab)
				)
				->Split(
					FTabManager::NewStack()
					->SetHideTabWell(true)
					->SetSizeCoefficient(0.25f)
					->SetForegroundTab(FPagePropertiesTabSummoner::TabID)
					->AddTab(FPagePropertiesTabSummoner::TabID, ETabState::OpenedTab)
				)
			)
		);

	// Register Tab Factories
	TabFactories.RegisterFactory(MakeShareable(new FPageListTabSummoner(InRenderPagesEditor)));
	TabFactories.RegisterFactory(MakeShareable(new FCollectionPropertiesTabSummoner(InRenderPagesEditor)));
	TabFactories.RegisterFactory(MakeShareable(new FPageViewerTabSummoner(InRenderPagesEditor)));
	TabFactories.RegisterFactory(MakeShareable(new FPagePropertiesTabSummoner(InRenderPagesEditor)));

	//Make sure we start with our existing list of extenders instead of creating a new one
	IRenderPagesEditorModule& RenderPagesEditorModule = IRenderPagesEditorModule::Get();
	ToolbarExtender = RenderPagesEditorModule.GetToolBarExtensibilityManager()->GetAllExtenders();

	InRenderPagesEditor->GetRenderPagesToolbarBuilder()->AddRenderPagesBlueprintEditorModesToolbar(ToolbarExtender);

	if (UToolMenu* Toolbar = InRenderPagesEditor->RegisterModeToolbarIfUnregistered(GetModeName()))
	{
		InRenderPagesEditor->GetRenderPagesToolbarBuilder()->AddListingModeToolbar(Toolbar);

		InRenderPagesEditor->GetToolbarBuilder()->AddCompileToolbar(Toolbar);
	}
}

void UE::RenderPages::Private::FRenderPagesApplicationModeListing::RegisterTabFactories(TSharedPtr<FTabManager> InTabManager)
{
	TSharedPtr<IRenderPageCollectionEditor> BP = GetBlueprintEditor();

	BP->RegisterToolbarTab(InTabManager.ToSharedRef());
	BP->PushTabFactories(TabFactories);
}

void UE::RenderPages::Private::FRenderPagesApplicationModeListing::PreDeactivateMode()
{
	// prevents: FRenderPagesApplicationModeBase::PreDeactivateMode();
}

void UE::RenderPages::Private::FRenderPagesApplicationModeListing::PostActivateMode()
{
	// prevents: FRenderPagesApplicationModeBase::PostActivateMode();
}


#undef LOCTEXT_NAMESPACE
