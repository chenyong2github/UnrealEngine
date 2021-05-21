// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMoviePipelineConfigEditor.h"
#include "Widgets/MoviePipelineWidgetConstants.h"
#include "Widgets/SMoviePipelineConfigSettings.h"
#include "MoviePipelineShotConfig.h"
#include "MoviePipelineSetting.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineUtils.h"

// Core includes
#include "UObject/UObjectIterator.h"
#include "Templates/SubclassOf.h"
#include "ClassIconFinder.h"

// AssetRegistry includes
#include "AssetRegistryModule.h"
#include "AssetData.h"
 
// ContentBrowser includes
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
 
// AssetTools includes
#include "AssetToolsModule.h"
 
// Slate includes
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/SlateIconFinder.h"

// EditorStyle includes
#include "EditorStyleSet.h"
#include "EditorFontGlyphs.h"
#include "ScopedTransaction.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "MovieRenderPipelineStyle.h"
#include "FrameNumberDetailsCustomization.h"

#define LOCTEXT_NAMESPACE "SMoviePipelineEditor"


PRAGMA_DISABLE_OPTIMIZATION
void SMoviePipelineConfigEditor::Construct(const FArguments& InArgs)
{
	bRequestDetailsRefresh = true;
	PipelineConfigAttribute = InArgs._PipelineConfig;
	OwningJobAttribute = InArgs._OwningJob;
    
	DetailsBox = SNew(SScrollBox);
	DetailsBox->SetScrollBarRightClickDragAllowed(true);
     
	SettingsWidget = SNew(SMoviePipelineConfigSettings)
	.OnSelectionChanged(this, &SMoviePipelineConfigEditor::OnSettingsSelectionChanged);
    
	CheckForNewSettingsObject();
     
	if (CachedPipelineConfig->GetUserSettings().Num() > 0)
	{
		// Automatically try to select the first setting so there is something displayed.
		SettingsWidget->SetSelectedSettings({ CachedPipelineConfig->GetUserSettings()[0] });
	}
	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
    
		+ SSplitter::Slot()
		.Value(.33f)
		[
			SNew(SBorder)
			.Padding(4)
			.BorderImage( FEditorStyle::GetBrush("ToolPanel.GroupBorder") )
			[
				SettingsWidget.ToSharedRef()
			]
		]
    
		+ SSplitter::Slot()
		.Value(.67f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				DetailsBox.ToSharedRef()
			]

			+ SVerticalBox::Slot()
			.Padding(FMargin(0, 4, 0, 0))
			.AutoHeight()
			[
				SNew(SBorder)
				.Visibility(this, &SMoviePipelineConfigEditor::IsSettingFooterVisible)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SBox)
					.MaxDesiredHeight(96)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Text(this, &SMoviePipelineConfigEditor::GetSettingsFooterText)
						]
					]	
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				.Visibility(this, &SMoviePipelineConfigEditor::IsValidationWarningVisible)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(2, 0, 4, 0)
					[
						SNew(STextBlock)
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
						.Text(FEditorFontGlyphs::Exclamation_Triangle)
						.ColorAndOpacity(FLinearColor::Yellow)
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Text(this, &SMoviePipelineConfigEditor::GetValidationWarningText)
					]
				]
			]
		]
	];
}

TSharedRef<SWidget> SMoviePipelineConfigEditor::MakeAddSettingButton()
{
	return SNew(SComboButton)
		.ContentPadding(MoviePipeline::ButtonPadding)
		.ButtonStyle(FMovieRenderPipelineStyle::Get(), "FlatButton.Success")
		.OnGetMenuContent(this, &SMoviePipelineConfigEditor::OnGenerateSettingsMenu)
		.ForegroundColor(FSlateColor::UseForeground())
		.HasDownArrow(false)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
    
			// Plus Icon
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), "NormalText.Important")
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
				.Text(FEditorFontGlyphs::Plus)
			]
    
			// "Setting" Text
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(4, 0, 0, 0)
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), "NormalText.Important")
				.Text(LOCTEXT("AddNewSetting_Text", "Setting"))
			]
    
			// Non-Default Down Caret arrow.
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(4, 0, 0, 0)
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), "NormalText.Important")
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
				.Text(FEditorFontGlyphs::Caret_Down)
			]
		];
}
PRAGMA_ENABLE_OPTIMIZATION

