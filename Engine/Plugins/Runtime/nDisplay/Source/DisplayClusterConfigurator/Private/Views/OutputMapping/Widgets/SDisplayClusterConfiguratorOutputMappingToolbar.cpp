// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorOutputMappingToolbar.h"

#include "DisplayClusterConfiguratorStyle.h"
#include "Interfaces/IDisplayClusterConfigurator.h"
#include "Views/OutputMapping/DisplayClusterConfiguratorOutputMappingCommands.h"
#include "Views/OutputMapping/DisplayClusterConfiguratorViewOutputMapping.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SEditorViewportToolBarMenu.h"
#include "SViewportToolBarIconMenu.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterConfiguratorOutputMappingToolbar"

const TArray<float> SDisplayClusterConfiguratorOutputMappingToolbar::ViewScales = { 2.0f, 1.5f, 1.0f, 0.75f, 0.5f, 1.0f / 3.0f };

void SDisplayClusterConfiguratorOutputMappingToolbar::Construct(const FArguments& InArgs, const TWeakPtr<FDisplayClusterConfiguratorViewOutputMapping>& InViewOutputMapping)
{
	ViewOutputMappingPtr = InViewOutputMapping;

	CommandList = InArgs._CommandList;

	ChildSlot
		[
			MakeToolBar(InArgs._Extenders)
		];

	SViewportToolBar::Construct(SViewportToolBar::FArguments());
}

TSharedRef<SWidget> SDisplayClusterConfiguratorOutputMappingToolbar::MakeToolBar(const TSharedPtr<FExtender> InExtenders)
{
	FToolBarBuilder ToolbarBuilder(CommandList, FMultiBoxCustomization::None, InExtenders);

	// Use a custom style
	FName ToolBarStyle = "EditorViewportToolBar";
	ToolbarBuilder.SetStyle(&FEditorStyle::Get(), ToolBarStyle);
	ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);

	ToolbarBuilder.BeginSection("Advanced");
	{
		// SEditorViewportToolbarMenu buttons are by default 22 pixels high, while the other toolbar buttons are 26 pixels tall
		// Use an SBox to manually match this button's height with the rest of the toolbar
		ToolbarBuilder.AddWidget(
			SNew(SBox)
			.HeightOverride(26)
			[
				SNew(SEditorViewportToolbarMenu)
				.ParentToolBar(SharedThis(this))
				.Cursor(EMouseCursor::Default)
				.Image("EditorViewportToolBar.OptionsDropdown")
				.OnGetMenuContent(this, &SDisplayClusterConfiguratorOutputMappingToolbar::MakeAdvancedMenu)
			]
		);
	}
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("ClusterItems");
	{
		ToolbarBuilder.BeginBlockGroup();
		{
			ToolbarBuilder.AddToolBarButton(FDisplayClusterConfiguratorOutputMappingCommands::Get().ToggleWindowInfo);
			ToolbarBuilder.AddToolBarButton(FDisplayClusterConfiguratorOutputMappingCommands::Get().ToggleWindowCornerImage);
			ToolbarBuilder.AddToolBarButton(FDisplayClusterConfiguratorOutputMappingCommands::Get().ToggleOutsideViewports);
		}
		ToolbarBuilder.EndBlockGroup();
	}
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("View");
	{
		ToolbarBuilder.AddToolBarButton(FDisplayClusterConfiguratorOutputMappingCommands::Get().ZoomToFit);

		ToolbarBuilder.AddWidget(
			SNew(SViewportToolBarIconMenu)
			.Cursor(EMouseCursor::Default)
			.Style(ToolBarStyle)
			.OnGetMenuContent(this, &SDisplayClusterConfiguratorOutputMappingToolbar::MakeViewScaleMenu)
			.ToolTipText(LOCTEXT("ViewOptionsMenu_ToolTip", "View Scale"))
			.Icon(FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.OutputMapping.ViewScale"))
			.Label(this, &SDisplayClusterConfiguratorOutputMappingToolbar::GetViewScaleText)
			.ParentToolBar(SharedThis(this))
		);
	}
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("Transform");
	{
		ToolbarBuilder.AddWidget(
			SNew(SViewportToolBarIconMenu)
			.Cursor(EMouseCursor::Default)
			.Style(ToolBarStyle)
			.OnGetMenuContent(this, &SDisplayClusterConfiguratorOutputMappingToolbar::MakeTransformMenu)
			.ToolTipText(LOCTEXT("TransformMenu_ToolTip", "Transform Operations"))
			.Icon(FSlateIcon(FEditorStyle::GetStyleSetName(), "EditorViewport.TranslateRotate2DMode.Small"))
			.Label(LOCTEXT("TransformSettings_Label", "Transform"))
			.ParentToolBar(SharedThis(this))
		);
	}
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("Placement");
	{
		ToolbarBuilder.AddWidget(
			SNew(SViewportToolBarIconMenu)
			.Cursor(EMouseCursor::Default)
			.Style(ToolBarStyle)
			.OnGetMenuContent(this, &SDisplayClusterConfiguratorOutputMappingToolbar::MakePositioningMenu)
			.ToolTipText(LOCTEXT("PositioningMenu_ToolTip", "Positioning Settings"))
			.Icon(FSlateIcon(FEditorStyle::GetStyleSetName(), "EditorViewport.TranslateRotate2DMode.Small"))
			.Label(LOCTEXT("PositioningSettings_Label", "Positioning"))
			.ParentToolBar(SharedThis(this))
		);

		ToolbarBuilder.AddWidget(
			SNew(SViewportToolBarIconMenu)
			.Cursor(EMouseCursor::Default)
			.Style(ToolBarStyle)
			.OnGetMenuContent(this, &SDisplayClusterConfiguratorOutputMappingToolbar::MakeSnappingMenu)
			.ToolTipText(LOCTEXT("SnappingMenu_ToolTip", "Node Snapping"))
			.Icon(FSlateIcon(FEditorStyle::GetStyleSetName(), "EditorViewport.Layer2DSnap"))
			.Label(LOCTEXT("AlignmentSettings_Label", "Snapping"))
			.ParentToolBar(SharedThis(this))
		);
	}
	ToolbarBuilder.EndSection();

	return ToolbarBuilder.MakeWidget();
}

