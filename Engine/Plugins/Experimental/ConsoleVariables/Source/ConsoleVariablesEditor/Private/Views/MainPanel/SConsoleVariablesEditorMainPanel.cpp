// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/MainPanel/SConsoleVariablesEditorMainPanel.h"

#include "ConsoleVariablesAsset.h"
#include "ConsoleVariablesEditorLog.h"
#include "ConsoleVariablesEditorModule.h"
#include "ConsoleVariablesEditorStyle.h"
#include "Views/List/ConsoleVariablesEditorList.h"
#include "Views/MainPanel/ConsoleVariablesEditorMainPanel.h"

#include "ContentBrowserModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "OutputLog/Public/OutputLogModule.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
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

	ConsoleInput = OutputLogModule.MakeConsoleInputBox(
		ConsoleInputEditableTextBox, FSimpleDelegate::CreateLambda([](){}), FSimpleDelegate::CreateLambda([](){}));

	check(ConsoleInput.IsValid());

	ConsoleInputEditableTextBox->SetOnKeyDownHandler(FOnKeyDown::CreateRaw(this, &SConsoleVariablesEditorMainPanel::HandleConsoleInputTextCommitted));

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Vertical)
		.ResizeMode(ESplitterResizeMode::FixedSize)
		
		+SSplitter::Slot().SizeRule(SSplitter::ESizeRule::SizeToContent)
		[
	        GeneratePanelToolbar(ConsoleInput.ToSharedRef())
		]

		+SSplitter::Slot()
		[
			MainPanel.Pin()->GetEditorList().Pin()->GetOrCreateWidget()
		]
	];
}

SConsoleVariablesEditorMainPanel::~SConsoleVariablesEditorMainPanel()
{

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

		if (IConsoleVariable* AsVariable = IConsoleManager::Get().FindConsoleVariable(*CommandString))
		{
			MainPanel.Pin()->AddConsoleVariable(CommandString, ValueString.IsEmpty() ? AsVariable->GetString() : ValueString, true);
		}
		else if (CommandString.IsEmpty())
		{
			UE_LOG(LogConsoleVariablesEditor, Warning, TEXT("hs: Input is blank."), __FUNCTION__);
		}
		else
		{
			UE_LOG(LogConsoleVariablesEditor, Warning, TEXT("%hs: Input %s is not a recognized console command."), __FUNCTION__, *CommandString);
		}

		ConsoleInputEditableTextBox->SetText(FText::GetEmpty());
	}

	return FReply::Handled();
}

TSharedRef<SWidget> SConsoleVariablesEditorMainPanel::GeneratePanelToolbar(const TSharedRef<SWidget> InConsoleInputWidget)
{
	return SNew(SBorder)
	        .Padding(0)
	        .BorderImage(FAppStyle::Get().GetBrush("NoBorder"))
			.HAlign(HAlign_Fill)
	        [
				SNew(SHorizontalBox)
				
				// Add Console Variable input
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.Padding(2.f, 2.f)
				[
					InConsoleInputWidget
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
					.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("ComboButton"))
					.OnGetMenuContent(this, &SConsoleVariablesEditorMainPanel::OnGeneratePresetsMenu)
					.ForegroundColor(FStyleColors::Foreground)
					.ButtonContent()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.Padding(0, 1, 4, 0)
						.AutoWidth()
						[
							SNew(SImage)
							.Image(FAppStyle::Get().GetBrush("AssetEditor.SaveAsset"))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]

						+ SHorizontalBox::Slot()
						.Padding(0, 1, 0, 0)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("PresetsToolbarButton", "Presets"))
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
					.Visibility(EVisibility::Collapsed)
					[
						SAssignNew(SettingsButtonPtr, SCheckBox)
						.Padding(FMargin(4.f))
						.ToolTipText(LOCTEXT("ShowSettings_Tip", "Show the general user/project settings for Console Variables"))
						.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox"))
						.ForegroundColor(FStyleColors::Foreground)
						.IsChecked(false)
						.OnCheckStateChanged_Lambda([this](ECheckBoxState CheckState)
						{
							FConsoleVariablesEditorModule::OpenConsoleVariablesSettings();
							SettingsButtonPtr->SetIsChecked(false);
						})
		                [
							SNew(SImage)
							.Image(FAppStyle::Get().GetBrush("Icons.Settings"))
							.ColorAndOpacity(FSlateColor::UseForeground())
		                ]
					]
                ]
        	];
}

TSharedRef<SWidget> SConsoleVariablesEditorMainPanel::OnGeneratePresetsMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

	const FText LoadedPresetName = MainPanel.Pin()->GetReferenceAssetOnDisk().IsValid() ?
		FText::Format(LoadedPresetFormatText, FText::FromString(MainPanel.Pin()->GetReferenceAssetOnDisk()->GetName())) : NoLoadedPresetText;

	MenuBuilder.AddMenuEntry(
		LoadedPresetName,
		LoadedPresetName,
		FSlateIcon(),
		FUIAction()
	);

	MenuBuilder.AddMenuSeparator();
	
	MenuBuilder.AddMenuEntry(
		LOCTEXT("SavePreset_Text", "Save Preset"),
		LOCTEXT("SavePreset_Tooltip", "Save the current preset if one has been loaded. Otherwise, the Save As dialog will be opened."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "AssetEditor.SaveAsset"),
		FUIAction(FExecuteAction::CreateRaw(MainPanel.Pin().Get(), &FConsoleVariablesEditorMainPanel::SavePreset))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("SavePresetAs_Text", "Save Preset As"),
		LOCTEXT("SavePresetAs_Tooltip", "Save the current configuration as a new preset that can be shared between multiple jobs, or imported later as the base of a new configuration."),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), "AssetEditor.SaveAssetAs"),
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
