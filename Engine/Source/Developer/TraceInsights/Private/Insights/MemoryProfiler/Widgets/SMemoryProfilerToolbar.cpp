// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMemoryProfilerToolbar.h"

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
#include "Insights/MemoryProfiler/MemoryProfilerCommands.h"
#include "Insights/MemoryProfiler/MemoryProfilerManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SMemoryProfilerToolbar"

////////////////////////////////////////////////////////////////////////////////////////////////////

SMemoryProfilerToolbar::SMemoryProfilerToolbar()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SMemoryProfilerToolbar::~SMemoryProfilerToolbar()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SMemoryProfilerToolbar::Construct(const FArguments& InArgs)
{
	struct Local
	{
		static void FillViewToolbar(FToolBarBuilder& ToolbarBuilder)
		{
			ToolbarBuilder.BeginSection("View");
			{
				ToolbarBuilder.AddToolBarButton(FMemoryProfilerCommands::Get().ToggleTimingViewVisibility);
				ToolbarBuilder.AddToolBarButton(FMemoryProfilerCommands::Get().ToggleMemTagTreeViewVisibility);
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