void SMoviePipelineConfigEditor::CheckForNewSettingsObject()
{
	UMoviePipelineConfigBase* NewMoviePipeline = PipelineConfigAttribute.Get();
	if (CachedPipelineConfig != NewMoviePipeline)
	{
		CachedPipelineConfig = NewMoviePipeline;

		SettingsWidget->SetShotConfigObject(NewMoviePipeline);
		bRequestDetailsRefresh = true;
	}

	UMoviePipelineExecutorJob* NewOwningJob = OwningJobAttribute.Get();
	if (CachedOwningJob != NewOwningJob)
	{
		CachedOwningJob = NewOwningJob;
		bRequestDetailsRefresh = true;
	}
}

void SMoviePipelineConfigEditor::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	CheckForNewSettingsObject();
	if (bRequestDetailsRefresh)
	{
		UpdateDetails();
		bRequestDetailsRefresh = false;
	}
}

TSharedRef<SWidget> SMoviePipelineConfigEditor::OnGenerateSettingsMenu()
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();
	FMenuBuilder MenuBuilder(true, nullptr, Extender);

	// Put the different categories into different sections
	TArray<UClass*> SourceClasses = UE::MovieRenderPipeline::FindMoviePipelineSettingClasses();

	TMap<FString, TArray<UClass*>> CategorizedClasses;

	for (UClass* Class : SourceClasses)
	{
		const UMoviePipelineSetting* SettingDefaultObject = GetDefault<UMoviePipelineSetting>(Class);
		CategorizedClasses.FindOrAdd(SettingDefaultObject->GetCategoryText().ToString()).Add(Class);
	}

	for (TPair <FString, TArray<UClass*>> KVP : CategorizedClasses)
	{
		MenuBuilder.BeginSection(NAME_None, FText::FromString(KVP.Key));

		// Sort the classes by their CDO's GetDisplayText and not the classes GetDisplayName.
		Algo::Sort(KVP.Value, [](UClass* A, UClass* B)
			{
				const UMoviePipelineSetting* DefaultA = GetDefault<UMoviePipelineSetting>(A);
				const UMoviePipelineSetting* DefaultB = GetDefault<UMoviePipelineSetting>(B);

				return DefaultA->GetDisplayText().CompareTo(DefaultB->GetDisplayText()) < 0;
			});

		for (UClass* Class : KVP.Value)
		{
			// Get a display name for the setting from the CDO.
			const UMoviePipelineSetting* SettingDefaultObject = GetDefault<UMoviePipelineSetting>(Class);

			// Depending on the type of config we're editing, some settings may not be eligible. If this is the case, we omit them from the list.
			bool bCanSettingBeAdded = CachedPipelineConfig->CanSettingBeAdded(SettingDefaultObject);
			if (!bCanSettingBeAdded)
			{
				continue;
			}

			// If the setting already exists and it only allows a single instance, we omit them from the list.
			bool bAllowDuplicates = true;
			for (UMoviePipelineSetting* ExistingSetting : CachedPipelineConfig->GetUserSettings())
			{
				// If we found a setting with the same class as ours, ask the CDO if multiple are valid.
				if (ExistingSetting->GetClass() == Class)
				{
					bAllowDuplicates = !SettingDefaultObject->IsSolo();
					break;
				}
			}
			if (!bAllowDuplicates)
			{
				continue;
			}

			FText SettingDisplayName;
			if (SettingDefaultObject)
			{
				SettingDisplayName = SettingDefaultObject->GetDisplayText();
			}

			TSubclassOf<UMoviePipelineSetting> SubclassOf = Class;
			MenuBuilder.AddMenuEntry(
				SettingDisplayName,
				Class->GetToolTipText(true),
				FSlateIconFinder::FindIconForClass(Class),
				FUIAction(
					FExecuteAction::CreateSP(this, &SMoviePipelineConfigEditor::AddSettingFromClass, SubclassOf),
					FCanExecuteAction::CreateSP(this, &SMoviePipelineConfigEditor::CanAddSettingFromClass, SubclassOf)
				)
			);
		}

		MenuBuilder.EndSection();
	}

	return MenuBuilder.MakeWidget();
}

