// Copyright Epic Games, Inc. All Rights Reserved.

#include "STimingProfilerToolbar.h"

#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"

// Insights
#include "Insights/InsightsCommands.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
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
	struct Local
	{
		static void FillViewToolbar(FToolBarBuilder& ToolbarBuilder)
		{
			ToolbarBuilder.BeginSection("View");
			{
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleFramesTrackVisibility,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(),
					FSlateIcon(FInsightsStyle::GetStyleSetName(), "FramesTrack.Icon.Large"));
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleTimingViewVisibility,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(),
					FSlateIcon(FInsightsStyle::GetStyleSetName(), "TimingView.Icon.Large"));
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleTimersViewVisibility,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(),
					FSlateIcon(FInsightsStyle::GetStyleSetName(), "TimersView.Icon.Large"));
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleCallersTreeViewVisibility,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(),
					FSlateIcon(FInsightsStyle::GetStyleSetName(), "TimersView.Icon.Large"));
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleCalleesTreeViewVisibility,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(),
					FSlateIcon(FInsightsStyle::GetStyleSetName(), "TimersView.Icon.Large"));
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleStatsCountersViewVisibility,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(),
					FSlateIcon(FInsightsStyle::GetStyleSetName(), "StatsCountersView.Icon.Large"));
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleLogViewVisibility,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(),
					FSlateIcon(FInsightsStyle::GetStyleSetName(), "LogView.Icon.Large"));
			}
			ToolbarBuilder.EndSection();
		}

		static void FillRightSideToolbar(FToolBarBuilder& ToolbarBuilder)
		{
			ToolbarBuilder.BeginSection("Debug");
			{
				ToolbarBuilder.AddToolBarButton(FInsightsCommands::Get().ToggleDebugInfo,
					NAME_None, FText(), TAttribute<FText>(),
					FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icon.Bug"));
			}
			ToolbarBuilder.EndSection();
		}
	};

	TSharedPtr<FUICommandList> CommandList = FInsightsManager::Get()->GetCommandList();

	FSlimHorizontalToolBarBuilder ToolbarBuilder(CommandList.ToSharedRef(), FMultiBoxCustomization::None);
	Local::FillViewToolbar(ToolbarBuilder);

	FSlimHorizontalToolBarBuilder RightSideToolbarBuilder(CommandList.ToSharedRef(), FMultiBoxCustomization::None);
	Local::FillRightSideToolbar(RightSideToolbarBuilder);

	// Create the tool bar!
	ChildSlot
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
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
				ToolbarBuilder.MakeWidget()
			]
		]

		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(0.0f)
		[
			SNew(SBorder)
			.Padding(0)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
			.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
			[
				RightSideToolbarBuilder.MakeWidget()
			]
		]
	];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
