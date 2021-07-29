// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSaveAndLoadFilters.h"

#include "LevelSnapshotsFilterPreset.h"
#include "FilterLoader.h"

#include "ContentBrowserModule.h"
#include "Engine/AssetManager.h"
#include "IContentBrowserSingleton.h"
#include "LevelSnapshotsEditorData.h"
#include "Misc/MessageDialog.h"
#include "SAssetSearchBox.h"
#include "SAssetDropTarget.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboBox.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

namespace
{
	constexpr const auto ComboButtonStyle = TEXT("RoundButton");
	constexpr const auto FontStyle = TEXT("GenericFilters.TextStyle");

	void SaveAs(TWeakObjectPtr<UFilterLoader> FilterLoader)
	{
		FilterLoader->SaveAs();
	}

	void SaveExisting(TWeakObjectPtr<UFilterLoader> FilterLoader)
	{
		const TOptional<FAssetData> CurrentlySelected = FilterLoader->GetAssetLastSavedOrLoaded();
		if (ensure(CurrentlySelected.IsSet()))
		{
			FFormatOrderedArguments Arguments;
			Arguments.Add(FText::FromString(CurrentlySelected->AssetName.ToString()));
			const FText Text = FText::Format(LOCTEXT("ConfirmOverwriteText", "This replaces the filters in asset {0} with ones set up in the editor. Are you sure?"), Arguments);
			const FText Title = LOCTEXT("ConfirmOverwriteTitle", "Confirm save");
			
			const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::OkCancel, Text, &Title);
			if (Answer == EAppReturnType::Cancel)
			{
				return;
			}
		}
		
		FilterLoader->OverwriteExisting();
	}

	void OnSelectPreset(const FAssetData& SelectedAssetData, TWeakObjectPtr<ULevelSnapshotsEditorData> EditorData)
	{
		if (!ensure(EditorData.IsValid()))
		{
			return;
		}
		
		UObject* Loaded = SelectedAssetData.GetAsset();
		ULevelSnapshotsFilterPreset* AsFilter = Cast<ULevelSnapshotsFilterPreset>(Loaded);
		if (!ensure(Loaded) || !ensure(AsFilter))
		{
			return;
		}

		TWeakObjectPtr<UFilterLoader> FilterLoader = EditorData->GetFilterLoader();
		const TOptional<FAssetData> PreviousSelection = FilterLoader->GetAssetLastSavedOrLoaded();

		const FText Title = LOCTEXT("LoseChangesDialogTitle", "Lose changes");
		const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::OkCancel, LOCTEXT("LoseChangesDialogText", "Are you sure you want to load another preset? Any changes you made will be lost."), &Title);
		if (Answer == EAppReturnType::Cancel)
		{
			return;
		}

		FilterLoader->LoadAsset(AsFilter);
	}
}

void SSaveAndLoadFilters::Construct(const FArguments& InArgs, ULevelSnapshotsEditorData* InEditorData)
{
	if (!ensure(InEditorData))
	{
		return;
	}

	EditorData = InEditorData;
	FilterLoader = InEditorData->GetFilterLoader();

	ChildSlot
    [
        SNew(SComboButton)
          .ToolTipText(LOCTEXT("SaveLoad_Tooltip", "Export the current filter to an asset, or load a previously saved filter."))
          .ContentPadding(4.f)
          .ComboButtonStyle(FEditorStyle::Get(), "GenericFilters.ComboButtonStyle")
          .OnGetMenuContent(this, &SSaveAndLoadFilters::GenerateSaveLoadMenu)
          .ForegroundColor(FSlateColor::UseForeground())
          .ButtonContent()
          [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .Padding(0, 1, 4, 0)
            .AutoWidth()
            [
              SNew(SImage)
              .Image(FSlateIconFinder::FindIconBrushForClass(ULevelSnapshotsFilterPreset::StaticClass()))
            ]

            + SHorizontalBox::Slot()
            .Padding(0, 1, 0, 0)
            [
              SNew(STextBlock)
              .Text(LOCTEXT("SavedToolbarButton", "Load/Save Filter"))
            ]
          ]
    ];
}

TSharedRef<SWidget> SSaveAndLoadFilters::GenerateSaveLoadMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	const TOptional<FAssetData> SelectedFilter = FilterLoader->GetAssetLastSavedOrLoaded();
	if (SelectedFilter.IsSet())
	{
		FFormatOrderedArguments Arguements;
		Arguements.Add(FText::FromString(SelectedFilter->AssetName.ToString()));
		const FText EntryName = FText::Format(LOCTEXT("LoadFilters.LoadedName", "Save \"{0}\""), Arguements);
						
		MenuBuilder.AddMenuEntry(
            EntryName,
            LOCTEXT("SaveExistingFiltersToolTip", "Overwrite the asset you last loaded"),
            FSlateIcon(FEditorStyle::Get().GetStyleSetName(), "AssetEditor.SaveAsset.Greyscale"),
            FUIAction(
                FExecuteAction::CreateStatic(&SaveExisting, FilterLoader)
            )
            );
	}
	
	MenuBuilder.AddMenuEntry(
	    LOCTEXT("SaveFiltersAs", "Save as..."),
	    LOCTEXT("SaveFiltersAsToolTip", "Saves a new asset."),
	    FSlateIcon(FEditorStyle::Get().GetStyleSetName(), "AssetEditor.SaveAsset.Greyscale"),
	    FUIAction(
	        FExecuteAction::CreateStatic(&SaveAs, FilterLoader)
		)	
    );

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("LoadFilter_MenuSection", "Load filter"));
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

		FAssetPickerConfig AssetPickerConfig;
		AssetPickerConfig.Filter.ClassNames.Add(ULevelSnapshotsFilterPreset::StaticClass()->GetFName());
		AssetPickerConfig.bAllowNullSelection = false;
		AssetPickerConfig.Filter.bRecursiveClasses = true;
		AssetPickerConfig.OnAssetSelected.BindStatic(OnSelectPreset, EditorData);
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
		AssetPickerConfig.bAllowDragging = false;
		
        TSharedRef<SWidget> PresetPicker = SNew(SBox)
            .HeightOverride(300)
            .WidthOverride(300)
            [
                SNew(SBorder)
                .BorderImage(FEditorStyle::GetBrush("Menu.Background"))
                [
                    ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
                ]
            ];
		MenuBuilder.AddWidget(PresetPicker, FText(), true, false);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}


#undef LOCTEXT_NAMESPACE
