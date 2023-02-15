// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieGraphAssetToolkit.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/CoreStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Graph/SMoviePipelineGraphPanel.h"
#include "Widgets/SOverlay.h"

#define LOCTEXT_NAMESPACE "MovieGraphAssetToolkit"

const FName FMovieGraphAssetToolkit::ContentTabId(TEXT("MovieGraphAssetToolkit"));

FMovieGraphAssetToolkit::FMovieGraphAssetToolkit(UAssetEditor* InOwningAssetEditor)
	: FBaseAssetToolkit(InOwningAssetEditor)
{
	// Note: Changes to the layout should include a increment to the layout's ID, i.e.
	// MoviePipelineRenderGraphEditor[X] -> MoviePipelineRenderGraphEditor[X+1]. Otherwise, layouts may be messed up
	// without a full reset to layout defaults inside the editor.
	const FName LayoutString = FName("MoviePipelineRenderGraphEditor1");

	// Override the Default Layout provided by FBaseAssetToolkit to hide the viewport and details panel tabs.
	StandaloneDefaultLayout = FTabManager::NewLayout(FName(LayoutString))
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(1.f)
				->SetHideTabWell(true)
				->AddTab(ContentTabId, ETabState::OpenedTab)
			)
		);
}

void FMovieGraphAssetToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(
		LOCTEXT("WorkspaceMenu_MoviePipelineRenderGraphAssetToolkit", "Movie Pipeline Render Graph Asset Editor"));

	FBaseAssetToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(ContentTabId, FOnSpawnTab::CreateSP(this, &FMovieGraphAssetToolkit::SpawnTab_RenderGraphEditor))
		.SetDisplayName(LOCTEXT("RenderGraphTab", "Render Graph"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FMovieGraphAssetToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(ContentTabId);
}

TSharedRef<SDockTab> FMovieGraphAssetToolkit::SpawnTab_RenderGraphEditor(const FSpawnTabArgs& Args)
{
	// Tabs and Windows have different backgrounds, so we copy the window style to make the 'void' area look similar.
	const FSlateBrush* BackgroundBrush = &FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window").ChildBackgroundBrush;

	TSharedRef<SDockTab> NewDockTab = SNew(SDockTab)
		.TabColorScale(GetTabColorScale())
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SImage)
				.Image(BackgroundBrush)
			]
			+ SOverlay::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				[
					SNew(SMoviePipelineGraphPanel)
				]
			]
		];

	return NewDockTab;
}

FName FMovieGraphAssetToolkit::GetToolkitFName() const
{
	return FName("MovieGraphAssetToolkit");
}

#undef LOCTEXT_NAMESPACE
