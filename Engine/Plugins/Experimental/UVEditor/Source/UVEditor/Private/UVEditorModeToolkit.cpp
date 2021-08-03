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
#include "UVEditorMode.h"
#include "UVEditorBackgroundPreview.h"

#include "EdModeInteractiveToolsContext.h"

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
			[
				SAssignNew(ToolMessageArea, STextBlock)
				.AutoWrapText(true)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.Text(LOCTEXT("UVEditorToolsLabel", "UV Editor Tools"))
			]

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
				.AutoHeight()
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

            // Todo: Move this out of here once we figure out how to add menus/UI to the viewport
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2, 0, 0, 0)
			[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew(ToolMessageArea, STextBlock)
						.AutoWrapText(true)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.Text(LOCTEXT("UVEditorSettingsLabel", "UV Editor Settings"))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew(EditorDetailsContainer, SBorder)
						.BorderImage(FEditorStyle::GetBrush("NoBorder"))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew(BackgroundDetailsContainer, SBorder)
						.BorderImage(FEditorStyle::GetBrush("NoBorder"))
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

	// Hook up the editor detail panel
	EditorDetailsContainer->SetContent(ModeDetailsView.ToSharedRef());

	// Hook up the background detail panel
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs BackgroundDetailsViewArgs;
		BackgroundDetailsViewArgs.bAllowSearch = false;
		BackgroundDetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		BackgroundDetailsViewArgs.bHideSelectionTip = true;
		BackgroundDetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
		BackgroundDetailsViewArgs.bShowOptions = false;
		BackgroundDetailsViewArgs.bAllowMultipleTopLevelObjects = true;

		CustomizeDetailsViewArgs(BackgroundDetailsViewArgs);		// allow subclass to customize arguments

		BackgroundDetailsView = PropertyEditorModule.CreateDetailView(BackgroundDetailsViewArgs);
		UUVEditorMode* UVEditorMode = Cast<UUVEditorMode>(OwningEditorMode.Get());
		BackgroundDetailsContainer->SetContent(BackgroundDetailsView.ToSharedRef());
	}
	
}

FName FUVEditorModeToolkit::GetToolkitFName() const
{
	return FName("UVEditorMode");
}

FText FUVEditorModeToolkit::GetBaseToolkitName() const
{
	return NSLOCTEXT("UVEditorModeToolkit", "DisplayName", "UVEditorMode");
}


void FUVEditorModeToolkit::SetBackgroundSettings(UObject* InSettingsObject)
{
	BackgroundDetailsView->SetObject(InSettingsObject);
}


#undef LOCTEXT_NAMESPACE
