// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUVEditor2DViewportToolBar.h"

#include "EditorViewportCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "UVEditorCommands.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"


void SUVEditor2DViewportToolBar::Construct(const FArguments& InArgs)
{
	CommandList = InArgs._CommandList;

	const FMargin ToolbarSlotPadding(4.0f, 1.0f);

	ChildSlot
	[
		SNew( SBorder )
		.BorderImage(FAppStyle::Get().GetBrush("EditorViewportToolBar.Background"))
		.Cursor(EMouseCursor::Default)
		[
			SNew( SHorizontalBox )

			// The first slot is just a spacer so that we get three evenly spaced columns 
			// and the selection toolbar can go in the center of the center one.
			+ SHorizontalBox::Slot()
			.Padding(ToolbarSlotPadding)
			.HAlign(HAlign_Right)

			+ SHorizontalBox::Slot()
			.Padding(ToolbarSlotPadding)
			.HAlign(HAlign_Center)
			[
				MakeSelectionToolBar(InArgs._Extenders)
			]

			+ SHorizontalBox::Slot()
			.Padding(ToolbarSlotPadding)
			.HAlign(HAlign_Right)
			[
				MakeGizmoToolBar(InArgs._Extenders)
			]
		]
	];

	SViewportToolBar::Construct(SViewportToolBar::FArguments());
}

TSharedRef<SWidget> SUVEditor2DViewportToolBar::MakeSelectionToolBar(const TSharedPtr<FExtender> InExtenders)
{

	FSlimHorizontalToolBarBuilder ToolbarBuilder(CommandList, FMultiBoxCustomization::None, InExtenders);

	// Use a custom style
	FName ToolBarStyle = "EditorViewportToolBar";
	ToolbarBuilder.SetStyle(&FEditorStyle::Get(), ToolBarStyle);
	ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);

	// Transform controls should not be focusable as it fights with the press space to change transform 
	// mode feature, which we may someday have.
	ToolbarBuilder.SetIsFocusable(false);

	// Widget controls
	ToolbarBuilder.BeginSection("SelectionModes");
	{
		ToolbarBuilder.BeginBlockGroup();

		ToolbarBuilder.AddToolBarButton(FUVEditorCommands::Get().VertexSelection, NAME_None,
			TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), TEXT("VertexSelection"));

		ToolbarBuilder.AddToolBarButton(FUVEditorCommands::Get().EdgeSelection, NAME_None,
			TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), TEXT("EdgeSelection"));

		ToolbarBuilder.AddToolBarButton(FUVEditorCommands::Get().TriangleSelection, NAME_None,
			TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), TEXT("TriangleSelection"));

		ToolbarBuilder.AddToolBarButton(FUVEditorCommands::Get().IslandSelection, NAME_None,
			TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), TEXT("IslandSelection"));

		ToolbarBuilder.AddToolBarButton(FUVEditorCommands::Get().FullMeshSelection, NAME_None,
			TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), TEXT("FullMeshSelection"));

		ToolbarBuilder.EndBlockGroup();
	}

	ToolbarBuilder.EndSection();

	return ToolbarBuilder.MakeWidget();
}

TSharedRef<SWidget> SUVEditor2DViewportToolBar::MakeGizmoToolBar(const TSharedPtr<FExtender> InExtenders)
{
	// The following is modeled after portions of STransformViewportToolBar, which gets 
	// used in SCommonEditorViewportToolbarBase.

	// The buttons are hooked up to actual functions via command bindings in SEditorViewport::BindCommands(),
	// and the toolbar gets built in SUVEditor2DViewport::MakeViewportToolbar().

	FSlimHorizontalToolBarBuilder ToolbarBuilder(CommandList, FMultiBoxCustomization::None, InExtenders);

	// Use a custom style
	FName ToolBarStyle = "EditorViewportToolBar";
	ToolbarBuilder.SetStyle(&FEditorStyle::Get(), ToolBarStyle);
	ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);

	// Transform controls should not be focusable as it fights with the press space to change transform 
	// mode feature, which we may someday have.
	ToolbarBuilder.SetIsFocusable(false);

	// Widget controls
	ToolbarBuilder.BeginSection("Transform");
	{
		ToolbarBuilder.BeginBlockGroup();

		// Select Mode
		static FName SelectModeName = FName(TEXT("SelectMode"));
		ToolbarBuilder.AddToolBarButton(FEditorViewportCommands::Get().SelectMode, NAME_None, TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), SelectModeName);

		// Translate Mode
		static FName TranslateModeName = FName(TEXT("TranslateMode"));
		ToolbarBuilder.AddToolBarButton(FEditorViewportCommands::Get().TranslateMode, NAME_None, TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), TranslateModeName);

		ToolbarBuilder.EndBlockGroup();
	}

	ToolbarBuilder.EndSection();

	return ToolbarBuilder.MakeWidget();
}