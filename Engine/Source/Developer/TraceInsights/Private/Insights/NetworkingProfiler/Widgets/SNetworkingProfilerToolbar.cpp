// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNetworkingProfilerToolbar.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/SBoxPanel.h"

// Insights
#include "Insights/InsightsCommands.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/NetworkingProfiler/NetworkingProfilerCommands.h"
#include "Insights/NetworkingProfiler/Widgets/SNetworkingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SNetworkingProfilerToolbar"

////////////////////////////////////////////////////////////////////////////////////////////////////

SNetworkingProfilerToolbar::SNetworkingProfilerToolbar()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SNetworkingProfilerToolbar::~SNetworkingProfilerToolbar()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SNetworkingProfilerToolbar::Construct(const FArguments& InArgs, TSharedPtr<SNetworkingProfilerWindow> InProfilerWindow)
{
	ProfilerWindow = InProfilerWindow;

	struct Local
	{
		static void FillViewToolbar(TSharedPtr<SNetworkingProfilerWindow> ProfilerWindow, FToolBarBuilder& ToolbarBuilder)
		{
			ToolbarBuilder.BeginSection("View");
			{
				ToolbarBuilder.AddToolBarButton(FNetworkingProfilerCommands::Get().TogglePacketViewVisibility,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(),
					FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.PacketView.ToolBar"));
				ToolbarBuilder.AddToolBarButton(FNetworkingProfilerCommands::Get().TogglePacketContentViewVisibility,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(),
					FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.PacketContentView.ToolBar"));
				ToolbarBuilder.AddToolBarButton(FNetworkingProfilerCommands::Get().ToggleNetStatsViewVisibility,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(),
					FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.NetStatsView.ToolBar"));
			}
			ToolbarBuilder.EndSection();
			ToolbarBuilder.BeginSection("Connection");
			{
				TSharedRef<SWidget> GameInstanceWidget = ProfilerWindow->CreateGameInstanceComboBox();
				ToolbarBuilder.AddWidget(GameInstanceWidget);

				TSharedRef<SWidget> ConnectionWidget = ProfilerWindow->CreateConnectionComboBox();
				ToolbarBuilder.AddWidget(ConnectionWidget);

				TSharedRef<SWidget> ConnectionModeWidget = ProfilerWindow->CreateConnectionModeComboBox();
				ToolbarBuilder.AddWidget(ConnectionModeWidget);
			}
			ToolbarBuilder.EndSection();
			//ToolbarBuilder.BeginSection("New");
			//{
			//	//TODO: ToolbarBuilder.AddToolBarButton(FNetworkingProfilerCommands::Get().OpenNewWindow,
			//		NAME_None, TAttribute<FText>(), TAttribute<FText>(),
			//		FSlateIcon(FInsightsStyle::GetStyleSetName(), "NewNetworkingInsights.Icon"));
			//}
			//ToolbarBuilder.EndSection();
		}

		static void FillRightSideToolbar(FToolBarBuilder& ToolbarBuilder)
		{
			ToolbarBuilder.BeginSection("Debug");
			{
				ToolbarBuilder.AddToolBarButton(FInsightsCommands::Get().ToggleDebugInfo,
					NAME_None, FText(), TAttribute<FText>(),
					FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.Debug.ToolBar"));
			}
			ToolbarBuilder.EndSection();
		}
	};

	TSharedPtr<FUICommandList> CommandList = ProfilerWindow->GetCommandList();

	FSlimHorizontalToolBarBuilder ToolbarBuilder(CommandList.ToSharedRef(), FMultiBoxCustomization::None);
	ToolbarBuilder.SetStyle(&FInsightsStyle::Get(), "PrimaryToolbar");
	Local::FillViewToolbar(ProfilerWindow, ToolbarBuilder);

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
