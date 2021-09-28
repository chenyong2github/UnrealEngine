// Copyright Epic Games, Inc. All Rights Reserved.

#include "InsightsStyle.h"
#include "Styling/SlateStyleRegistry.h"

#define IMAGE_BRUSH(RelativePath, ...)  FSlateImageBrush (Style.RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define BOX_BRUSH(RelativePath, ...)    FSlateBoxBrush   (Style.RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define BORDER_BRUSH(RelativePath, ...) FSlateBorderBrush(Style.RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)

#define IMAGE_BRUSH_SVG(RelativePath, ...)  FSlateVectorImageBrush (Style.RootToContentDir(RelativePath, TEXT(".svg")), __VA_ARGS__)
#define BOX_BRUSH_SVG(RelativePath, ...)    FSlateVectorBoxBrush   (Style.RootToContentDir(RelativePath, TEXT(".svg")), __VA_ARGS__)
#define BORDER_BRUSH_SVG(RelativePath, ...) FSlateVectorBorderBrush(Style.RootToContentDir(RelativePath, TEXT(".svg")), __VA_ARGS__)

#define CORE_IMAGE_BRUSH(RelativePath, ...)  FSlateVectorImageBrush (Style.RootToCoreContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define CORE_BOX_BRUSH(RelativePath, ...)    FSlateVectorBoxBrush   (Style.RootToCoreContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define CORE_BORDER_BRUSH(RelativePath, ...) FSlateVectorBorderBrush(Style.RootToCoreContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)

#define CORE_IMAGE_BRUSH_SVG(RelativePath, ...)  FSlateVectorImageBrush (Style.RootToCoreContentDir(RelativePath, TEXT(".svg")), __VA_ARGS__)
#define CORE_BOX_BRUSH_SVG(RelativePath, ...)    FSlateVectorBoxBrush   (Style.RootToCoreContentDir(RelativePath, TEXT(".svg")), __VA_ARGS__)
#define CORE_BORDER_BRUSH_SVG(RelativePath, ...) FSlateVectorBorderBrush(Style.RootToCoreContentDir(RelativePath, TEXT(".svg")), __VA_ARGS__)

#define TODO_IMAGE_BRUSH(...) IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/StaticMesh", __VA_ARGS__)

TSharedPtr<FSlateStyleSet> FInsightsStyle::StyleInstance = nullptr;

void FInsightsStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FInsightsStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FInsightsStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("InsightsStyle"));
	return StyleSetName;
}

