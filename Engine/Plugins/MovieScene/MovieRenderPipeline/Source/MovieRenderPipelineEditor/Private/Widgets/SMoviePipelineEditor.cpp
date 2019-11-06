// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMoviePipelineEditor.h"
#include "Widgets/MoviePipelineWidgetConstants.h"

#include "MoviePipelineShotConfig.h"
#include "MoviePipelineSetting.h"

// Core includes
#include "UObject/UObjectIterator.h"
#include "Templates/SubclassOf.h"
#include "ClassIconFinder.h"
// 
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
#include "SMoviePipelineSettings.h"

#define LOCTEXT_NAMESPACE "SMoviePipelineEditor"


TArray<UClass*> FindMoviePipelineSettingClasses()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	TArray<FAssetData> ClassList;

	FARFilter Filter;
	Filter.ClassNames.Add(UMoviePipelineSetting::StaticClass()->GetFName());

	// Include any Blueprint based objects as well, this includes things like Blutilities, UMG, and GameplayAbility objects
	Filter.bRecursiveClasses = true;
	AssetRegistryModule.Get().GetAssets(Filter, ClassList);

	TArray<UClass*> Classes;

	for (const FAssetData& Data : ClassList)
	{
		UClass* Class = Data.GetClass();
		if (Class)
		{
			Classes.Add(Class);
		}
	}

	for (TObjectIterator<UClass> ClassIterator; ClassIterator; ++ClassIterator)
	{
		if (ClassIterator->IsChildOf(UMoviePipelineSetting::StaticClass()) && !ClassIterator->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			Classes.Add(*ClassIterator);
		}
	}

	return Classes;
}

PRAGMA_DISABLE_OPTIMIZATION
void SMoviePipelineEditor::Construct(const FArguments& InArgs)
{
	bRequestDetailsRefresh = true;
	MoviePipelineAttribute = InArgs._MoviePipeline;
    
	DetailsBox = SNew(SScrollBox);
	DetailsBox->SetScrollBarRightClickDragAllowed(true);
     
	SettingsWidget = SNew(SMoviePipelineSettings)
	.OnSelectionChanged(this, &SMoviePipelineEditor::OnSettingsSelectionChanged);
    
	CheckForNewMoviePipeline();
     
	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
    
		+ SSplitter::Slot()
		.Value(.25f)
		[
			SNew(SBorder)
			.Padding(4)
			.BorderImage( FEditorStyle::GetBrush("ToolPanel.GroupBorder") )
			[
				SettingsWidget.ToSharedRef()
			]
		]
    
		+ SSplitter::Slot()
		.Value(.75f)
		[
			DetailsBox.ToSharedRef()
		]
	];
}

TSharedRef<SWidget> SMoviePipelineEditor::MakeAddSettingButton()
{
	return SNew(SComboButton)
		.ContentPadding(MoviePipeline::ButtonPadding)
		// .ButtonStyle(FTakeRecorderStyle::Get(), "FlatButton.Success")
		.OnGetMenuContent(this, &SMoviePipelineEditor::OnGenerateSettingsMenu)
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

void SMoviePipelineEditor::CheckForNewMoviePipeline()
{
	UMoviePipelineConfigBase* NewMoviePipeline = MoviePipelineAttribute.Get();
	if (CachedMoviePipeline != NewMoviePipeline)
	{
		CachedMoviePipeline = NewMoviePipeline;

		SettingsWidget->SetShotConfigObject(NewMoviePipeline);
		bRequestDetailsRefresh = true;
	}
}

void SMoviePipelineEditor::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	CheckForNewMoviePipeline();
	if (bRequestDetailsRefresh)
	{
		UpdateDetails();
		bRequestDetailsRefresh = false;
	}
}

TSharedRef<SWidget> SMoviePipelineEditor::OnGenerateSettingsMenu()
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();
	{
		// ULevelSequence*       LevelSequence = LevelSequenceAttribute.Get();
		// UTakeRecorderSources* Sources = LevelSequence ? LevelSequence->FindOrAddMetaData<UTakeRecorderSources>() : nullptr;
		// 
		// if (Sources)
		// {
		// 	FTakeRecorderModule& TakeRecorderModule = FModuleManager::GetModuleChecked<FTakeRecorderModule>("TakeRecorder");
		// 	TakeRecorderModule.PopulateSourcesMenu(Extender, Sources);
		// }
	}

	FMenuBuilder MenuBuilder(true, nullptr, Extender);

	MenuBuilder.BeginSection("Settings", LOCTEXT("SettingsMenuSection", "Available Settings"));
	{
		TArray<UClass*> SourceClasses = FindMoviePipelineSettingClasses();
		Algo::SortBy(SourceClasses, &UClass::GetDisplayNameText, FText::FSortPredicate());

		for (UClass* Class : SourceClasses)
		{
			TSubclassOf<UMoviePipelineSetting> SubclassOf = Class;

			MenuBuilder.AddMenuEntry(
				FText::FromString(Class->GetMetaData(TEXT("MovieRenderPipelineDisplayName"))),
				Class->GetToolTipText(true),
				FSlateIconFinder::FindIconForClass(Class),
				FUIAction(
					FExecuteAction::CreateSP(this, &SMoviePipelineEditor::AddSettingFromClass, SubclassOf),
					FCanExecuteAction::CreateSP(this, &SMoviePipelineEditor::CanAddSettingFromClass, SubclassOf)
				)
			);
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SMoviePipelineEditor::UpdateDetails()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs(false, false, false, FDetailsViewArgs::HideNameArea, true);
	DetailsViewArgs.bShowScrollBar = false;
	 
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

void SMoviePipelineEditor::AddSettingFromClass(TSubclassOf<UMoviePipelineSetting> SettingClass)
{
	UMoviePipelineConfigBase* Pipeline = MoviePipelineAttribute.Get();

	if (*SettingClass && Pipeline)
	{
		FScopedTransaction Transaction(FText::Format(LOCTEXT("AddNewSetting", "Add New {0} Setting"), SettingClass->GetDisplayNameText()));
		Pipeline->FindOrAddSettingByClass(SettingClass);
	}
}

bool SMoviePipelineEditor::CanAddSettingFromClass(TSubclassOf<UMoviePipelineSetting> SettingClass)
{
	return true;
}


#undef LOCTEXT_NAMESPACE