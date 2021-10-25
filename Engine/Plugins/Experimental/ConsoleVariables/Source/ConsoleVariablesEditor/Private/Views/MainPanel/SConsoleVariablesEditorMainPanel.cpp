// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/MainPanel/SConsoleVariablesEditorMainPanel.h"

#include "ConsoleVariablesAsset.h"
#include "ConsoleVariablesEditorModule.h"
#include "ConsoleVariablesEditorStyle.h"
#include "Views/List/ConsoleVariablesEditorList.h"
#include "Views/MainPanel/ConsoleVariablesEditorMainPanel.h"

#include "ContentBrowserModule.h"
#include "EditorFontGlyphs.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "OutputLog/Public/OutputLogModule.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConsoleVariablesEditor"

void SConsoleVariablesEditorMainPanel::Construct(const FArguments& InArgs, const TSharedRef<FConsoleVariablesEditorMainPanel>& InMainPanel)
{
	check(InMainPanel->GetEditorList().IsValid());

	MainPanel = InMainPanel;

	const FOutputLogModule& OutputLogModule = FModuleManager::LoadModuleChecked< FOutputLogModule >(TEXT("OutputLog"));

	ConsoleInput = OutputLogModule.MakeConsoleInputBox(ConsoleInputEditableTextBox, FSimpleDelegate::CreateLambda([](){}), FSimpleDelegate::CreateLambda([](){}));

	check(ConsoleInput.IsValid());
	ConsoleInput->SetVisibility(EVisibility::Collapsed);

	ConsoleInputEditableTextBox->SetOnKeyDownHandler(FOnKeyDown::CreateRaw(this, &SConsoleVariablesEditorMainPanel::HandleConsoleInputTextCommitted));

	ChildSlot
	[
		SNew(SOverlay)

		+SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			.ResizeMode(ESplitterResizeMode::FixedSize)
			
			+SSplitter::Slot().SizeRule(SSplitter::ESizeRule::SizeToContent)
			[
		        GeneratePanelToolbar()
			]

			+SSplitter::Slot()
			[
				MainPanel.Pin()->GetEditorList().Pin()->GetOrCreateWidget()
			]
		]

		+SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0.f, 0.f, 0.f, 25.f))
		[
			SNew(SBox)
			.MinDesiredWidth(200.f)
			[
				// CMD Input
				ConsoleInput.ToSharedRef()
			]
		]
	];
}

SConsoleVariablesEditorMainPanel::~SConsoleVariablesEditorMainPanel()
{

}

FReply SConsoleVariablesEditorMainPanel::OnClickAddConsoleVariableButton() const
{
	ConsoleInput->SetVisibility(EVisibility::Visible);
	FSlateApplication::Get().SetUserFocus(FSlateApplication::Get().GetUserIndexForKeyboard(), ConsoleInputEditableTextBox);
	return FReply::Handled();
}

FReply SConsoleVariablesEditorMainPanel::HandleConsoleInputTextCommitted(const FGeometry& MyGeometry, const FKeyEvent& KeyPressed)
{
	if (KeyPressed.GetKey().GetFName() == TEXT("Enter"))
	{
		FString CommandString = ConsoleInputEditableTextBox->GetText().ToString();
		FString ValueString;
		
		if (CommandString.Contains(" "))
		{
			CommandString.Split(TEXT(" "), &CommandString, &ValueString);
		}

		if (IConsoleManager::Get().IsNameRegistered(*CommandString))
		{
			MainPanel.Pin()->AddConsoleVariable(CommandString, ValueString);
		}
		else if (CommandString.IsEmpty())
		{
			UE_LOG(LogTemp, Log, TEXT("Input is blank."), *CommandString);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("Input %s is not a recognized console command."), *CommandString);
		}

		ConsoleInputEditableTextBox->SetText(FText::GetEmpty());
		ConsoleInput->SetVisibility(EVisibility::Collapsed);
	}

	return FReply::Handled();
}

void SConsoleVariablesEditorMainPanel::RefreshList(UConsoleVariablesAsset* InAsset) const
{
	if (MainPanel.Pin()->GetEditorList().IsValid())
	{
		MainPanel.Pin()->GetEditorList().Pin()->RefreshList(InAsset);
	}
}

