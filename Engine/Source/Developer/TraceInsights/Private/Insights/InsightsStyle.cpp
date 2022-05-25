// Copyright Epic Games, Inc. All Rights Reserved.

#include "InsightsStyle.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/LowLevelMemTracker.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/StyleColors.h"
#include "Styling/ToolBarStyle.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FInsightsStyle
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FInsightsStyle::FStyle> FInsightsStyle::StyleInstance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsStyle::Initialize()
{
	LLM_SCOPE_BYNAME(TEXT("Insights/Style"));

	// The core style must be initialized before the Insights style
	FSlateApplication::InitializeCoreStyle();

	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<FInsightsStyle::FStyle> FInsightsStyle::Create()
{
	TSharedRef<class FInsightsStyle::FStyle> NewStyle = MakeShareable(new FInsightsStyle::FStyle(FInsightsStyle::GetStyleSetName()));
	NewStyle->Initialize();
	return NewStyle;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const ISlateStyle& FInsightsStyle::Get()
{
	return *StyleInstance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName FInsightsStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("InsightsStyle"));
	return StyleSetName;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FInsightsStyle::FStyle
////////////////////////////////////////////////////////////////////////////////////////////////////

FInsightsStyle::FStyle::FStyle(const FName& InStyleSetName)
	: FSlateStyleSet(InStyleSetName)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FInsightsStyle::FStyle::SyncParentStyles()
{
	const ISlateStyle* ParentStyle = GetParentStyle();

	NormalText = ParentStyle->GetWidgetStyle<FTextBlockStyle>("NormalText");
	Button = ParentStyle->GetWidgetStyle<FButtonStyle>("Button");

	SelectorColor = ParentStyle->GetSlateColor("SelectorColor");
	SelectionColor = ParentStyle->GetSlateColor("SelectionColor");
	SelectionColor_Inactive = ParentStyle->GetSlateColor("SelectionColor_Inactive");
	SelectionColor_Pressed = ParentStyle->GetSlateColor("SelectionColor_Pressed");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#define EDITOR_IMAGE_BRUSH(RelativePath, ...) IMAGE_BRUSH("../../../Editor/Slate/" RelativePath, __VA_ARGS__)
#define EDITOR_IMAGE_BRUSH_SVG(RelativePath, ...) IMAGE_BRUSH_SVG("../../../Editor/Slate/" RelativePath, __VA_ARGS__)
#define EDITOR_BOX_BRUSH(RelativePath, ...) BOX_BRUSH("../../../Editor/Slate/" RelativePath, __VA_ARGS__)
#define EDITOR_BORDER_BRUSH(RelativePath, ...) BORDER_BRUSH("../../../Editor/Slate/" RelativePath, __VA_ARGS__)
#define TODO_IMAGE_BRUSH(...) EDITOR_IMAGE_BRUSH_SVG("Starship/Common/StaticMesh", __VA_ARGS__)

void FInsightsStyle::FStyle::Initialize()
{
	SetParentStyleName("CoreStyle");

	// Sync styles from the parent style that will be used as templates for styles defined here
	SyncParentStyles();

	Set("Mono.9", DEFAULT_FONT("Mono", 9));
	Set("Mono.10", DEFAULT_FONT("Mono", 10));

	SetContentRoot(FPaths::EngineContentDir() / TEXT("Slate/Starship/Insights"));
	SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	const FVector2D Icon12x12(12.0f, 12.0f); // for TreeItem icons
	const FVector2D Icon16x16(16.0f, 16.0f); // for regular icons
	const FVector2D Icon20x20(20.0f, 20.0f); // for ToolBar icons

	Set("AppIcon", new IMAGE_BRUSH_SVG("UnrealInsights", FVector2D(45.0f, 45.0f)));
	Set("AppIconPadding", FMargin(5.0f, 5.0f, 5.0f, 5.0f));

	Set("AppIcon.Small", new IMAGE_BRUSH_SVG("UnrealInsights", FVector2D(24.0f, 24.0f)));
	Set("AppIconPadding.Small", FMargin(4.0f, 4.0f, 0.0f, 0.0f));

	//////////////////////////////////////////////////

	Set("WhiteBrush", new FSlateColorBrush(FLinearColor::White));
	Set("DarkGreenBrush", new FSlateColorBrush(FLinearColor(0.0f, 0.25f, 0.0f, 1.0f)));

	Set("SingleBorder", new FSlateBorderBrush(NAME_None, FMargin(1.0f)));
	Set("DoubleBorder", new FSlateBorderBrush(NAME_None, FMargin(2.0f)));

	Set("EventBorder", new FSlateBorderBrush(NAME_None, FMargin(1.0f)));
	Set("HoveredEventBorder", new FSlateBorderBrush(NAME_None, FMargin(2.0f)));
	Set("SelectedEventBorder", new FSlateBorderBrush(NAME_None, FMargin(2.0f)));

	Set("RoundedBackground", new FSlateRoundedBoxBrush(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), Icon16x16));

	Set("Border.TB", new EDITOR_BOX_BRUSH("Icons/Profiler/Profiler_Border_TB_16x", FMargin(4.0f / 16.0f)));
	Set("Border.L", new EDITOR_BOX_BRUSH("Icons/Profiler/Profiler_Border_L_16x", FMargin(4.0f / 16.0f)));
	Set("Border.R", new EDITOR_BOX_BRUSH("Icons/Profiler/Profiler_Border_R_16x", FMargin(4.0f / 16.0f)));

	Set("Graph.Point", new EDITOR_IMAGE_BRUSH("Old/Graph/ExecutionBubble", Icon16x16));
	Set("TreeViewBanner.WarningIcon", new CORE_IMAGE_BRUSH_SVG("Starship/Common/alert-circle", Icon20x20, FStyleColors::Warning));

	//////////////////////////////////////////////////
	// Icons for major components

	Set("Icons.SessionInfo", new IMAGE_BRUSH_SVG("Session", Icon16x16));

	//////////////////////////////////////////////////
	// Trace Store, Connection, Launcher

	Set("Icons.TraceStore", new IMAGE_BRUSH_SVG("TraceStore", Icon16x16));
	Set("Icons.Connection", new IMAGE_BRUSH_SVG("Connection", Icon16x16));
	Set("Icons.Launcher", new TODO_IMAGE_BRUSH(Icon16x16));

	//////////////////////////////////////////////////
	// Timing Insights

	Set("Icons.TimingProfiler", new IMAGE_BRUSH_SVG("Timing", Icon16x16));

	Set("Icons.FramesTrack", new IMAGE_BRUSH_SVG("Frames", Icon16x16));
	Set("Icons.FramesTrack.ToolBar", new IMAGE_BRUSH_SVG("Frames_20", Icon20x20));

	Set("Icons.TimingView", new IMAGE_BRUSH_SVG("Timing", Icon16x16));
	Set("Icons.TimingView.ToolBar", new IMAGE_BRUSH_SVG("Timing_20", Icon20x20));

	Set("Icons.TimersView", new IMAGE_BRUSH_SVG("Timer", Icon16x16));
	Set("Icons.TimersView.ToolBar", new IMAGE_BRUSH_SVG("Timer_20", Icon20x20));

	Set("Icons.CountersView", new IMAGE_BRUSH_SVG("Counter", Icon16x16));
	Set("Icons.CountersView.ToolBar", new IMAGE_BRUSH_SVG("Counter_20", Icon20x20));

	Set("Icons.CallersView", new IMAGE_BRUSH_SVG("Callers", Icon16x16));
	Set("Icons.CallersView.ToolBar", new IMAGE_BRUSH_SVG("Callers_20", Icon20x20));

	Set("Icons.CalleesView", new IMAGE_BRUSH_SVG("Callees", Icon16x16));
	Set("Icons.CalleesView.ToolBar", new IMAGE_BRUSH_SVG("Callees_20", Icon20x20));

	Set("Icons.LogView", new IMAGE_BRUSH_SVG("Log", Icon16x16));
	Set("Icons.LogView.ToolBar", new IMAGE_BRUSH_SVG("Log_20", Icon20x20));

	Set("Icons.TableTreeView", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/Spreadsheet", Icon16x16));
	Set("Icons.TableTreeView.ToolBar", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/Spreadsheet", Icon20x20));

	Set("Icons.TasksView", new IMAGE_BRUSH_SVG("Tasks", Icon16x16));

	Set("Icons.AllTracksMenu.ToolBar", new IMAGE_BRUSH_SVG("AllTracks_20", Icon20x20));
	Set("Icons.CpuGpuTracksMenu.ToolBar", new IMAGE_BRUSH_SVG("CpuGpuTracks_20", Icon20x20));
	Set("Icons.OtherTracksMenu.ToolBar", new IMAGE_BRUSH_SVG("SpecialTracks_20", Icon20x20));
	Set("Icons.PluginTracksMenu.ToolBar", new IMAGE_BRUSH_SVG("PluginTracks_20", Icon20x20));
	Set("Icons.ViewModeMenu.ToolBar", new IMAGE_BRUSH_SVG("ViewMode_20", Icon20x20));

	Set("Icons.HighlightEvents.ToolBar", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/Visualizer", Icon20x20));
	Set("Icons.ResetHighlight.ToolBar", new CORE_IMAGE_BRUSH_SVG("Starship/Common/Reject", Icon20x20));

	//////////////////////////////////////////////////
	// Asset Loading Insights

	Set("Icons.LoadingProfiler", new CORE_IMAGE_BRUSH_SVG("Starship/Common/file", Icon16x16));

	//////////////////////////////////////////////////
	// Networking Insights

	Set("Icons.NetworkingProfiler", new IMAGE_BRUSH_SVG("Networking", Icon16x16));

	Set("Icons.PacketView", new IMAGE_BRUSH_SVG("Packets", Icon16x16));
	Set("Icons.PacketView.ToolBar", new IMAGE_BRUSH_SVG("Packets_20", Icon20x20));

	Set("Icons.PacketContentView", new IMAGE_BRUSH_SVG("PacketContent", Icon16x16));
	Set("Icons.PacketContentView.ToolBar", new IMAGE_BRUSH_SVG("PacketContent_20", Icon20x20));

	Set("Icons.NetStatsView", new IMAGE_BRUSH_SVG("NetStats", Icon16x16));
	Set("Icons.NetStatsView.ToolBar", new IMAGE_BRUSH_SVG("NetStats_20", Icon20x20));

	//////////////////////////////////////////////////
	// Memory Insights

	Set("Icons.MemoryProfiler", new IMAGE_BRUSH_SVG("Memory", Icon16x16));

	Set("Icons.MemTagTreeView", new IMAGE_BRUSH_SVG("MemTags", Icon16x16));
	Set("Icons.MemTagTreeView.ToolBar", new IMAGE_BRUSH_SVG("MemTags_20", Icon20x20));

	Set("Icons.MemInvestigationView", new IMAGE_BRUSH_SVG("MemInvestigation", Icon16x16));
	Set("Icons.MemInvestigationView.ToolBar", new IMAGE_BRUSH_SVG("MemInvestigation_20", Icon20x20));

	Set("Icons.MemAllocTableTreeView", new IMAGE_BRUSH_SVG("MemAllocTable", Icon16x16));

	Set("Icons.ModulesView", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/Spreadsheet", Icon16x16));
	Set("Icons.ModulesView.ToolBar", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/Spreadsheet", Icon20x20));

	Set("Icons.AddAllMemTagGraphs", new CORE_IMAGE_BRUSH_SVG("Starship/Common/plus-circle", Icon16x16));
	Set("Icons.RemoveAllMemTagGraphs", new CORE_IMAGE_BRUSH_SVG("Starship/Common/Delete", Icon16x16));

	//////////////////////////////////////////////////
	// Tasks

	Set("Icons.GoToTask", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_ViewColumn_32x", Icon16x16));
	Set("Icons.ShowTaskCriticalPath", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_HotPath_32x", Icon16x16));
	Set("Icons.ShowTaskTransitions", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_Calls_32x", Icon16x16));
	Set("Icons.ShowTaskConnections", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_Calls_32x", Icon16x16));
	Set("Icons.ShowTaskPrerequisites", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_Calls_32x", Icon16x16));
	Set("Icons.ShowTaskSubsequents", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_Calls_32x", Icon16x16));
	Set("Icons.ShowNestedTasks", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_Calls_32x", Icon16x16));
	Set("Icons.ShowTaskTrack", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_Calls_32x", Icon16x16));
	Set("Icons.ShowDetailedTaskTrackInfo", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_Calls_32x", Icon16x16));

	//////////////////////////////////////////////////

	Set("Icons.FindFirst.ToolBar", new IMAGE_BRUSH_SVG("ControlsFirst", Icon20x20));
	Set("Icons.FindPrevious.ToolBar", new IMAGE_BRUSH_SVG("ControlsPrevious", Icon20x20));
	Set("Icons.FindNext.ToolBar", new IMAGE_BRUSH_SVG("ControlsNext", Icon20x20));
	Set("Icons.FindLast.ToolBar", new IMAGE_BRUSH_SVG("ControlsLast", Icon20x20));

	//////////////////////////////////////////////////

	Set("Icons.SizeSmall", new IMAGE_BRUSH_SVG("SizeSmall", Icon16x16));
	Set("Icons.SizeSmall.ToolBar", new IMAGE_BRUSH_SVG("SizeSmall_20", Icon20x20));
	Set("Icons.SizeMedium", new IMAGE_BRUSH_SVG("SizeMedium", Icon16x16));
	Set("Icons.SizeMedium.ToolBar", new IMAGE_BRUSH_SVG("SizeMedium_20", Icon20x20));
	Set("Icons.SizeLarge", new IMAGE_BRUSH_SVG("SizeLarge", Icon16x16));
	Set("Icons.SizeLarge.ToolBar", new IMAGE_BRUSH_SVG("SizeLarge_20", Icon20x20));

	//////////////////////////////////////////////////

	Set("MainFrame.OpenVisualStudio", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/VisualStudio", Icon16x16));
	Set("MainFrame.OpenSourceCodeEditor", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/SourceCodeEditor", Icon16x16));

	//////////////////////////////////////////////////

	Set("Icons.ResetToDefault", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_ResetToDefault_32x", Icon16x16));
	Set("Icons.DiffersFromDefault", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/ResetToDefault", Icon16x16));

	Set("Icons.Console", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/Console", Icon16x16));
	//Set("Icons.Filter", new CORE_IMAGE_BRUSH_SVG("Starship/Common/filter", Icon16x16));	//-> use FAppStyle "Icons.Filter"
	Set("Icons.Filter.ToolBar", new CORE_IMAGE_BRUSH_SVG("Starship/Common/filter", Icon20x20));
	Set("Icons.Find", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/TraceDataFiltering", Icon16x16));
	Set("Icons.FilterAddGroup", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/WorldOutliner", Icon16x16));
	Set("Icons.ClassicFilter", new IMAGE_BRUSH_SVG("Filter", Icon16x16));
	Set("Icons.ClassicFilterConfig", new IMAGE_BRUSH_SVG("FilterConfig", Icon16x16));

	Set("Icons.FolderExplore", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/ContentBrowser", Icon16x16));
	//Set("Icons.FolderOpen", new CORE_IMAGE_BRUSH_SVG("Starship/Common/folder-open", Icon16x16));		//-> use FAppStyle "Icons.FolderOpen"
	//Set("Icons.FolderClosed", new CORE_IMAGE_BRUSH_SVG("Starship/Common/folder-closed", Icon16x16));	//-> use FAppStyle "Icons.FolderClosed"

	Set("Icons.SortBy", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_SortBy_32x", Icon16x16));
	//Set("Icons.SortAscending", new CORE_IMAGE_BRUSH_SVG("Starship/Common/SortUp", Icon16x16));	//-> use FAppStyle "Icons.SortUp"
	//Set("Icons.SortDescending", new CORE_IMAGE_BRUSH_SVG("Starship/Common/SortDown", Icon16x16));	//-> use FAppStyle "Icons.SortDown"

	Set("Icons.ViewColumn", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_ViewColumn_32x", Icon16x16));
	Set("Icons.ResetColumn", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_ResetColumn_32x", Icon16x16));

	Set("Icons.ExpandAll", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_ExpandAll_32x", Icon16x16));
	Set("Icons.CollapseAll", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_CollapseAll_32x", Icon16x16));
	Set("Icons.ExpandSelection", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_ExpandSelection_32x", Icon16x16));
	Set("Icons.CollapseSelection", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_CollapseSelection_32x", Icon16x16));

	Set("Icons.ToggleShowGraphSeries", new EDITOR_IMAGE_BRUSH("Icons/Profiler/profiler_ShowGraphData_32x", Icon16x16));

	Set("Icons.TestAutomation", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/TestAutomation", Icon16x16));
	Set("Icons.Test", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/Test", Icon16x16));

	Set("Icons.Debug", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/bug", Icon16x16));
	Set("Icons.Debug.ToolBar", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/bug", Icon20x20));

	Set("Icons.AutoScroll", new IMAGE_BRUSH_SVG("AutoScrollRight_20", Icon16x16));

	Set("Icons.ZeroCountFilter", new IMAGE_BRUSH_SVG("ZeroCountFilter", Icon16x16));

	Set("Icons.Function", new IMAGE_BRUSH_SVG("Function", Icon16x16));

	Set("Icons.Pinned", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/Pinned", Icon16x16));
	Set("Icons.Unpinned", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/Unpinned", Icon16x16));

	Set("Icons.Rename", new CORE_IMAGE_BRUSH_SVG("Starship/Common/Rename", Icon16x16));
	Set("Icons.Delete", new CORE_IMAGE_BRUSH_SVG("Starship/Common/Delete", Icon16x16));

	//////////////////////////////////////////////////

	Set("TreeTable.RowBackground", new EDITOR_IMAGE_BRUSH("Old/White", Icon16x16, FLinearColor(1.0f, 1.0f, 1.0f, 0.25f)));

	Set("Icons.Hint.TreeItem", new IMAGE_BRUSH_SVG("InfoTag_12", Icon12x12));
	Set("Icons.HotPath.TreeItem", new IMAGE_BRUSH_SVG("HotPath_12", Icon12x12));
	Set("Icons.Group.TreeItem", new CORE_IMAGE_BRUSH_SVG("Starship/Common/folder-closed", Icon12x12));
	Set("Icons.Leaf.TreeItem", new CORE_IMAGE_BRUSH_SVG("Starship/Common/file", Icon12x12));
	Set("Icons.GpuTimer.TreeItem", new TODO_IMAGE_BRUSH(Icon12x12));
	Set("Icons.CpuTimer.TreeItem", new TODO_IMAGE_BRUSH(Icon12x12));
	Set("Icons.Counter.TreeItem", new TODO_IMAGE_BRUSH(Icon12x12));
	Set("Icons.StatCounter.TreeItem", new TODO_IMAGE_BRUSH(Icon12x12));
	Set("Icons.DataTypeDouble.TreeItem", new TODO_IMAGE_BRUSH(Icon12x12));
	Set("Icons.DataTypeInt64.TreeItem", new TODO_IMAGE_BRUSH(Icon12x12));
	Set("Icons.NetEvent.TreeItem", new TODO_IMAGE_BRUSH(Icon12x12));
	Set("Icons.MemTag.TreeItem", new TODO_IMAGE_BRUSH(Icon12x12));

	Set("TreeTable.TooltipBold", FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Bold", 8))
		.SetColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
		.SetShadowOffset(FVector2D(1.0f, 1.0f))
		.SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f))
	);

	Set("TreeTable.Tooltip", FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Regular", 8))
		.SetColorAndOpacity(FLinearColor::White)
		.SetShadowOffset(FVector2D(1.0f, 1.0f))
		.SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f))
	);

	//////////////////////////////////////////////////

	// PrimaryToolbar
	{
		FToolBarStyle PrimaryToolbarStyle = FAppStyle::Get().GetWidgetStyle<FToolBarStyle>("SlimToolBar");

		Set("PrimaryToolbar", PrimaryToolbarStyle);

		Set("PrimaryToolbar.MinUniformToolbarSize", 40.0f);
		Set("PrimaryToolbar.MaxUniformToolbarSize", 40.0f);
	}

	// SecondaryToolbar
	{
		FToolBarStyle SecondaryToolbarStyle = FAppStyle::Get().GetWidgetStyle<FToolBarStyle>("SlimToolBar");

		SecondaryToolbarStyle.SetBackgroundPadding(         FMargin(4.0f, 4.0f));
		SecondaryToolbarStyle.SetBlockPadding(              FMargin(2.0f, 0.0f));
		SecondaryToolbarStyle.SetButtonPadding(             FMargin(0.0f, 0.0f));
		SecondaryToolbarStyle.SetCheckBoxPadding(           FMargin(2.0f, 0.0f));
		SecondaryToolbarStyle.SetComboButtonPadding(        FMargin(0.0f, 0.0f));
		SecondaryToolbarStyle.SetIndentedBlockPadding(      FMargin(2.0f, 0.0f));
		SecondaryToolbarStyle.SetLabelPadding(              FMargin(2.0f, 0.0f));
		SecondaryToolbarStyle.SetSeparatorPadding(          FMargin(2.0f, -3.0f));

		SecondaryToolbarStyle.ToggleButton.SetPadding(      FMargin(0.0f, 0.0f));

		SecondaryToolbarStyle.ButtonStyle.SetNormalPadding( FMargin(6.0f, 2.0f, 4.0f, 2.0f));
		SecondaryToolbarStyle.ButtonStyle.SetPressedPadding(FMargin(6.0f, 2.0f, 4.0f, 2.0f));

		//SecondaryToolbarStyle.IconSize.Set(16.0f, 16.0f);

		Set("SecondaryToolbar", SecondaryToolbarStyle);

		Set("SecondaryToolbar.MinUniformToolbarSize", 32.0f);
		Set("SecondaryToolbar.MaxUniformToolbarSize", 32.0f);
	}

	// SecondaryToolbar2 (used by AutoScroll and NetPacketContentView toolbars)
	{
		FToolBarStyle SecondaryToolbarStyle = FAppStyle::Get().GetWidgetStyle<FToolBarStyle>("SlimToolBar");

		SecondaryToolbarStyle.SetBackgroundPadding(         FMargin(4.0f, 2.0f));
		SecondaryToolbarStyle.SetBlockPadding(              FMargin(2.0f, 2.0f));
		SecondaryToolbarStyle.SetButtonPadding(             FMargin(0.0f, 2.0f));
		SecondaryToolbarStyle.SetCheckBoxPadding(           FMargin(2.0f, 2.0f));
		SecondaryToolbarStyle.SetComboButtonPadding(        FMargin(0.0f, 2.0f));
		SecondaryToolbarStyle.SetIndentedBlockPadding(      FMargin(2.0f, 2.0f));
		SecondaryToolbarStyle.SetLabelPadding(              FMargin(2.0f, 2.0f));
		SecondaryToolbarStyle.SetSeparatorPadding(          FMargin(2.0f, -1.0f));

		SecondaryToolbarStyle.ToggleButton.SetPadding(      FMargin(0.0f, 0.0f));

		SecondaryToolbarStyle.ButtonStyle.SetNormalPadding( FMargin(3.0f, 0.0f, -1.0f, 0.0f));
		SecondaryToolbarStyle.ButtonStyle.SetPressedPadding(FMargin(3.0f, 0.0f, -1.0f, 0.0f));

		//SecondaryToolbarStyle.IconSize.Set(16.0f, 16.0f);

		Set("SecondaryToolbar2", SecondaryToolbarStyle);

		Set("SecondaryToolbar2.MinUniformToolbarSize", 32.0f);
		Set("SecondaryToolbar2.MaxUniformToolbarSize", 32.0f);
	}

	//////////////////////////////////////////////////
	// Workaround for Automation styles...
#if 1

	const FVector2D Icon24x24(24.0f, 24.0f);
	const FVector2D Icon40x40(40.0f, 40.0f);

	Set("Automation.Header", FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Mono", 12))
		.SetColorAndOpacity(FLinearColor(FColor(0xffffffff))));

	Set("Automation.Normal", FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Mono", 9))
		.SetColorAndOpacity(FLinearColor(FColor(0xffaaaaaa))));

	Set("Automation.Warning", FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Mono", 9))
		.SetColorAndOpacity(FLinearColor(FColor(0xffbbbb44))));

	Set("Automation.Error", FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Mono", 9))
		.SetColorAndOpacity(FLinearColor(FColor(0xffff0000))));

	Set("Automation.ReportHeader", FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Mono", 10))
		.SetColorAndOpacity(FLinearColor(FColor(0xffffffff))));

	Set("Automation.Success", new EDITOR_IMAGE_BRUSH("Automation/Success", Icon16x16));
	Set("Automation.Warning", new EDITOR_IMAGE_BRUSH("Automation/Warning", Icon16x16));
	Set("Automation.Fail", new EDITOR_IMAGE_BRUSH("Automation/Fail", Icon16x16));
	Set("Automation.InProcess", new EDITOR_IMAGE_BRUSH("Automation/InProcess", Icon16x16));
	Set("Automation.NotRun", new EDITOR_IMAGE_BRUSH("Automation/NotRun", Icon16x16, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f)));
	Set("Automation.Skipped", new EDITOR_IMAGE_BRUSH("Automation/NoSessionWarning", Icon16x16));
	Set("Automation.ParticipantsWarning", new EDITOR_IMAGE_BRUSH("Automation/ParticipantsWarning", Icon16x16));
	Set("Automation.Participant", new EDITOR_IMAGE_BRUSH("Automation/Participant", Icon16x16));

	Set("Automation.SmokeTest", new EDITOR_IMAGE_BRUSH("Automation/SmokeTest", Icon16x16));
	Set("Automation.SmokeTestParent", new EDITOR_IMAGE_BRUSH("Automation/SmokeTestParent", Icon16x16));

	Set("AutomationWindow.RunTests", new EDITOR_IMAGE_BRUSH("Automation/RunTests", Icon40x40));
	Set("AutomationWindow.RefreshTests", new EDITOR_IMAGE_BRUSH("Automation/RefreshTests", Icon40x40));
	Set("AutomationWindow.FindWorkers", new EDITOR_IMAGE_BRUSH("Automation/RefreshWorkers", Icon40x40));
	Set("AutomationWindow.StopTests", new EDITOR_IMAGE_BRUSH("Automation/StopTests", Icon40x40));
	Set("AutomationWindow.RunTests.Small", new EDITOR_IMAGE_BRUSH("Automation/RunTests", Icon20x20));
	Set("AutomationWindow.RefreshTests.Small", new EDITOR_IMAGE_BRUSH("Automation/RefreshTests", Icon20x20));
	Set("AutomationWindow.FindWorkers.Small", new EDITOR_IMAGE_BRUSH("Automation/RefreshWorkers", Icon20x20));
	Set("AutomationWindow.StopTests.Small", new EDITOR_IMAGE_BRUSH("Automation/StopTests", Icon20x20));

	//filter icons
	Set("AutomationWindow.ErrorFilter", new EDITOR_IMAGE_BRUSH("Automation/ErrorFilter", Icon40x40));
	Set("AutomationWindow.WarningFilter", new EDITOR_IMAGE_BRUSH("Automation/WarningFilter", Icon40x40));
	Set("AutomationWindow.SmokeTestFilter", new EDITOR_IMAGE_BRUSH("Automation/SmokeTestFilter", Icon40x40));
	Set("AutomationWindow.DeveloperDirectoryContent", new EDITOR_IMAGE_BRUSH("Automation/DeveloperDirectoryContent", Icon40x40));
	Set("AutomationWindow.ExcludedTestsFilter", new EDITOR_IMAGE_BRUSH("Automation/ExcludedTestsFilter", Icon40x40));
	Set("AutomationWindow.ErrorFilter.Small", new EDITOR_IMAGE_BRUSH("Automation/ErrorFilter", Icon20x20));
	Set("AutomationWindow.WarningFilter.Small", new EDITOR_IMAGE_BRUSH("Automation/WarningFilter", Icon20x20));
	Set("AutomationWindow.SmokeTestFilter.Small", new EDITOR_IMAGE_BRUSH("Automation/SmokeTestFilter", Icon20x20));
	Set("AutomationWindow.DeveloperDirectoryContent.Small", new EDITOR_IMAGE_BRUSH("Automation/DeveloperDirectoryContent", Icon20x20));
	Set("AutomationWindow.TrackHistory", new EDITOR_IMAGE_BRUSH("Automation/TrackTestHistory", Icon40x40));

	//device group settings
	Set("AutomationWindow.GroupSettings", new EDITOR_IMAGE_BRUSH("Automation/Groups", Icon40x40));
	Set("AutomationWindow.GroupSettings.Small", new EDITOR_IMAGE_BRUSH("Automation/Groups", Icon20x20));

	//test preset icons
	Set("AutomationWindow.PresetNew", new EDITOR_IMAGE_BRUSH("Icons/icon_add_40x", Icon16x16));
	Set("AutomationWindow.PresetSave", new EDITOR_IMAGE_BRUSH_SVG("Starship/Common/SaveCurrent", Icon16x16));
	Set("AutomationWindow.PresetRemove", new EDITOR_IMAGE_BRUSH("Icons/icon_Cascade_DeleteLOD_40x", Icon16x16));

	//test backgrounds
	Set("AutomationWindow.GameGroupBorder", new EDITOR_BOX_BRUSH("Automation/GameGroupBorder", FMargin(4.0f / 16.0f)));
	Set("AutomationWindow.EditorGroupBorder", new EDITOR_BOX_BRUSH("Automation/EditorGroupBorder", FMargin(4.0f / 16.0f)));

	Set("MessageLog.ListBorder", new EDITOR_BOX_BRUSH("/Docking/AppTabContentArea", FMargin(4 / 16.0f)));

	Set("Launcher.Platform_Windows", new EDITOR_IMAGE_BRUSH("Launcher/All_Platforms_24x", Icon24x24));

	// Output Log Window
	{
		const FSlateColor LogColor_SelectionBackground(EStyleColor::User2);
		const FSlateColor LogColor_Normal(EStyleColor::User3);
		const FSlateColor LogColor_Command(EStyleColor::User4);

		const int32 LogFontSize = 9;
		const FTextBlockStyle NormalLogText = FTextBlockStyle(NormalText)
			.SetFont(DEFAULT_FONT("Mono", LogFontSize))
			.SetColorAndOpacity(LogColor_Normal)
			.SetSelectedBackgroundColor(LogColor_SelectionBackground)
			.SetHighlightColor(FStyleColors::Black);

		Set("Log.Normal", NormalLogText);

		Set("Log.Command", FTextBlockStyle(NormalLogText)
			.SetColorAndOpacity(LogColor_Command));

		Set("Log.Warning", FTextBlockStyle(NormalLogText)
			.SetColorAndOpacity(FStyleColors::Warning));

		Set("Log.Error", FTextBlockStyle(NormalLogText)
			.SetColorAndOpacity(FStyleColors::Error));
	}

	{
		Set("ToggleButton", FButtonStyle(Button)
			.SetNormal(FSlateNoResource())
			.SetHovered(EDITOR_BOX_BRUSH("Common/RoundedSelection_16x", 4.0f / 16.0f, SelectionColor))
			.SetPressed(EDITOR_BOX_BRUSH("Common/RoundedSelection_16x", 4.0f / 16.0f, SelectionColor_Pressed)));
	}

	{
		FTextBlockStyle InheritedFromNativeTextStyle = FTextBlockStyle(NormalText)
			.SetFont(DEFAULT_FONT("Regular", 10));

		Set("Common.InheritedFromNativeTextStyle", InheritedFromNativeTextStyle);

		// Go to native class hyperlink
		FButtonStyle EditNativeHyperlinkButton = FButtonStyle()
			.SetNormal(EDITOR_BORDER_BRUSH("Old/HyperlinkDotted", FMargin(0, 0, 0, 3 / 16.0f)))
			.SetPressed(FSlateNoResource())
			.SetHovered(EDITOR_BORDER_BRUSH("Old/HyperlinkUnderline", FMargin(0, 0, 0, 3 / 16.0f)));
		FHyperlinkStyle EditNativeHyperlinkStyle = FHyperlinkStyle()
			.SetUnderlineStyle(EditNativeHyperlinkButton)
			.SetTextStyle(InheritedFromNativeTextStyle)
			.SetPadding(FMargin(0.0f));

		Set("Common.GotoNativeCodeHyperlink", EditNativeHyperlinkStyle);
	}

#endif
	//////////////////////////////////////////////////
}

#undef TODO_IMAGE_BRUSH
#undef EDITOR_BOX_BRUSH
#undef EDITOR_IMAGE_BRUSH_SVG
#undef EDITOR_IMAGE_BRUSH

////////////////////////////////////////////////////////////////////////////////////////////////////