void SMoviePipelineConfigEditor::UpdateDetails()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs(false, false, false, FDetailsViewArgs::HideNameArea, true);
	DetailsViewArgs.bShowScrollBar = false;
	DetailsViewArgs.ColumnWidth = 0.5f;

	TArray<UMoviePipelineSetting*> SelectedSources;
	SettingsWidget->GetSelectedSettings(SelectedSources);

	
	// Create 1 details panel per source class type
	TSortedMap<const UClass*, TArray<UObject*>> ClassToSources;
	for (UMoviePipelineSetting* Source : SelectedSources)
	{
		ClassToSources.FindOrAdd(Source->GetClass()).Add(Source);
	}
	
	TArray<FObjectKey> PreviousClasses;
	ClassToDetailsView.GenerateKeyArray(PreviousClasses);

	TSharedPtr<INumericTypeInterface<double>> NumericTypeInterface = MakeShareable(new TDefaultNumericTypeInterface<double>);

	for (auto& Pair : ClassToSources)
	{
		PreviousClasses.Remove(Pair.Key);
	
		TSharedPtr<IDetailsView> ExistingDetails = ClassToDetailsView.FindRef(Pair.Key);
		if (ExistingDetails.IsValid())
		{
			ExistingDetails->SetObjects(Pair.Value);
		}
		else
		{
			TSharedRef<IDetailsView> Details = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
			Details->RegisterInstancedCustomPropertyTypeLayout("FrameNumber", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FFrameNumberDetailsCustomization::MakeInstance, NumericTypeInterface));

			Details->SetObjects(Pair.Value);
			DetailsBox->AddSlot()
				[
					Details
				];
	
			ClassToDetailsView.Add(Pair.Key, Details);
		}
	}
	
	for (FObjectKey StaleClass : PreviousClasses)
	{
		TSharedPtr<IDetailsView> Details = ClassToDetailsView.FindRef(StaleClass);
		DetailsBox->RemoveSlot(Details.ToSharedRef());
		ClassToDetailsView.Remove(StaleClass);
	}
}

void SMoviePipelineConfigEditor::AddSettingFromClass(TSubclassOf<UMoviePipelineSetting> SettingClass)
{
	UMoviePipelineConfigBase* Pipeline = PipelineConfigAttribute.Get();

	if (*SettingClass && Pipeline)
	{
		FScopedTransaction Transaction(FText::Format(LOCTEXT("AddNewSetting", "Add New {0} Setting"), SettingClass->GetDisplayNameText()));
		UMoviePipelineSetting* NewSetting = Pipeline->FindOrAddSettingByClass(SettingClass);

		SettingsWidget->SetSelectedSettings({ NewSetting });
	}
}

bool SMoviePipelineConfigEditor::CanAddSettingFromClass(TSubclassOf<UMoviePipelineSetting> SettingClass)
{
	return true;
}

EVisibility SMoviePipelineConfigEditor::IsSettingFooterVisible() const
{
	return (GetSettingsFooterText().IsEmpty()) ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SMoviePipelineConfigEditor::GetSettingsFooterText() const
{
	TArray<UMoviePipelineSetting*> SelectedSettings;
	SettingsWidget->GetSelectedSettings(SelectedSettings);

	FTextBuilder TextBuilder;

	if (CachedOwningJob.IsValid())
	{
		for (const UMoviePipelineSetting* Setting : SelectedSettings)
		{
			const FText FooterText = Setting->GetFooterText(CachedOwningJob.Get());
			if (!FooterText.IsEmpty())
			{
				TextBuilder.AppendLine(FooterText);
			}
		}
	}

	return TextBuilder.ToText();
}

EVisibility SMoviePipelineConfigEditor::IsValidationWarningVisible() const
{
	TArray<UMoviePipelineSetting*> SelectedSettings;
	SettingsWidget->GetSelectedSettings(SelectedSettings);
	
	EMoviePipelineValidationState ValidationResult = EMoviePipelineValidationState::Valid;
	for (const UMoviePipelineSetting* Setting : SelectedSettings)
	{
		// Don't show warnings for disabled settings as the invalid setting won't be used when rendering.
		if (!Setting->IsEnabled())
		{
			continue;
		}

		if ((int32)Setting->GetValidationState() > (int32)ValidationResult)
		{
			ValidationResult = Setting->GetValidationState();
		}
	}

	return (ValidationResult != EMoviePipelineValidationState::Valid) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SMoviePipelineConfigEditor::GetValidationWarningText() const
{
	TArray<UMoviePipelineSetting*> SelectedSettings;
	SettingsWidget->GetSelectedSettings(SelectedSettings);

	FTextBuilder TextBuilder;
	for (const UMoviePipelineSetting* Setting : SelectedSettings)
	{
		for (const FText& Result : Setting->GetValidationResults())
		{
			TextBuilder.AppendLine(Result);
		}
	}

	return TextBuilder.ToText();
}
#undef LOCTEXT_NAMESPACE