TSharedRef<FSlateStyleSet> FInsightsStyle::Create()
{
	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon24x24(24.0f, 24.0f);
	const FVector2D Icon32x32(32.0f, 32.0f);
	const FVector2D Icon40x40(40.0f, 40.0f);

	TSharedRef<FSlateStyleSet> StyleRef = MakeShared<FSlateStyleSet>(FInsightsStyle::GetStyleSetName());
	StyleRef->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
	StyleRef->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	FSlateStyleSet& Style = StyleRef.Get();

	//Style.Set("AppIcon", new IMAGE_BRUSH("Icons/Insights/AppIcon_24x", Icon24x24));

	//////////////////////////////////////////////////

	Style.Set("WhiteBrush", new FSlateColorBrush(FLinearColor::White));
	Style.Set("SingleBorder", new FSlateBorderBrush(NAME_None, FMargin(1.0f)));
	Style.Set("DoubleBorder", new FSlateBorderBrush(NAME_None, FMargin(2.0f)));

	Style.Set("EventBorder", new FSlateBorderBrush(NAME_None, FMargin(1.0f)));
	Style.Set("HoveredEventBorder", new FSlateBorderBrush(NAME_None, FMargin(2.0f)));
	Style.Set("SelectedEventBorder", new FSlateBorderBrush(NAME_None, FMargin(2.0f)));

	Style.Set("RoundedBackground", new FSlateRoundedBoxBrush(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), Icon24x24));

	//////////////////////////////////////////////////
	// Icons for major components

	Style.Set("SessionInfo.Icon.Large", new CORE_IMAGE_BRUSH_SVG("Starship/Common/Info", Icon32x32));
	Style.Set("SessionInfo.Icon.Small", new CORE_IMAGE_BRUSH_SVG("Starship/Common/Info", Icon16x16));

	Style.Set("Toolbar.Icon.Large", new IMAGE_BRUSH("Icons/icon_tab_Tools_16x", Icon32x32));
	Style.Set("Toolbar.Icon.Small", new IMAGE_BRUSH("Icons/icon_tab_Tools_16x", Icon16x16));

	//////////////////////////////////////////////////
	// Trace Store, Connection, Launcher

	Style.Set("TraceStore.Icon.Large", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/Home", Icon32x32));
	Style.Set("TraceStore.Icon.Small", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/Home", Icon16x16));

	Style.Set("Connection.Icon.Large", new TODO_IMAGE_BRUSH(Icon32x32));
	Style.Set("Connection.Icon.Small", new TODO_IMAGE_BRUSH(Icon16x16));

	Style.Set("Launcher.Icon.Large", new TODO_IMAGE_BRUSH(Icon32x32));
	Style.Set("Launcher.Icon.Small", new TODO_IMAGE_BRUSH(Icon16x16));

	Style.Set("FolderExplore.Icon.Large", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/ContentBrowser", Icon32x32));
	Style.Set("FolderExplore.Icon.Small", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/ContentBrowser", Icon16x16));

	//////////////////////////////////////////////////
	// Timing Insights

	Style.Set("TimingProfiler.Icon.Large", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/Recent", Icon32x32));
	Style.Set("TimingProfiler.Icon.Small", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/Recent", Icon16x16));

	Style.Set("FramesTrack.Icon.Large", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/Statistics", Icon32x32));
	Style.Set("FramesTrack.Icon.Small", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/Statistics", Icon16x16));

	Style.Set("GraphTrack.Icon.Large", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", Icon32x32));
	Style.Set("GraphTrack.Icon.Small", new IMAGE_BRUSH("Icons/Profiler/profiler_stats_40x", Icon16x16));

	Style.Set("TimingView.Icon.Large", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/Timeline", Icon32x32));
	Style.Set("TimingView.Icon.Small", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/Timeline", Icon16x16));

	Style.Set("TimersView.Icon.Large", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/Realtime", Icon32x32));
	Style.Set("TimersView.Icon.Small", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/Realtime", Icon16x16));

	Style.Set("StatsCountersView.Icon.Large", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/Profile", Icon32x32));
	Style.Set("StatsCountersView.Icon.Small", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/Profile", Icon16x16));

	Style.Set("LogView.Icon.Large", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/Log", Icon32x32));
	Style.Set("LogView.Icon.Small", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/Log", Icon16x16));

	Style.Set("TableTreeView.Icon.Large", new TODO_IMAGE_BRUSH(Icon32x32));
	Style.Set("TableTreeView.Icon.Small", new TODO_IMAGE_BRUSH(Icon16x16));

	//////////////////////////////////////////////////
	// Asset Loading Insights

	Style.Set("LoadingProfiler.Icon.Large", new TODO_IMAGE_BRUSH(Icon32x32));
	Style.Set("LoadingProfiler.Icon.Small", new TODO_IMAGE_BRUSH(Icon16x16));

	//////////////////////////////////////////////////
	// Networking Insights

	Style.Set("NetworkingProfiler.Icon.Large", new TODO_IMAGE_BRUSH(Icon32x32));
	Style.Set("NetworkingProfiler.Icon.Small", new TODO_IMAGE_BRUSH(Icon16x16));

	Style.Set("PacketView.Icon.Large", new TODO_IMAGE_BRUSH(Icon32x32));
	Style.Set("PacketView.Icon.Small", new TODO_IMAGE_BRUSH(Icon16x16));

	Style.Set("PacketContentView.Icon.Large", new TODO_IMAGE_BRUSH(Icon32x32));
	Style.Set("PacketContentView.Icon.Small", new TODO_IMAGE_BRUSH(Icon16x16));

	Style.Set("NetStatsView.Icon.Large", new TODO_IMAGE_BRUSH(Icon32x32));
	Style.Set("NetStatsView.Icon.Small", new TODO_IMAGE_BRUSH(Icon16x16));

	//////////////////////////////////////////////////
	// Memory Insights

	Style.Set("MemoryProfiler.Icon.Large", new TODO_IMAGE_BRUSH(Icon32x32));
	Style.Set("MemoryProfiler.Icon.Small", new TODO_IMAGE_BRUSH(Icon16x16));

	Style.Set("MemInvestigationView.Icon.Large", new TODO_IMAGE_BRUSH(Icon32x32));
	Style.Set("MemInvestigationView.Icon.Small", new TODO_IMAGE_BRUSH(Icon16x16));

	Style.Set("MemAllocTableTreeView.Icon.Large", new TODO_IMAGE_BRUSH(Icon32x32));
	Style.Set("MemAllocTableTreeView.Icon.Small", new TODO_IMAGE_BRUSH(Icon16x16));

	Style.Set("MemTagTreeView.Icon.Large", new TODO_IMAGE_BRUSH(Icon32x32));
	Style.Set("MemTagTreeView.Icon.Small", new TODO_IMAGE_BRUSH(Icon16x16));

	Style.Set("Mem.Add.Small", new CORE_IMAGE_BRUSH_SVG("Starship/Common/plus-circle", Icon16x16));
	Style.Set("Mem.Remove.Small", new CORE_IMAGE_BRUSH_SVG("Starship/Common/Delete", Icon16x16));
	Style.Set("Mem.LoadXML.Small", new CORE_IMAGE_BRUSH_SVG("Starship/Common/folder-open", Icon16x16));

	//////////////////////////////////////////////////

	Style.Set("FindFirst", new CORE_IMAGE_BRUSH_SVG("Starship/Common/arrow-left", Icon20x20));
	Style.Set("FindPrevious", new IMAGE_BRUSH("../../Editor/Slate/Icons/GeneralTools/Previous_40x", Icon20x20));
	Style.Set("FindNext", new IMAGE_BRUSH("../../Editor/Slate/Icons/GeneralTools/Next_40x", Icon20x20));
	Style.Set("FindLast", new CORE_IMAGE_BRUSH_SVG("Starship/Common/arrow-right", Icon20x20));

	//////////////////////////////////////////////////

	Style.Set("Menu.Icon.Large", new CORE_IMAGE_BRUSH_SVG("Starship/Common/menu", Icon32x32));
	Style.Set("Menu.Icon.Small", new CORE_IMAGE_BRUSH_SVG("Starship/Common/menu", Icon16x16));

	Style.Set("Icon.Dummy", new CORE_IMAGE_BRUSH_SVG("Starship/Common/dummy", Icon24x24));

	Style.Set("Icon.Filter", new CORE_IMAGE_BRUSH_SVG("Starship/Common/filter", Icon24x24));
	Style.Set("Icon.Find", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/TraceDataFiltering", Icon24x24));

	Style.Set("Icon.FolderExplore", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/ContentBrowser", Icon24x24));
	Style.Set("Icon.FolderOpen", new CORE_IMAGE_BRUSH_SVG("Starship/Common/folder-open", Icon24x24));

	Style.Set("Icon.TestAutomation", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/TestAutomation", Icon24x24));
	Style.Set("Icon.Test", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/Test", Icon24x24));

	Style.Set("Icon.Bug", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/Common/bug", Icon24x24));

	Style.Set("Icon.AutoScroll", new IMAGE_BRUSH_SVG("../../Editor/Slate/Starship/MainToolbar/play", Icon24x24));
	Style.Set("Icon.AllTracksMenu", new CORE_IMAGE_BRUSH_SVG("Starship/Common/menu", Icon24x24));
	Style.Set("Icon.CpuGpuTracksMenu", new CORE_IMAGE_BRUSH_SVG("Starship/Common/menu", Icon24x24));
	Style.Set("Icon.OtherTracksMenu", new CORE_IMAGE_BRUSH_SVG("Starship/Common/menu", Icon24x24));

	return StyleRef;
}

#undef TODO_IMAGE_BRUSH

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH

#undef IMAGE_BRUSH_SVG
#undef BOX_BRUSH_SVG
#undef BORDER_BRUSH_SVG

#undef CORE_IMAGE_BRUSH_SVG
#undef CORE_BOX_BRUSH_SVG
#undef CORE_BORDER_BRUSH_SVG

const ISlateStyle& FInsightsStyle::Get()
{
	return *StyleInstance;
}
