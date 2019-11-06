// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMoviePipelinePanel.h"
#include "Widgets/SMoviePipelineEditor.h"
#include "Widgets/MoviePipelineWidgetConstants.h"
#include "Misc/MessageDialog.h"
#include "MoviePipelineShotConfig.h"

// Slate Includes
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/SlateIconFinder.h"
#include "EditorStyleSet.h"
#include "EditorFontGlyphs.h"


// ContentBrowser Includes
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"

// UnrealEd Includes
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "SMoviePipelinePanel"

SMoviePipelinePanel::~SMoviePipelinePanel()
{
}

PRAGMA_DISABLE_OPTIMIZATION
void SMoviePipelinePanel::Construct(const FArguments& InArgs)
{
	// Allocate a transient preset automatically so they can start editing without having to create an asset.
	TransientPreset = AllocateTransientPreset();

	// Copy the base preset into the transient preset if it was provided.
	if (InArgs._BasePreset)
	{
		// TransientPreset->CopyFrom(InArgs._BasePreset);
	}

	// Create the child widgets that need to know about our pipeline
	MoviePipelineEditorWidget = SNew(SMoviePipelineEditor)
	.MoviePipeline(this, &SMoviePipelinePanel::GetMoviePipeline);

	ChildSlot
	[
		SNew(SVerticalBox)

		// Create the toolbar for adding new stuff, choosing/saving a preset and resetting to the preset defaults.
		+ SVerticalBox::Slot()
		.Padding(FMargin(0.f, 1.0f))
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
			.BorderBackgroundColor(FLinearColor(.6, .6, .6, 1.0f))
			// .IsEnabled_Lambda([this]() { return !CockpitWidget->Reviewing(); })
			[
				SNew(SHorizontalBox)

				// Adding things to the Pipeline
				+ SHorizontalBox::Slot()
				.Padding(MoviePipeline::ButtonOffset)
				.VAlign(VAlign_Fill)
				.AutoWidth()
				[
					MoviePipelineEditorWidget->MakeAddSettingButton()
				]

				// Presets Management Button
				+ SHorizontalBox::Slot()
				.Padding(MoviePipeline::ButtonOffset)
				.VAlign(VAlign_Fill)
				.AutoWidth()
				[
					SNew(SComboButton)
					.ContentPadding(MoviePipeline::ButtonPadding)
					// .ComboButtonStyle(FTakeRecorderStyle::Get(), "ComboButton")
					.OnGetMenuContent(this, &SMoviePipelinePanel::OnGeneratePresetsMenu)
					.ForegroundColor(FSlateColor::UseForeground())
					.ButtonContent()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SImage)
							.Image(FSlateIconFinder::FindIconBrushForClass(UMoviePipelineConfigBase::StaticClass()))
						]

						+ SHorizontalBox::Slot()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("PresetsToolbarButton", "Presets"))
						]
					]
				]

				// Spacer to make the next widget right aligned
				+ SHorizontalBox::Slot()
				[
					SNew(SSpacer)
				]

				// A button to undo your mistakes
				+ SHorizontalBox::Slot()
				.Padding(MoviePipeline::ButtonOffset)
				.VAlign(VAlign_Fill)
				.AutoWidth()
				[
					SNew(SButton)
					.ContentPadding(MoviePipeline::ButtonPadding)
					.ToolTipText(LOCTEXT("RevertChanges_Text", "Revert all changes made to this take back its original state (either its original preset, or an empty take)."))
					.ForegroundColor(FSlateColor::UseForeground())
					.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
					.OnClicked(this, &SMoviePipelinePanel::OnRevertChanges)
					[
						SNew(STextBlock)
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
						.Text(FEditorFontGlyphs::Undo)
					]
				]
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				MoviePipelineEditorWidget.ToSharedRef()
			]
		]

		// Footer Bar
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
			.BorderBackgroundColor(FLinearColor(.6, .6, .6, 1.0f))
			// .IsEnabled_Lambda([this]() { return !CockpitWidget->Reviewing(); })
			[
				SNew(SHorizontalBox)

				// Spacer to make the next widget right aligned
				+ SHorizontalBox::Slot()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DummyValidationError", "This is where a validation error might show up?"))
				]

				// Attempt to kick off the pipeline if it's valid
				+ SHorizontalBox::Slot()
				.Padding(MoviePipeline::ButtonOffset)
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Right)
				.AutoWidth()
				[
					SNew(SButton)
					.ContentPadding(MoviePipeline::ButtonPadding)
					.Text(LOCTEXT("RenderPipeline_Text", "Render Movie"))
					.ToolTipText(LOCTEXT("RenderPipelineTooltip_Text", "Renders the selected sequence with the current output pipeline."))
					.ForegroundColor(FSlateColor::UseForeground())
					// .ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
					// .OnClicked(this, &SMoviePipelinePanel::OnRevertChanges)

				]
			]
		]
	];
}

