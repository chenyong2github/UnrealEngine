// Copyright Epic Games, Inc. All Rights Reserved.

#include "InsightsMenuBuilder.h"

#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Testing/SStarshipSuite.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#include "Insights/InsightsStyle.h"
#include "Insights/InsightsManager.h"

#define LOCTEXT_NAMESPACE "InsightsMenuBuilder"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FInsightsMenuBuilder
////////////////////////////////////////////////////////////////////////////////////////////////////

FInsightsMenuBuilder::FInsightsMenuBuilder()
#if !WITH_EDITOR
	: InsightsToolsGroup(FGlobalTabmanager::Get()->AddLocalWorkspaceMenuCategory(NSLOCTEXT("InsightsMenuTools", "InsightTools", "Insights Tools")))
	, WindowsGroup(FGlobalTabmanager::Get()->AddLocalWorkspaceMenuCategory(NSLOCTEXT("InsightsMenuTools", "InsightWindows", "Windows")))
#endif
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<FWorkspaceItem> FInsightsMenuBuilder::GetInsightsToolsGroup()
{
#if !WITH_EDITOR
	return InsightsToolsGroup;
#else
	return WorkspaceMenu::GetMenuStructure().GetDeveloperToolsProfilingCategory();
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<FWorkspaceItem> FInsightsMenuBuilder::GetWindowsGroup()
{
#if !WITH_EDITOR
	return WindowsGroup;
#else
	return WorkspaceMenu::GetMenuStructure().GetDeveloperToolsProfilingCategory();
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsMenuBuilder::PopulateMenu(FMenuBuilder& MenuBuilder)
{
#if !WITH_EDITOR
	MenuBuilder.BeginSection("Insights");
	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenSessionBrowser", "Session Browser"),
		LOCTEXT("OpenSessionBrowser_ToolTip", "Opens the Unreal Insights Session Browser window."),
		FSlateIcon(FInsightsStyle::GetStyleSetName(), "AppIcon.Small"),
		FUIAction(FExecuteAction::CreateLambda([] { FInsightsManager::Get()->OpenUnrealInsights(); })));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenTraceFile1", "Open Trace File (new instance)..."),
		LOCTEXT("OpenTraceFile1_ToolTip", "Starts analysis for a specified trace file, in a separate Unreal Insights instance."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([] { FInsightsManager::Get()->OpenTraceFile(); })));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenTraceFile2", "Open Trace File (same instance)..."),
		LOCTEXT("OpenTraceFile2_ToolTip", "Starts analysis for a specified trace file, replacing the current analysis session."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([] { FInsightsManager::Get()->LoadTraceFile(); })));
	MenuBuilder.EndSection();

	FGlobalTabmanager::Get()->PopulateLocalTabSpawnerMenu(MenuBuilder);

	static FName WidgetReflectorTabId("WidgetReflector");
	bool bAllowDebugTools = FGlobalTabmanager::Get()->HasTabSpawner(WidgetReflectorTabId);
	if (bAllowDebugTools)
	{
		MenuBuilder.BeginSection("WidgetTools");
		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenWidgetReflector", "Widget Reflector"),
			LOCTEXT("OpenWidgetReflector_ToolTip", "Opens the Widget Reflector, a handy tool for diagnosing problems with live widgets."),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "WidgetReflector.Icon"),
			FUIAction(FExecuteAction::CreateLambda([=] { FGlobalTabmanager::Get()->TryInvokeTab(WidgetReflectorTabId); })));
		MenuBuilder.EndSection();
	}

#if !UE_BUILD_SHIPPING
	// Open Starship Test Suite
	{
		FUIAction OpenStarshipSuiteAction;
		OpenStarshipSuiteAction.ExecuteAction = FExecuteAction::CreateLambda([this]()
			{
				RestoreStarshipSuite();
			});

		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenStarshipSuite", "Starship Test Suite"),
			LOCTEXT("OpenStarshipSuite_ToolTip", "Opens the Starship UX test suite."),
			FSlateIcon(FInsightsStyle::GetStyleSetName(), "Icons.Test"),
			OpenStarshipSuiteAction,
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
#endif // !UE_BUILD_SHIPPING
#endif // !WITH_EDITOR
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsMenuBuilder::AddMenuEntry(
	FMenuBuilder& InOutMenuBuilder,
	const FUIAction& InAction,
	const TAttribute<FText>& InLabel,
	const TAttribute<FText>& InToolTipText,
	const TAttribute<FText>& InKeybinding,
	const EUserInterfaceActionType InUserInterfaceActionType)
{
	InOutMenuBuilder.AddMenuEntry(
		InAction,
		SNew(SBox)
		.Padding(FMargin(0.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(FMargin(2.0f, 0.0f))
			.VAlign(VAlign_Center)
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.TextStyle(FAppStyle::Get(), "Menu.Label")
				.Text(InLabel)
			]
			+ SHorizontalBox::Slot()
			.Padding(FMargin(4.0f, 0.0f))
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.TextStyle(FAppStyle::Get(), "Menu.Keybinding")
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(InKeybinding)
			]
		],
		NAME_None,
		InToolTipText,
		InUserInterfaceActionType
	);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
