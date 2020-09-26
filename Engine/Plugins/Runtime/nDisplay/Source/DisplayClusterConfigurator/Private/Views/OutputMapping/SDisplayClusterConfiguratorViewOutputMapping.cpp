// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/SDisplayClusterConfiguratorViewOutputMapping.h"

#include "DisplayClusterConfiguratorCommands.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "Interfaces/IDisplayClusterConfigurator.h"
#include "Views/OutputMapping/DisplayClusterConfiguratorViewOutputMapping.h"
#include "Views/OutputMapping/SDisplayClusterConfiguratorGraphEditor.h"
#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorOutputMappingToolbar.h"
#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorRuler.h"

#include "Framework/Commands/UICommandList.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SSpacer.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterConfiguratorViewOutputMapping"

void SDisplayClusterConfiguratorViewOutputMapping::Construct(const FArguments& InArgs, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit, const TSharedRef<SDisplayClusterConfiguratorGraphEditor>& InGraphEditor, const TSharedRef<FDisplayClusterConfiguratorViewOutputMapping> InViewOutputMapping)
{
	ToolkitPtr = InToolkit;
	GraphEditorPtr = InGraphEditor;
	ViewOutputMappingPtr = InViewOutputMapping;

	BindCommands();

	SDisplayClusterConfiguratorViewBase::Construct(
		SDisplayClusterConfiguratorViewBase::FArguments()
		.Content()
		[

			SNew(SGridPanel)
			.FillColumn(1, 1.0f)
			.FillRow(1, 1.0f)

			// Corner
			+ SGridPanel::Slot(0, 0)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(FLinearColor(FColor(48, 48, 48)))
				.Visibility(this, &SDisplayClusterConfiguratorViewOutputMapping::GetRulerVisibility)
			]

			// Top Ruler
			+ SGridPanel::Slot(1, 0)
			[
				SAssignNew(TopRuler, SDisplayClusterConfiguratorRuler)
				.Orientation(Orient_Horizontal)
				.Visibility(this, &SDisplayClusterConfiguratorViewOutputMapping::GetRulerVisibility)
			]

			// Side Ruler
			+ SGridPanel::Slot(0, 1)
			[
				SAssignNew(SideRuler, SDisplayClusterConfiguratorRuler)
				.Orientation(Orient_Vertical)
				.Visibility(this, &SDisplayClusterConfiguratorViewOutputMapping::GetRulerVisibility)
			]

			// Graph area
			+ SGridPanel::Slot(1, 1)
			[
				SNew(SOverlay)
				.Visibility(EVisibility::Visible)
				.Clipping(EWidgetClipping::ClipToBoundsAlways)

				+SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SAssignNew(PreviewSurface, SDPIScaler)
					.DPIScale(1.f)
					[
						InGraphEditor
					]
				]

				+ SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					CreateOverlayUI()
				]
			]
		],
		InToolkit);

	// Set initial zoom
	FVector2D GraphEditorLocation = FVector2D::ZeroVector;
	float GraphEditorZoomAmount = 0;
	InGraphEditor->GetViewLocation(GraphEditorLocation, GraphEditorZoomAmount);
	InGraphEditor->SetViewLocation(GraphEditorLocation, 0.3f);
}

void SDisplayClusterConfiguratorViewOutputMapping::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	TSharedPtr<SDisplayClusterConfiguratorGraphEditor> GraphEditor = GraphEditorPtr.Pin();
	check(GraphEditor.IsValid());

	FVector2D GraphEditorLocation = FVector2D::ZeroVector;
	float GraphEditorZoomAmount = 0;
	GraphEditor->GetViewLocation(GraphEditorLocation, GraphEditorZoomAmount);

	// Compute the origin in absolute space.
	FGeometry RootGeometry = PreviewSurface->GetCachedGeometry();
	FVector2D AbsoluteOrigin = MakeGeometryWindowLocal(RootGeometry).LocalToAbsolute(FVector2D::ZeroVector);
	FVector2D AbsoluteOriginWithOffset = AbsoluteOrigin + (-GraphEditorLocation * GraphEditorZoomAmount);

	TopRuler->SetRuling(AbsoluteOriginWithOffset, 1.f / GraphEditorZoomAmount);
	SideRuler->SetRuling(AbsoluteOriginWithOffset, 1.f / GraphEditorZoomAmount);

	if (IsHovered())
	{
		// Get cursor in absolute window space.
		FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();
		CursorPos = MakeGeometryWindowLocal(RootGeometry).LocalToAbsolute(RootGeometry.AbsoluteToLocal(CursorPos));

		TopRuler->SetCursor(CursorPos);
		SideRuler->SetCursor(CursorPos);
	}
	else
	{
		TopRuler->SetCursor(TOptional<FVector2D>());
		SideRuler->SetCursor(TOptional<FVector2D>());
	}
}