PRAGMA_ENABLE_OPTIMIZATION

UMoviePipelineConfigBase* SMoviePipelinePanel::AllocateTransientPreset()
{
	static const TCHAR* PackageName = TEXT("/Temp/MoviePipeline/PendingPipeline");

	// Return a cached transient if it exists
	UMoviePipelineConfigBase* ExistingPreset = FindObject<UMoviePipelineConfigBase>(nullptr, TEXT("/Temp/MoviePipeline/PendingPipeline.PendingPipeline"));
	if (ExistingPreset)
	{
		return ExistingPreset;
	}

	static FName DesiredName = "PendingMoviePipeline";
	
	UPackage* NewPackage = CreatePackage(nullptr, PackageName);
	NewPackage->SetFlags(RF_Transient);
	NewPackage->AddToRoot();

	UMoviePipelineConfigBase* NewPreset = NewObject<UMoviePipelineConfigBase>(NewPackage, DesiredName, RF_Transient | RF_Transactional | RF_Standalone);

	return NewPreset;
}

TSharedRef<SWidget> SMoviePipelinePanel::OnGeneratePresetsMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("SaveAsPreset_Text", "Save As Preset"),
		LOCTEXT("SaveAsPreset_Tip", "Save the current setup as a new preset that can be imported at a later date"),
		FSlateIcon(FEditorStyle::Get().GetStyleSetName(), "AssetEditor.SaveAsset.Greyscale"),
		FUIAction(
			// FExecuteAction::CreateSP(this, &STakeRecorderPanel::OnSaveAsPreset)
		)
	);

	FAssetPickerConfig AssetPickerConfig;
	{
		AssetPickerConfig.SelectionMode = ESelectionMode::Single;
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::Column;
		AssetPickerConfig.bFocusSearchBoxWhenOpened = true;
		AssetPickerConfig.bAllowNullSelection = false;
		AssetPickerConfig.bShowBottomToolbar = true;
		AssetPickerConfig.bAutohideSearchBar = false;
		AssetPickerConfig.bAllowDragging = false;
		AssetPickerConfig.bCanShowClasses = false;
		AssetPickerConfig.bShowPathInColumnView = true;
		AssetPickerConfig.bShowTypeInColumnView = false;
		AssetPickerConfig.bSortByPathInColumnView = false;

		AssetPickerConfig.AssetShowWarningText = LOCTEXT("NoPresets_Warning", "No Presets Found");
		AssetPickerConfig.Filter.ClassNames.Add(UMoviePipelineConfigBase::StaticClass()->GetFName());
		// AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SMoviePipelinePanel::OnImportPreset);
	}

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("ImportPreset_MenuSection", "Import Preset"));
	{
		TSharedRef<SWidget> PresetPicker = SNew(SBox)
			.MinDesiredWidth(400.f)
			.MinDesiredHeight(400.f)
			[
				ContentBrowser.CreateAssetPicker(AssetPickerConfig)
			];

		MenuBuilder.AddWidget(PresetPicker, FText(), true, false);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

FReply SMoviePipelinePanel::OnRevertChanges()
{
	FText WarningMessage(LOCTEXT("Warning_RevertChanges", "Are you sure you want to revert changes? Your current changes will be discarded."));
	if (EAppReturnType::No == FMessageDialog::Open(EAppMsgType::YesNo, WarningMessage))
	{
		return FReply::Handled();
	}

	// UMovieRenderPipeline* PresetOrigin = CockpitWidget->GetMetaData()->GetPresetOrigin();

	FScopedTransaction Transaction(LOCTEXT("RevertChanges_Transaction", "Revert Changes"));

	// TransientPreset->Modify();
	// TransientPreset->CopyFrom(PresetOrigin);

	return FReply::Handled();
}

void SMoviePipelinePanel::AddReferencedObjects(FReferenceCollector& Collector)
{
	// Collector.AddReferencedObject(TransientPreset);
	// Collector.AddReferencedObject(SuppliedLevelSequence);
	// Collector.AddReferencedObject(RecordingLevelSequence);
}

UMoviePipelineConfigBase* SMoviePipelinePanel::GetMoviePipeline() const
{
	return TransientPreset;
}

#undef LOCTEXT_NAMESPACE // SMoviePipelinePanel