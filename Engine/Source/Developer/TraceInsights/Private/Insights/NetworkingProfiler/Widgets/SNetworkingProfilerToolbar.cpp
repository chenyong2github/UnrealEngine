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

void SNetworkingProfilerToolbar::Construct(const FArguments& InArgs)
{
	struct Local
	{
		static void FillViewToolbar(FToolBarBuilder& ToolbarBuilder)
		{
			ToolbarBuilder.BeginSection("View");
			{
				ToolbarBuilder.AddToolBarButton(FNetworkingProfilerCommands::Get().TogglePacketSizesViewVisibility);
				ToolbarBuilder.AddToolBarButton(FNetworkingProfilerCommands::Get().TogglePacketBreakdownViewVisibility);
				//ToolbarBuilder.AddToolBarButton(FNetworkingProfilerCommands::Get().ToggleDataStreamBreakdownViewVisibility);
			}
			ToolbarBuilder.EndSection();
			ToolbarBuilder.BeginSection("Connection");
			{
				//FUIAction Action_ChooseReplicationSystem
				//(
				//	FExecuteAction::CreateSP(this, &STableTreeView::ContextMenu_CopySelectedToClipboard_Execute),
				//	FCanExecuteAction::CreateSP(this, &STableTreeView::ContextMenu_CopySelectedToClipboard_CanExecute)
				//);
				//const FOnGetContent MenuContentGenerator;
				//ToolbarBuilder.AddComboButton(Action, MenuContentGenerator);
				TSharedRef<SWidget> ReplicationWidget = SNew(STextBlock)
					.Text(LOCTEXT("Replication", "Replication System"));
				ToolbarBuilder.AddWidget(ReplicationWidget);

				ToolbarBuilder.AddSeparator();

				TSharedRef<SWidget> ConnectionWidget = SNew(STextBlock)
					.Text(LOCTEXT("Connection", "Connection"));
				ToolbarBuilder.AddWidget(ConnectionWidget);

				ToolbarBuilder.AddSeparator();

				TSharedRef<SWidget> TypeWidget = SNew(STextBlock)
					.Text(LOCTEXT("Type", "Sent/Received"));
				ToolbarBuilder.AddWidget(TypeWidget);
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

	TSharedPtr<FUICommandList> CommandList = FInsightsManager::Get()->GetCommandList();

	FToolBarBuilder ToolbarBuilder(CommandList.ToSharedRef(), FMultiBoxCustomization::None);
	Local::FillViewToolbar(ToolbarBuilder);

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
		.FillWidth(1.0)
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