TSharedRef<SWidget> SConsoleVariablesEditorMainPanel::GeneratePanelToolbar()
{
	struct Local
	{
		static TSharedRef<SWidget> CreatePlusText(const FText& Text)
		{
			return SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .HAlign(HAlign_Center)
                    .AutoWidth()
                    .Padding(FMargin(1.f, 1.f))
                    [
	                    SNew(STextBlock)
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
	                    .TextStyle(FEditorStyle::Get(), "NormalText.Important")
	                    .Text(FEditorFontGlyphs::Plus)
                    ]

                    + SHorizontalBox::Slot()
                    .HAlign(HAlign_Left)
                    .AutoWidth()
                    .Padding(2.f, 1.f)
                    [
                        SNew(STextBlock)
                        .Justification(ETextJustify::Center)
                        .TextStyle(FEditorStyle::Get(), "NormalText.Important")
                        .Text(Text)
                    ];
		}
	};

	return SNew(SBorder)
	        .Padding(0)
	        .BorderImage(FEditorStyle::GetBrush("NoBorder"))
			.HAlign(HAlign_Fill)
	        [
				SNew(SHorizontalBox)
				
				// Add Console Variable button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.Padding(2.f, 2.f)
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
	                .ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
	                .ForegroundColor(FSlateColor::UseForeground())
	                .OnClicked(this, &SConsoleVariablesEditorMainPanel::OnClickAddConsoleVariableButton)
	                [
						Local::CreatePlusText(LOCTEXT("AddConsoleVariable", "Add Console Variable"))
	                ]
				]

				// Presets Management Button
				+ SHorizontalBox::Slot()
				.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Fill)
				.AutoWidth()
				[
					SNew(SComboButton)
					.ToolTipText(LOCTEXT("PresetManagementButton_Tooltip", "Export the current CVar list to a preset, or import a copy of an existing preset."))
					.ContentPadding(4.f)
					.ComboButtonStyle(FConsoleVariablesEditorStyle::Get(), "ComboButton")
					.OnGetMenuContent(this, &SConsoleVariablesEditorMainPanel::OnGeneratePresetsMenu)
					.ForegroundColor(FSlateColor::UseForeground())
					.ButtonContent()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.Padding(0, 1, 4, 0)
						.AutoWidth()
						[
							SNew(SImage)
							.Image(FEditorStyle::Get().GetBrush("AssetEditor.SaveAsset"))
						]

						+ SHorizontalBox::Slot()
						.Padding(0, 1, 0, 0)
						[
							SNew(STextBlock)
							.Text_Lambda([this] ()
							{
								return MainPanel.Pin()->GetReferenceAssetOnDisk().IsValid() ? 
									FText::Format(
										LoadedPresetFormatText, 
										FText::FromName(MainPanel.Pin()->GetReferenceAssetOnDisk()->GetFName())) : NoLoadedPresetText;
							})
						]
					]
				]

				// Open Settings
				+ SHorizontalBox::Slot()
                .HAlign(HAlign_Right)
				.VAlign(VAlign_Fill)
                [
					SNew(SBox)
					.WidthOverride(28)
					.HeightOverride(28)
					[
						SAssignNew(SettingsButtonPtr, SCheckBox)
						.Padding(FMargin(4.f))
						.ToolTipText(LOCTEXT("ShowSettings_Tip", "Show the general user/project settings for Console Variables"))
						.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
						.ForegroundColor(FSlateColor::UseForeground())
						.IsChecked(false)
						.OnCheckStateChanged_Lambda([this](ECheckBoxState CheckState)
						{
							FConsoleVariablesEditorModule::OpenConsoleVariablesSettings();
							SettingsButtonPtr->SetIsChecked(false);
						})
		                [
							SNew(STextBlock)
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.14"))
							.Text(FEditorFontGlyphs::Cogs)
		                ]
					]
                ]
        	];
}

TSharedRef<SWidget> SConsoleVariablesEditorMainPanel::OnGeneratePresetsMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("SavePreset_Text", "Save Preset"),
		LOCTEXT("SavePreset_Tooltip", "Save the current preset if one has been loaded. Otherwise, the Save As dialog will be opened."),
		FSlateIcon(FEditorStyle::Get().GetStyleSetName(), "AssetEditor.SaveAsset"),
		FUIAction(FExecuteAction::CreateRaw(MainPanel.Pin().Get(), &FConsoleVariablesEditorMainPanel::SavePreset))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("SavePresetAs_Text", "Save Preset As"),
		LOCTEXT("SavePresetAs_Tooltip", "Save the current configuration as a new preset that can be shared between multiple jobs, or imported later as the base of a new configuration."),
		FSlateIcon(FEditorStyle::Get().GetStyleSetName(), "AssetEditor.SaveAssetAs"),
		FUIAction(FExecuteAction::CreateRaw(MainPanel.Pin().Get(), &FConsoleVariablesEditorMainPanel::SavePresetAs))
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
		AssetPickerConfig.bForceShowEngineContent = false;
		AssetPickerConfig.bForceShowPluginContent = false;

		AssetPickerConfig.AssetShowWarningText = LOCTEXT("NoPresets_Warning", "No Presets Found");
		AssetPickerConfig.Filter.ClassNames.Add(UConsoleVariablesAsset::StaticClass()->GetFName());
		AssetPickerConfig.Filter.bRecursiveClasses = true;
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateRaw(MainPanel.Pin().Get(), &FConsoleVariablesEditorMainPanel::ImportPreset);
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

#undef LOCTEXT_NAMESPACE
