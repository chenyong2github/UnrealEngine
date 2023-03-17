// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleEditorAssetPicker.h"

#include "DMXControlConsolePreset.h"
#include "Models/DMXControlConsoleEditorPresetModel.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Commands/DMXControlConsoleEditorCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleEditorAssetPicker"

void SDMXControlConsoleEditorAssetPicker::Construct(const FArguments& InArgs)
{
	RegisterCommands();

	ChildSlot
		[
			SNew(SBox)
			.MinDesiredWidth(150.f)
			.Padding(2.0f)
			[
				SAssignNew(AssetComboButton, SComboButton)
				.OnGetMenuContent(this, &SDMXControlConsoleEditorAssetPicker::CreateMenu)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(this, &SDMXControlConsoleEditorAssetPicker::GetEditorPresetName)
				]
			]
		];
}

void SDMXControlConsoleEditorAssetPicker::RegisterCommands()
{
	CommandList = MakeShared<FUICommandList>();

	UDMXControlConsoleEditorPresetModel* PresetModel = GetMutableDefault<UDMXControlConsoleEditorPresetModel>();
	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().SavePreset,
		FExecuteAction::CreateUObject(PresetModel, &UDMXControlConsoleEditorPresetModel::SavePreset)
	);

	CommandList->MapAction
	(
		FDMXControlConsoleEditorCommands::Get().SavePresetAs,
		FExecuteAction::CreateUObject(PresetModel, &UDMXControlConsoleEditorPresetModel::SavePresetAs)
	);
}

TSharedRef<SWidget> SDMXControlConsoleEditorAssetPicker::CreateMenu()
{
	ensureMsgf(CommandList.IsValid(), TEXT("Invalid command list for control console asset picker."));

	FMenuBuilder MenuBuilder(true, CommandList);

	// Display the loaded preset name
	{
		const TSharedRef<SWidget> LoadedPresetNameWidget =
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("NoBorder"))
			.Padding(20.f, 2.f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
					{
						return FText::Format(LOCTEXT("CurrentPresetLabel", "Current Preset: {0}"), GetEditorPresetName());
					})
			];

		MenuBuilder.AddWidget(LoadedPresetNameWidget, FText::GetEmpty());
	}

	// Separator
	{
		MenuBuilder.AddMenuSeparator();
	}

	// Save Preset button
	{
		MenuBuilder.AddMenuEntry(FDMXControlConsoleEditorCommands::Get().SavePreset,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "AssetEditor.SaveAsset")
		);
	}

	// Save As Preset button
	{
		MenuBuilder.AddMenuEntry(FDMXControlConsoleEditorCommands::Get().SavePresetAs,
			NAME_None,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "AssetEditor.SaveAssetAs")
		);
	}

	// Asset picker
	MenuBuilder.BeginSection(NAME_None, LOCTEXT("LoadPresetMenuSection", "Load Preset"));
	{
		IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

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
			AssetPickerConfig.bForceShowEngineContent = false;
			AssetPickerConfig.bForceShowPluginContent = false;
			AssetPickerConfig.HiddenColumnNames.Add(TEXT("ItemDiskSize"));
			AssetPickerConfig.HiddenColumnNames.Add(TEXT("HasVirtualizedData"));

			AssetPickerConfig.AssetShowWarningText = LOCTEXT("NoPresetsFoundMessage", "No Presets Found");
			AssetPickerConfig.Filter.ClassPaths.Add(UDMXControlConsolePreset::StaticClass()->GetClassPathName());
			AssetPickerConfig.Filter.bRecursiveClasses = false;
			AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SDMXControlConsoleEditorAssetPicker::OnAssetSelected);
			AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateSP(this, &SDMXControlConsoleEditorAssetPicker::OnAssetEnterPressed);
		}

		const TSharedRef<SWidget> PresetPicker = SNew(SBox)
			.MinDesiredWidth(650.f)
			.MinDesiredHeight(400.f)
			[
				ContentBrowser.CreateAssetPicker(AssetPickerConfig)
			];

		constexpr bool bNoIndent = true;
		constexpr bool bShowsSearch = false;
		MenuBuilder.AddWidget(PresetPicker, FText(), bNoIndent, bShowsSearch);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

FText SDMXControlConsoleEditorAssetPicker::GetEditorPresetName() const
{
	const UDMXControlConsoleEditorPresetModel* PresetModel = GetDefault<UDMXControlConsoleEditorPresetModel>();
	const UDMXControlConsolePreset* EditorPreset = PresetModel->GetEditorPreset();
	if (ensureMsgf(EditorPreset, TEXT("No Preset present in Control Console Editor, cannot display preset name.")))
	{
		if (EditorPreset->IsAsset())
		{
			const bool bDirty = EditorPreset->GetOutermost()->IsDirty();
			return bDirty ?
				FText::FromString(EditorPreset->GetName() + TEXT(" *")) :
				FText::FromString(EditorPreset->GetName());
		}
		else
		{
			return LOCTEXT("UnsavedPresetName", "Unsaved Preset *");
		}
	}
	return FText::GetEmpty();
}

void SDMXControlConsoleEditorAssetPicker::OnAssetSelected(const FAssetData& AssetData)
{
	UDMXControlConsoleEditorPresetModel* PresetModel = GetMutableDefault<UDMXControlConsoleEditorPresetModel>();
	PresetModel->LoadPreset(AssetData);

	AssetComboButton->SetIsOpen(false);
}

void SDMXControlConsoleEditorAssetPicker::OnAssetEnterPressed(const TArray<FAssetData>& SelectedAssets)
{
	if (SelectedAssets.IsEmpty())
	{
		return;
	}

	UDMXControlConsoleEditorPresetModel* PresetModel = GetMutableDefault<UDMXControlConsoleEditorPresetModel>();
	PresetModel->LoadPreset(SelectedAssets[0]);
}

#undef LOCTEXT_NAMESPACE
