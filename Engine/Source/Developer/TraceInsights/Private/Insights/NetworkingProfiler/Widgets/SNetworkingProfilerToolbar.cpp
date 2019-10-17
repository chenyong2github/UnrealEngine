// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SNetworkingProfilerToolbar.h"

#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"

// Insights
#include "Insights/InsightsCommands.h"
#include "Insights/InsightsManager.h"
#include "Insights/NetworkingProfiler/NetworkingProfilerCommands.h"
#include "Insights/NetworkingProfiler/NetworkingProfilerManager.h"
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
				ToolbarBuilder.AddToolBarButton(FNetworkingProfilerManager::GetCommands().TogglePacketViewVisibility);
				ToolbarBuilder.AddToolBarButton(FNetworkingProfilerManager::GetCommands().TogglePacketContentViewVisibility);
				ToolbarBuilder.AddToolBarButton(FNetworkingProfilerManager::GetCommands().ToggleNetStatsViewVisibility);
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
			ToolbarBuilder.BeginSection("New");
			{
				//TODO: ToolbarBuilder.AddToolBarButton(FNetworkingProfilerManager::GetCommands().OpenNewWindow);
			}
			ToolbarBuilder.EndSection();
		}

		static void FillRightSideToolbar(FToolBarBuilder& ToolbarBuilder)
		{
			ToolbarBuilder.BeginSection("Debug");
			{
				ToolbarBuilder.AddToolBarButton(FInsightsCommands::Get().ToggleDebugInfo);
			}
			ToolbarBuilder.EndSection();
		}
	};

	//TSharedPtr<FUICommandList> CommandList = FInsightsManager::Get()->GetCommandList();
	TSharedPtr<FUICommandList> CommandList = ProfilerWindow->GetCommandList();

	FToolBarBuilder ToolbarBuilder(CommandList.ToSharedRef(), FMultiBoxCustomization::None);
	Local::FillViewToolbar(ProfilerWindow, ToolbarBuilder);

	FToolBarBuilder RightSideToolbarBuilder(CommandList.ToSharedRef(), FMultiBoxCustomization::None);
	Local::FillRightSideToolbar(RightSideToolbarBuilder);

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
				ToolbarBuilder.MakeWidget()
			]
		]

		+SHorizontalBox::Slot()
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