TSharedRef<SWidget> SDisplayClusterConfiguratorOutputMappingToolbar::MakeTransformMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = false;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, CommandList);

	MenuBuilder.BeginSection(TEXT("CommonTransformSection"));
	{
		MenuBuilder.AddMenuEntry(FDisplayClusterConfiguratorOutputMappingCommands::Get().RotateViewport90CCW);
		MenuBuilder.AddMenuEntry(FDisplayClusterConfiguratorOutputMappingCommands::Get().RotateViewport90CW);
		MenuBuilder.AddMenuEntry(FDisplayClusterConfiguratorOutputMappingCommands::Get().RotateViewport180);
		MenuBuilder.AddSeparator();
		MenuBuilder.AddMenuEntry(FDisplayClusterConfiguratorOutputMappingCommands::Get().FlipViewportHorizontal);
		MenuBuilder.AddMenuEntry(FDisplayClusterConfiguratorOutputMappingCommands::Get().FlipViewportVertical);
		MenuBuilder.AddSeparator();
		MenuBuilder.AddMenuEntry(FDisplayClusterConfiguratorOutputMappingCommands::Get().ResetViewportTransform);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SDisplayClusterConfiguratorOutputMappingToolbar::MakePositioningMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = false;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, CommandList);

	MenuBuilder.BeginSection(TEXT("OverlapBoundsSection"), LOCTEXT("OverlapBoundsSectionLabel", "Overlap & Bounds"));
	{
		MenuBuilder.AddMenuEntry(FDisplayClusterConfiguratorOutputMappingCommands::Get().ToggleClusterItemOverlap);
		MenuBuilder.AddMenuEntry(FDisplayClusterConfiguratorOutputMappingCommands::Get().ToggleLockClusterNodesInHosts);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(TEXT("LockingSection"), LOCTEXT("LockingSectionLabel", "Locking"));
	{
		MenuBuilder.AddMenuEntry(FDisplayClusterConfiguratorOutputMappingCommands::Get().ToggleLockClusterNodes);
		MenuBuilder.AddMenuEntry(FDisplayClusterConfiguratorOutputMappingCommands::Get().ToggleLockViewports);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SDisplayClusterConfiguratorOutputMappingToolbar::MakeSnappingMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = false;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, CommandList);

	MenuBuilder.BeginSection(TEXT("AdjacentEdgeSnapping"));
	{
		MenuBuilder.AddMenuEntry(FDisplayClusterConfiguratorOutputMappingCommands::Get().ToggleAdjacentEdgeSnapping);

		MenuBuilder.AddWidget(
			SNew(SBox)
			.HAlign(HAlign_Right)
			[
				SNew(SBox)
				.Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
				.WidthOverride(100.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("Menu.WidgetBorder"))
					.Padding(FMargin(1.0f))
					[
						SNew(SNumericEntryBox<int>)
						.Value(this, &SDisplayClusterConfiguratorOutputMappingToolbar::GetAdjacentEdgePadding)
						.OnValueChanged(this, &SDisplayClusterConfiguratorOutputMappingToolbar::SetAdjacentEdgePadding)
						.IsEnabled(this, &SDisplayClusterConfiguratorOutputMappingToolbar::IsAdjacentEdgeSnappingEnabled)
						.MinValue(0)
						.MaxValue(INT_MAX)
						.MaxSliderValue(100)
						.AllowSpin(true)
					]
				]
			],
			LOCTEXT("AlignmentSettings_AdjacentEdgePadding", "Adjacent Edge Padding")
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(TEXT("SameEdgeSnapping"));
	{
		MenuBuilder.AddMenuEntry(FDisplayClusterConfiguratorOutputMappingCommands::Get().ToggleSameEdgeSnapping);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(TEXT("GeneralSnapping"));
	{
		MenuBuilder.AddWidget(
			SNew(SBox)
			.HAlign(HAlign_Right)
			[
				SNew(SBox)
				.Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
				.WidthOverride(100.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("Menu.WidgetBorder"))
					.Padding(FMargin(1.0f))
					[
						SNew(SNumericEntryBox<int>)
						.Value(this, &SDisplayClusterConfiguratorOutputMappingToolbar::GetSnapProximity)
						.OnValueChanged(this, &SDisplayClusterConfiguratorOutputMappingToolbar::SetSnapProximity)
						.IsEnabled(this, &SDisplayClusterConfiguratorOutputMappingToolbar::IsSnappingEnabled)
						.MinValue(0)
						.MaxValue(INT_MAX)
						.MaxSliderValue(100)
						.AllowSpin(true)
					]
				]
			],
			LOCTEXT("AlignmentSettings_SnapProximity", "Snap Proximity")
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

bool SDisplayClusterConfiguratorOutputMappingToolbar::IsSnappingEnabled() const
{
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping = ViewOutputMappingPtr.Pin();
	check(ViewOutputMapping != nullptr);

	return ViewOutputMapping->GetNodeAlignmentSettings().bSnapAdjacentEdges || ViewOutputMapping->GetNodeAlignmentSettings().bSnapSameEdges;
}

bool SDisplayClusterConfiguratorOutputMappingToolbar::IsAdjacentEdgeSnappingEnabled() const
{
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping = ViewOutputMappingPtr.Pin();
	check(ViewOutputMapping != nullptr);

	return ViewOutputMapping->GetNodeAlignmentSettings().bSnapAdjacentEdges;
}

TOptional<int> SDisplayClusterConfiguratorOutputMappingToolbar::GetAdjacentEdgePadding() const
{
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping = ViewOutputMappingPtr.Pin();
	check(ViewOutputMapping != nullptr);

	return ViewOutputMapping->GetNodeAlignmentSettings().AdjacentEdgesSnapPadding;
}

void SDisplayClusterConfiguratorOutputMappingToolbar::SetAdjacentEdgePadding(int NewPadding)
{
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping = ViewOutputMappingPtr.Pin();
	check(ViewOutputMapping != nullptr);

	ViewOutputMapping->GetNodeAlignmentSettings().AdjacentEdgesSnapPadding = NewPadding;
}

TOptional<int> SDisplayClusterConfiguratorOutputMappingToolbar::GetSnapProximity() const
{
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping = ViewOutputMappingPtr.Pin();
	check(ViewOutputMapping != nullptr);

	return ViewOutputMapping->GetNodeAlignmentSettings().SnapProximity;
}

void SDisplayClusterConfiguratorOutputMappingToolbar::SetSnapProximity(int NewSnapProximity)
{
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping = ViewOutputMappingPtr.Pin();
	check(ViewOutputMapping != nullptr);

	ViewOutputMapping->GetNodeAlignmentSettings().SnapProximity = NewSnapProximity;
}

TSharedRef<SWidget> SDisplayClusterConfiguratorOutputMappingToolbar::MakeAdvancedMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = false;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, CommandList);

	MenuBuilder.BeginSection(TEXT("General"), LOCTEXT("GeneralSectionLabel", "General"));
	{
		MenuBuilder.AddMenuEntry(FDisplayClusterConfiguratorOutputMappingCommands::Get().ToggleTintViewports);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(TEXT("Hosts"), LOCTEXT("HostsSectionLabel", "Hosts"));
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("HostArrangementSubMenuLabel", "Host Arrangement"),
			LOCTEXT("HostArrangementSubMenuToolTip", "Indicates how hosts are arranged on the graph editor"),
			FNewMenuDelegate::CreateSP(this, &SDisplayClusterConfiguratorOutputMappingToolbar::MakeHostArrangementTypeSubMenu)
		);

		
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SDisplayClusterConfiguratorOutputMappingToolbar::MakeHostArrangementTypeSubMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection(TEXT("HostArrangementType"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("HostArrangementHorizontalLabel", "Horizontal"),
			LOCTEXT("HostArrangementHorizontalToolTip", "Arranges hosts horizontally on the graph editor"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorOutputMappingToolbar::SetHostArrangementType, EHostArrangementType::Horizontal),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SDisplayClusterConfiguratorOutputMappingToolbar::IsHostArrangementTypeChecked, EHostArrangementType::Horizontal)
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("HostArrangementVerticalLabel", "Vertical"),
			LOCTEXT("HostArrangementVerticalToolTip", "Arranges hosts vertically on the graph editor"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorOutputMappingToolbar::SetHostArrangementType, EHostArrangementType::Vertical),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SDisplayClusterConfiguratorOutputMappingToolbar::IsHostArrangementTypeChecked, EHostArrangementType::Vertical)
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("HostArrangementWrapLabel", "Wrap"),
			LOCTEXT("HostArrangementWrapToolTip", "Arranges hosts horizontally on the graph editor until the threshold is reached, then wraps them"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorOutputMappingToolbar::SetHostArrangementType, EHostArrangementType::Wrap),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SDisplayClusterConfiguratorOutputMappingToolbar::IsHostArrangementTypeChecked, EHostArrangementType::Wrap)
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("HostArrangementGridLabel", "Grid"),
			LOCTEXT("HostArrangementGridToolTip", "Arranges hosts in a grid on the graph editor"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorOutputMappingToolbar::SetHostArrangementType, EHostArrangementType::Grid),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SDisplayClusterConfiguratorOutputMappingToolbar::IsHostArrangementTypeChecked, EHostArrangementType::Grid)
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(TEXT("HostArrangementSettings"));
	{
		MenuBuilder.AddWidget(
			SNew(SBox)
			.HAlign(HAlign_Right)
			[
				SNew(SBox)
				.Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
				.WidthOverride(100.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("Menu.WidgetBorder"))
					.Padding(FMargin(1.0f))
					[
						SNew(SNumericEntryBox<int>)
						.Value(this, &SDisplayClusterConfiguratorOutputMappingToolbar::GetHostWrapThreshold)
						.OnValueChanged(this, &SDisplayClusterConfiguratorOutputMappingToolbar::SetHostWrapThreshold)
						.IsEnabled(this, &SDisplayClusterConfiguratorOutputMappingToolbar::IsHostArrangementTypeChecked, EHostArrangementType::Wrap)
						.MinValue(0)
						.MaxValue(INT_MAX)
						.MaxSliderValue(10000)
						.AllowSpin(true)
					]
				]
			],
			LOCTEXT("HostArrangementSettings_WrapThreshold", "Wrapping Threshold")
		);

		MenuBuilder.AddWidget(
			SNew(SBox)
			.HAlign(HAlign_Right)
			[
				SNew(SBox)
				.Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
				.WidthOverride(100.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("Menu.WidgetBorder"))
					.Padding(FMargin(1.0f))
					[
						SNew(SNumericEntryBox<int>)
						.Value(this, &SDisplayClusterConfiguratorOutputMappingToolbar::GetHostGridSize)
						.OnValueChanged(this, &SDisplayClusterConfiguratorOutputMappingToolbar::SetHostGridSize)
						.IsEnabled(this, &SDisplayClusterConfiguratorOutputMappingToolbar::IsHostArrangementTypeChecked, EHostArrangementType::Grid)
						.MinValue(1)
						.MaxValue(10)
						.MinSliderValue(1)
						.MaxSliderValue(5)
						.AllowSpin(true)
					]
				]
			],
			LOCTEXT("HostArrangementSettings_GridSize", "Grid Size")
		);
	}
	MenuBuilder.EndSection();
}

