// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorOutputMappingToolbar.h"

#include "DisplayClusterConfiguratorCommands.h"
#include "DisplayClusterConfiguratorStyle.h"
#include "Interfaces/IDisplayClusterConfigurator.h"
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
	const FDisplayClusterConfiguratorCommands& Commands = IDisplayClusterConfigurator::Get().GetCommands();

	// Use a custom style
	FName ToolBarStyle = "ViewportMenu";
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
				.Image("EditorViewportToolBar.MenuDropdown")
				.OnGetMenuContent(this, &SDisplayClusterConfiguratorOutputMappingToolbar::MakeAdvancedMenu)
			]
		);
	}
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("View");
	{
		ToolbarBuilder.AddToolBarButton(Commands.ToggleWindowInfo,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.OutputMapping.ToggleWindowInfo"),
			"ToggleWindowInfo"
			);

		ToolbarBuilder.AddToolBarButton(Commands.ToggleWindowCornerImage,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.OutputMapping.ToggleWindowCornerImage"),
			"ToggleWindowCornerImage"
			);

		ToolbarBuilder.AddToolBarButton(Commands.ToggleOutsideViewports,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.OutputMapping.ToggleOutsideViewports"),
			"ToggleOutsideViewports"
			);

		ToolbarBuilder.AddToolBarButton(Commands.ZoomToFit,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FDisplayClusterConfiguratorStyle::GetStyleSetName(), "DisplayClusterConfigurator.OutputMapping.ZoomToFit"),
			"ZoomToFit"
			);

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

TSharedRef<SWidget> SDisplayClusterConfiguratorOutputMappingToolbar::MakePositioningMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = false;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, CommandList);

	MenuBuilder.BeginSection(TEXT("OverlapBoundsSection"), LOCTEXT("OverlapBoundsSectionLabel", "Overlap & Bounds"));
	{
		MenuBuilder.AddMenuEntry(IDisplayClusterConfigurator::Get().GetCommands().ToggleClusterItemOverlap);
		MenuBuilder.AddMenuEntry(IDisplayClusterConfigurator::Get().GetCommands().ToggleLockClusterNodesInHosts);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(TEXT("LockingSection"), LOCTEXT("LockingSectionLabel", "Locking"));
	{
		MenuBuilder.AddMenuEntry(IDisplayClusterConfigurator::Get().GetCommands().ToggleLockClusterNodes);
		MenuBuilder.AddMenuEntry(IDisplayClusterConfigurator::Get().GetCommands().ToggleLockViewports);
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
		MenuBuilder.AddMenuEntry(IDisplayClusterConfigurator::Get().GetCommands().ToggleAdjacentEdgeSnapping);

		MenuBuilder.AddWidget(
			SNew(SHorizontalBox)
			.IsEnabled(this, &SDisplayClusterConfiguratorOutputMappingToolbar::IsAdjacentEdgeSnappingEnabled)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(0.f, 0.f, 5.f, 0.f))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AlignmentSettings_AdjacentEdgePadding", "Adjacent Edge Padding"))
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Bottom)
			.FillWidth(1.f)
			[
				SNew(SNumericEntryBox<int>)
				.Value(this, &SDisplayClusterConfiguratorOutputMappingToolbar::GetAdjacentEdgePadding)
				.OnValueChanged(this, &SDisplayClusterConfiguratorOutputMappingToolbar::SetAdjacentEdgePadding)
				.MinValue(0)
				.MaxValue(INT_MAX)
				.MaxSliderValue(100)
				.AllowSpin(true)
			],
			FText::GetEmpty()
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(TEXT("SameEdgeSnapping"));
	{
		MenuBuilder.AddMenuEntry(IDisplayClusterConfigurator::Get().GetCommands().ToggleSameEdgeSnapping);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(TEXT("GeneralSnapping"));
	{
		MenuBuilder.AddWidget(
			SNew(SHorizontalBox)
			.IsEnabled(this, &SDisplayClusterConfiguratorOutputMappingToolbar::IsSnappingEnabled)
			
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(0.f, 0.f, 5.f, 0.f))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AlignmentSettings_SnapProximity", "Snap Proximity"))
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Bottom)
			.FillWidth(1.f)
			[
				SNew(SNumericEntryBox<int>)
				.Value(this, &SDisplayClusterConfiguratorOutputMappingToolbar::GetSnapProximity)
				.OnValueChanged(this, &SDisplayClusterConfiguratorOutputMappingToolbar::SetSnapProximity)
				.MinValue(0)
				.MaxValue(INT_MAX)
				.MaxSliderValue(100)
				.AllowSpin(true)
			],
			FText::GetEmpty()
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
		MenuBuilder.AddMenuEntry(IDisplayClusterConfigurator::Get().GetCommands().ToggleTintViewports);
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
			SNew(SHorizontalBox)
			.IsEnabled(this, &SDisplayClusterConfiguratorOutputMappingToolbar::IsHostArrangementTypeChecked, EHostArrangementType::Wrap)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(0.f, 0.f, 5.f, 0.f))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("HostArrangementSettings_WrapThreshold", "Wrapping Threshold"))
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Bottom)
			.FillWidth(1.f)
			[
				SNew(SNumericEntryBox<int>)
				.Value(this, &SDisplayClusterConfiguratorOutputMappingToolbar::GetHostWrapThreshold)
				.OnValueChanged(this, &SDisplayClusterConfiguratorOutputMappingToolbar::SetHostWrapThreshold)
				.MinValue(0)
				.MaxValue(INT_MAX)
				.MaxSliderValue(10000)
				.AllowSpin(true)
			],
			FText::GetEmpty()
		);

		MenuBuilder.AddWidget(
			SNew(SHorizontalBox)
			.IsEnabled(this, &SDisplayClusterConfiguratorOutputMappingToolbar::IsHostArrangementTypeChecked, EHostArrangementType::Grid)
			
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(0.f, 0.f, 5.f, 0.f))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("HostArrangementSettings_GridSize", "Grid Size"))
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Bottom)
			.FillWidth(1.f)
			[
				SNew(SNumericEntryBox<int>)
				.Value(this, &SDisplayClusterConfiguratorOutputMappingToolbar::GetHostGridSize)
				.OnValueChanged(this, &SDisplayClusterConfiguratorOutputMappingToolbar::SetHostGridSize)
				.MinValue(1)
				.MaxValue(10)
				.MinSliderValue(1)
				.MaxSliderValue(5)
				.AllowSpin(true)
			],
			FText::GetEmpty()
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