FGeometry SDisplayClusterConfiguratorViewOutputMapping::MakeGeometryWindowLocal(const FGeometry& WidgetGeometry) const
{
	FGeometry NewGeometry = WidgetGeometry;

	TSharedPtr<SWindow> WidgetWindow = FSlateApplication::Get().FindWidgetWindow(SharedThis(this));
	if (WidgetWindow.IsValid())
	{
		TSharedRef<SWindow> CurrentWindowRef = WidgetWindow.ToSharedRef();

		NewGeometry.AppendTransform(FSlateLayoutTransform(Inverse(CurrentWindowRef->GetPositionInScreen())));
	}

	return NewGeometry;
}

EVisibility SDisplayClusterConfiguratorViewOutputMapping::GetRulerVisibility() const
{
	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping = ViewOutputMappingPtr.Pin();
	check(ViewOutputMapping.IsValid());

	if (ViewOutputMapping->IsRulerVisible())
	{
		return EVisibility::Visible;
	}

	return EVisibility::Hidden;
}

TSharedRef<SWidget> SDisplayClusterConfiguratorViewOutputMapping::CreateOverlayUI()
{
	return SNew(SOverlay)

	// Top bar with buttons for changing the designer
	+ SOverlay::Slot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Top)
	.Padding(2.f)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SDisplayClusterConfiguratorOutputMappingToolbar)
			.CommandList(CommandList)
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SSpacer)
			.Size(FVector2D(1, 1))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 2, 100, 2)
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), "Graph.ZoomText")
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("BoldCondensed"), 14))
			.Text(this, &SDisplayClusterConfiguratorViewOutputMapping::GetCursorPositionText)
			.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.25f))
			.Visibility(this, &SDisplayClusterConfiguratorViewOutputMapping::GetCursorPositionTextVisibility)
		]
	];
}
FText SDisplayClusterConfiguratorViewOutputMapping::GetCursorPositionText() const
{
	TSharedPtr<SDisplayClusterConfiguratorGraphEditor> GraphEditor = GraphEditorPtr.Pin();
	check(GraphEditor.IsValid());

	FGeometry RootGeometry = PreviewSurface->GetCachedGeometry();
	const FVector2D CursorPos = RootGeometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());

	FVector2D GraphEditorLocation = FVector2D::ZeroVector;
	float GraphEditorZoomAmount = 0;
	GraphEditor->GetViewLocation(GraphEditorLocation, GraphEditorZoomAmount);

	FVector2D GraphPosition = (CursorPos / GraphEditorZoomAmount) + GraphEditorLocation;

	return FText::Format(LOCTEXT("CursorPositionFormat", "{0} x {1}"), FText::AsNumber(FMath::RoundToInt(GraphPosition.X)), FText::AsNumber(FMath::RoundToInt(GraphPosition.Y)));
}

EVisibility SDisplayClusterConfiguratorViewOutputMapping::GetCursorPositionTextVisibility() const
{
	return IsHovered() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
}

void SDisplayClusterConfiguratorViewOutputMapping::ZoomToFit()
{
	GraphEditorPtr.Pin()->ZoomToFit(false);
}

void SDisplayClusterConfiguratorViewOutputMapping::BindCommands()
{
	CommandList = MakeShareable(new FUICommandList);

	const FDisplayClusterConfiguratorCommands& Commands = IDisplayClusterConfigurator::Get().GetCommands();

	TSharedPtr<FDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping = ViewOutputMappingPtr.Pin();
	check(ViewOutputMapping != nullptr);

	CommandList->MapAction(
		Commands.ToggleWindowInfo,
		FExecuteAction::CreateSP(ViewOutputMapping.Get(), &FDisplayClusterConfiguratorViewOutputMapping::ToggleShowWindowInfo),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(ViewOutputMapping.Get(), &FDisplayClusterConfiguratorViewOutputMapping::IsShowWindowInfo)
		);

	CommandList->MapAction(
		Commands.ToggleWindowCornerImage,
		FExecuteAction::CreateSP(ViewOutputMapping.Get(), &FDisplayClusterConfiguratorViewOutputMapping::ToggleShowWindowCornerImage),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(ViewOutputMapping.Get(), &FDisplayClusterConfiguratorViewOutputMapping::IsShowWindowCornerImage)
		);

	CommandList->MapAction(
		Commands.ToggleOutsideViewports,
		FExecuteAction::CreateSP(ViewOutputMapping.Get(), &FDisplayClusterConfiguratorViewOutputMapping::ToggleShowOutsideViewports),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(ViewOutputMapping.Get(), &FDisplayClusterConfiguratorViewOutputMapping::IsShowOutsideViewports)
		);

	CommandList->MapAction(
		Commands.ZoomToFit,
		FExecuteAction::CreateSP(this, &SDisplayClusterConfiguratorViewOutputMapping::ZoomToFit)
		);
}


#undef LOCTEXT_NAMESPACE