bool SDisplayClusterConfiguratorOutputMappingToolbar::IsHostArrangementTypeChecked(EHostArrangementType ArrangementType) const
{
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping = ViewOutputMappingPtr.Pin();
	check(ViewOutputMapping != nullptr);

	return ViewOutputMapping->GetHostArrangementSettings().ArrangementType == ArrangementType;
}

void SDisplayClusterConfiguratorOutputMappingToolbar::SetHostArrangementType(EHostArrangementType ArrangementType)
{
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping = ViewOutputMappingPtr.Pin();
	check(ViewOutputMapping != nullptr);

	ViewOutputMapping->GetHostArrangementSettings().ArrangementType = ArrangementType;
}

TOptional<int> SDisplayClusterConfiguratorOutputMappingToolbar::GetHostWrapThreshold() const
{
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping = ViewOutputMappingPtr.Pin();
	check(ViewOutputMapping != nullptr);

	return ViewOutputMapping->GetHostArrangementSettings().WrapThreshold;
}

void SDisplayClusterConfiguratorOutputMappingToolbar::SetHostWrapThreshold(int NewWrapThreshold)
{
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping = ViewOutputMappingPtr.Pin();
	check(ViewOutputMapping != nullptr);

	ViewOutputMapping->GetHostArrangementSettings().WrapThreshold = NewWrapThreshold;
}

