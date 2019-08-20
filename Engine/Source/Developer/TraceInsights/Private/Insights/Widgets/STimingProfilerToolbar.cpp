// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "STimingProfilerToolbar.h"

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
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleFramesTrackVisibility);
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleGraphTrackVisibility);
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleTimingViewVisibility);
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleTimersViewVisibility);
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleStatsCountersViewVisibility);
				ToolbarBuilder.AddToolBarButton(FTimingProfilerCommands::Get().ToggleLogViewVisibility);
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
	FillModulesToolbar(ToolbarBuilder);

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

bool STimingProfilerToolbar::ToggleModule_CanExecute(FName ModuleName) const
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	if (!SessionService->IsRecorderServerRunning())
	{
		return false;
	}

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (!Session.IsValid())
	{
		return false;
	}

	Trace::FSessionHandle SessionHandle = FInsightsManager::Get()->GetSessionHandle();
	if (SessionHandle == 0)
	{
		return false;
	}

	Trace::FSessionInfo SessionInfo;
	SessionService->GetSessionInfo(SessionHandle, SessionInfo);
	return SessionInfo.bIsLive;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerToolbar::ToggleModule_Execute(FName ModuleName)
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	Trace::FSessionHandle SessionHandle = FInsightsManager::Get()->GetSessionHandle();

	bool bState = SessionService->IsModuleEnabled(SessionHandle, ModuleName);
	return SessionService->SetModuleEnabled(SessionHandle, ModuleName, !bState);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimingProfilerToolbar::ToggleModule_IsChecked(FName ModuleName) const
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	Trace::FSessionHandle SessionHandle = FInsightsManager::Get()->GetSessionHandle();

	return SessionService->IsModuleEnabled(SessionHandle, ModuleName);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState STimingProfilerToolbar::ToggleModule_IsChecked2(FName ModuleName) const
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	Trace::FSessionHandle SessionHandle = FInsightsManager::Get()->GetSessionHandle();

	return SessionService->IsModuleEnabled(SessionHandle, ModuleName) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerToolbar::ToggleModule_OnCheckStateChanged(ECheckBoxState NewRadioState, FName ModuleName)
{
	TSharedRef<Trace::ISessionService> SessionService = FInsightsManager::Get()->GetSessionService();
	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	Trace::FSessionHandle SessionHandle = FInsightsManager::Get()->GetSessionHandle();

	bool bState = (NewRadioState == ECheckBoxState::Checked);
	return SessionService->SetModuleEnabled(SessionHandle, ModuleName, bState);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingProfilerToolbar::FillModulesToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection("Modules");
	{
		TSharedRef<Trace::IModuleService> ModuleService = FInsightsManager::Get()->GetModuleService();

		TArray<Trace::FModuleInfo> AvailableModules;
		ModuleService->GetAvailableModules(AvailableModules);

		for (int32 ModuleIndex = 0; ModuleIndex < AvailableModules.Num(); ++ModuleIndex)
		{
			const Trace::FModuleInfo& Module = AvailableModules[ModuleIndex];

			FText Label = FText::FromName(Module.DisplayName);
			FText ToggleModuleToolTip = FText::Format(LOCTEXT("ToggleModuleToolTip", "Enable/disable {0} trace module (only for live sessions)."), FText::FromName(Module.DisplayName));

			// TODO: Uncomment this when adding icons to toolbar.
			//ToolbarBuilder.AddToolBarButton(
			//	FUIAction(
			//		FExecuteAction::CreateRaw(this, &STimingProfilerToolbar::ToggleModule_Execute, Module.Name),
			//		FCanExecuteAction::CreateRaw(this, &STimingProfilerToolbar::ToggleModule_CanExecute, Module.Name),
			//		FIsActionChecked::CreateRaw(this, &STimingProfilerToolbar::ToggleModule_IsChecked, Module.Name)
			//	),
			//	NAME_None, // ExtensionHook
			//	Label,
			//	ToggleModuleToolTip,
			//	TAttribute<FSlateIcon>(), // Icon -- empty icon is not really empty :(
			//	EUserInterfaceActionType::ToggleButton,
			//	NAME_None); // TutorialHighlightName

			// Workaround for having toogle buttons without icons.
			ToolbarBuilder.AddWidget(SNew(SCheckBox)
				.IsEnabled(this, &STimingProfilerToolbar::ToggleModule_CanExecute, Module.Name)
				.IsChecked(this, &STimingProfilerToolbar::ToggleModule_IsChecked2, Module.Name)
				.OnCheckStateChanged(this, &STimingProfilerToolbar::ToggleModule_OnCheckStateChanged, Module.Name)
				.Content()
				[
					SNew(STextBlock)
					.Text(Label)
					.ToolTip(SNew(SToolTip).Text(ToggleModuleToolTip))
				]
			);
		}
	}
	ToolbarBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
