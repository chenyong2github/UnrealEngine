// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorModeToolkit.h"

#include "EditorStyleSet.h" //FEditorStyle
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "IDetailsView.h"
#include "Tools/UEdMode.h"
#include "UVEditorCommands.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FUVEditorModeToolkit"

FUVEditorModeToolkit::FUVEditorModeToolkit()
{
	// Construct the panel that we will give in GetInlineContent().
	// This could probably be done in Init() instead, but constructor
	// makes it easy to guarantee that GetInlineContent() will always
	// be ready to work.

	SAssignNew(ToolkitWidget, SBorder)
		.HAlign(HAlign_Fill)
		.Padding(4)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Left)
			.Padding(1.f)
			[
				SAssignNew(ToolButtonsContainer, SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(FMargin(4, 2, 0, 0))
			]

			+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(2, 0, 0, 0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(ToolWarningArea, STextBlock)
					.AutoWrapText(true)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.15f, 0.15f))) //TODO: This probably needs to not be hardcoded
					.Text(FText::GetEmpty())
					.Visibility(EVisibility::Collapsed)
				]

				+ SVerticalBox::Slot()
				[
					SAssignNew(ToolDetailsContainer, SBorder)
					.BorderImage(FEditorStyle::GetBrush("NoBorder"))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(ToolMessageArea, STextBlock)
					.AutoWrapText(true)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.Text(FText::GetEmpty())
				]
			]
		];
}

FUVEditorModeToolkit::~FUVEditorModeToolkit()
{
}

void FUVEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{
	FModeToolkit::Init(InitToolkitHost, InOwningMode);	

	// Build the tool palette
	const FUVEditorCommands& CommandInfos = FUVEditorCommands::Get();
	TSharedRef<FUICommandList> CommandList = GetToolkitCommands();
	check(OwningEditorMode.IsValid());
	FUniformToolBarBuilder ToolbarBuilder(CommandList, FMultiBoxCustomization(OwningEditorMode->GetModeInfo().ToolbarCustomizationName), TSharedPtr<FExtender>(), false);
	ToolbarBuilder.SetStyle(&FEditorStyle::Get(), "PaletteToolBar");

	// TODO: Add more tools here
	ToolbarBuilder.AddToolBarButton(CommandInfos.BeginSelectTool);
	ToolbarBuilder.AddToolBarButton(CommandInfos.BeginTransformTool);

	// Hook in the tool palette
	ToolButtonsContainer->SetContent(ToolbarBuilder.MakeWidget());

	// Hook up the tool detail panel
	ToolDetailsContainer->SetContent(DetailsView.ToSharedRef());
}

FName FUVEditorModeToolkit::GetToolkitFName() const
{
	return FName("UVEditorMode");
}

FText FUVEditorModeToolkit::GetBaseToolkitName() const
{
	return NSLOCTEXT("UVEditorModeToolkit", "DisplayName", "UVEditorMode");
}

#undef LOCTEXT_NAMESPACE
