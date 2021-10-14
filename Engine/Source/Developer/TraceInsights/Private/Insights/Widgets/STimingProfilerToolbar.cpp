// Copyright Epic Games, Inc. All Rights Reserved.

#include "STimingProfilerToolbar.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/SBoxPanel.h"

// Insights
#include "Insights/InsightsCommands.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/TimingProfilerCommands.h"

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
	ToolbarBuilder.SetStyle(&FInsightsStyle::Get(), "PrimaryToolbar");
	Local::FillViewToolbar(ToolbarBuilder);

	FSlimHorizontalToolBarBuilder RightSideToolbarBuilder(CommandList.ToSharedRef(), FMultiBoxCustomization::None);
	RightSideToolbarBuilder.SetStyle(&FInsightsStyle::Get(), "PrimaryToolbar");
	Local::FillRightSideToolbar(RightSideToolbarBuilder);

	ChildSlot
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		.FillWidth(1.0)
		.Padding(0.0f)
		[
			ToolbarBuilder.MakeWidget()
		]

		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(0.0f)
		[
			RightSideToolbarBuilder.MakeWidget()
		]
	];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
