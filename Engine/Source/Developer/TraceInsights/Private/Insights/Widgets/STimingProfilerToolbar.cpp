// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "STimingProfilerToolbar.h"

#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"

// Insights
#include "Insights/InsightsCommands.h"
#include "Insights/InsightsManager.h"
#include "Insights/TimingProfilerCommands.h"
#include "Insights/TimingProfilerManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "STimingProfilerToolbar"

////////////////////////////////////////////////////////////////////////////////////////////////////

STimingProfilerToolbar::STimingProfilerToolbar()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

STimingProfilerToolbar::~STimingProfilerToolbar()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerToolbar::Construct(const FArguments& InArgs)
{
	CreateCommands();

	struct Local
	{
		static void FillToolbar1(FToolBarBuilder& ToolbarBuilder)
		{
			//ToolbarBuilder.BeginSection("Session");
			//{
			//	ToolbarBuilder.AddToolBarButton(FInsightsCommands::Get().InsightsManager_Live);
			//	ToolbarBuilder.AddToolBarButton(FInsightsCommands::Get().InsightsManager_Load);
			//}
			//ToolbarBuilder.EndSection();
			ToolbarBuilder.BeginSection("View");
			{
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleFramesTrackVisibility);
				//ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleGraphTrackVisibility);
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleTimingViewVisibility);
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleTimersViewVisibility);
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleStatsCountersViewVisibility);
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleLogViewVisibility);
			}
			ToolbarBuilder.EndSection();
			//ToolbarBuilder.BeginSection("Options");
			//{
			//	ToolbarBuilder.AddToolBarButton(FInsightsCommands::Get().OpenSettings);
			//}
			//ToolbarBuilder.EndSection();
		}

		static void FillToolbar2(FToolBarBuilder& ToolbarBuilder)
		{
			ToolbarBuilder.BeginSection("Options");
			{
				ToolbarBuilder.AddToolBarButton(FInsightsCommands::Get().ToggleDebugInfo);
			}
			ToolbarBuilder.EndSection();
		}
	};

	TSharedPtr<FUICommandList> CommandList = FInsightsManager::Get()->GetCommandList();

	FToolBarBuilder ToolbarBuilder1(CommandList.ToSharedRef(), FMultiBoxCustomization::None);
	Local::FillToolbar1(ToolbarBuilder1);

	FToolBarBuilder ToolbarBuilder2(CommandList.ToSharedRef(), FMultiBoxCustomization::None);
	Local::FillToolbar2(ToolbarBuilder2);

	// Create the tool bar!
	ChildSlot
	[
		SNew(SHorizontalBox)

		+SHorizontalBox::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.FillWidth(1.0)
		.Padding(0.0f)
		[
			SNew(SBorder)
			.Padding(0)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
			.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
			[
				ToolbarBuilder1.MakeWidget()
			]
		]

		+SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.FillWidth(1.0)
		.Padding(0.0f)
		[
			SNew(SBorder)
			.Padding(0)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
			.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
			[
				ToolbarBuilder2.MakeWidget()
			]
		]
	];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerToolbar::ShowStats()
{
	// do nothing
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerToolbar::ShowMemory()
{
	// do nothing
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerToolbar::CreateCommands()
{
	//TSharedPtr<FUICommandList> ProfilerCommandList = FTimingProfilerManager::Get()->GetCommandList();
	//const FTimingProfilerCommands& Commands = FTimingProfilerCommands::Get();
	//
	//// Stats command
	//ProfilerCommandList->MapAction(Commands.StatsProfiler,
	//	FExecuteAction::CreateRaw(this, &STimingProfilerToolbar::ShowStats),
	//	FCanExecuteAction(),
	//	FIsActionChecked::CreateRaw(this, &STimingProfilerToolbar::IsShowingStats)
	//);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