TOptional<int> SDisplayClusterConfiguratorOutputMappingToolbar::GetHostGridSize() const
{
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping = ViewOutputMappingPtr.Pin();
	check(ViewOutputMapping != nullptr);

	return ViewOutputMapping->GetHostArrangementSettings().GridSize;
}

void SDisplayClusterConfiguratorOutputMappingToolbar::SetHostGridSize(int NewGridSize)
{
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping = ViewOutputMappingPtr.Pin();
	check(ViewOutputMapping != nullptr);

	ViewOutputMapping->GetHostArrangementSettings().GridSize = NewGridSize;
}

TSharedRef<SWidget> SDisplayClusterConfiguratorOutputMappingToolbar::MakeViewScaleMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = false;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, CommandList);

	for (int Index = 0; Index < ViewScales.Num(); ++Index)
	{
		const float ViewScale = ViewScales[Index];

		FUIAction UIAction(
			FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorOutputMappingToolbar::SetViewScale, Index),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &SDisplayClusterConfiguratorOutputMappingToolbar::IsViewScaleChecked, Index)
		);

		MenuBuilder.AddMenuEntry(
			FText::AsNumber(ViewScale),
			FText::Format(LOCTEXT("PixelRatio_Tooltip", "Sets the visual scale to {0}"), FText::AsNumber(ViewScale)),
			FSlateIcon(),
			UIAction,
			NAME_None,
			EUserInterfaceActionType::RadioButton 
		);
	}

	return MenuBuilder.MakeWidget();
}

bool SDisplayClusterConfiguratorOutputMappingToolbar::IsViewScaleChecked(int32 Index) const
{
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping = ViewOutputMappingPtr.Pin();
	check(ViewOutputMapping != nullptr);

	const float CurrentViewScale = ViewOutputMapping->GetOutputMappingSettings().ViewScale;
	
	return FMath::IsNearlyEqual(CurrentViewScale, ViewScales[Index]);
}

void SDisplayClusterConfiguratorOutputMappingToolbar::SetViewScale(int32 Index)
{
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping = ViewOutputMappingPtr.Pin();
	check(ViewOutputMapping != nullptr);

	ViewOutputMapping->GetOutputMappingSettings().ViewScale = ViewScales[Index];

	ViewOutputMapping->RefreshNodePositions();
}

FText SDisplayClusterConfiguratorOutputMappingToolbar::GetViewScaleText() const
{
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping = ViewOutputMappingPtr.Pin();
	check(ViewOutputMapping != nullptr);

	const float ViewScale = ViewOutputMapping->GetOutputMappingSettings().ViewScale;
	return FText::AsNumber(ViewScale);
}

#undef LOCTEXT_NAMESPACE
