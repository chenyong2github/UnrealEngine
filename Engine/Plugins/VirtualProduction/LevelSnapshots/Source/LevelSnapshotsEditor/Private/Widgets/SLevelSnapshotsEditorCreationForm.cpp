// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLevelSnapshotsEditorCreationForm.h"

#include "Settings/LevelSnapshotsEditorProjectSettings.h"
#include "Settings/LevelSnapshotsEditorDataManagementSettings.h"
#include "LevelSnapshotsEditorData.h"
#include "LevelSnapshotsEditorStyle.h"

#include "Editor.h"
#include "EditorStyleSet.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "ISettingsSection.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWindow> SLevelSnapshotsEditorCreationForm::MakeAndShowCreationWindow(
	const FCloseCreationFormDelegate& CallOnClose, 
	ULevelSnapshotsEditorProjectSettings* InProjectSettings, ULevelSnapshotsEditorDataManagementSettings* InDataManagementSettings)
{
	check(InProjectSettings);
	check(InDataManagementSettings);
	
	// Compute centered window position based on max window size, which include when all categories are expanded
	const FVector2D LastSize = InProjectSettings->GetLastCreationWindowSize();
	const FVector2D BaseWindowSize = FVector2D(LastSize.X, LastSize.Y); // Max window size it can get based on current slate


	const FSlateRect WorkAreaRect = FSlateApplicationBase::Get().GetPreferredWorkArea();
	const FVector2D DisplayTopLeft(WorkAreaRect.Left, WorkAreaRect.Top);
	const FVector2D DisplaySize(WorkAreaRect.Right - WorkAreaRect.Left, WorkAreaRect.Bottom - WorkAreaRect.Top);

	const FVector2D WindowPosition = (DisplayTopLeft + (DisplaySize - BaseWindowSize) / 2.0f);

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(NSLOCTEXT("LevelSnapshots", "LevelSnapshots_CreationForm_Title", "Create Level Snapshot"))
		.SizingRule(ESizingRule::UserSized)
		.AutoCenter(EAutoCenter::PrimaryWorkArea)
		.ClientSize(BaseWindowSize)
		.AdjustInitialSizeAndPositionForDPIScale(false)
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.ScreenPosition(WindowPosition);

	const TSharedRef<SLevelSnapshotsEditorCreationForm> CreationForm = SNew(SLevelSnapshotsEditorCreationForm, Window, CallOnClose, InProjectSettings, InDataManagementSettings);
	Window->SetContent
	(
		CreationForm
	);
	Window->SetOnWindowClosed(FOnWindowClosed::CreateSP(CreationForm, &SLevelSnapshotsEditorCreationForm::OnWindowClosed));

	FSlateApplication::Get().AddWindow(Window);
	return Window;
}

void SLevelSnapshotsEditorCreationForm::Construct(
	const FArguments& InArgs,
	TWeakPtr<SWindow> InWidgetWindow,
	const FCloseCreationFormDelegate& CallOnClose, 
	ULevelSnapshotsEditorProjectSettings* InProjectSettings,
	ULevelSnapshotsEditorDataManagementSettings* InDataManagementSettings)
{
	check(InProjectSettings);
	check(InDataManagementSettings);
	
	ProjectSettingsObjectPtr = InProjectSettings;
	DataManagementSettingsObjectPtr = InDataManagementSettings;
	
	WidgetWindow = InWidgetWindow;
	CallOnCloseDelegate = CallOnClose;

	bNameDiffersFromDefault = DataManagementSettingsObjectPtr.Get()->IsNameOverridden();
	
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(new FSlateColorBrush(FColor(10, 10, 10)))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.f, 2.f, 2.f, 0.f)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				[
					SNew(SVerticalBox)

					+SVerticalBox::Slot()
					.AutoHeight()
					.Padding(1.f, 1.f, 0.f, 0.f)
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Top)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FColor(200, 200, 200))
						.Text(NSLOCTEXT("LevelSnapshots", "CreationForm_SnapshotNameLabel", "Name"))
					]

					+SVerticalBox::Slot()
					.AutoHeight()
					.Padding(8.f, 1.f, 8.f, 10.f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Fill)
						[
							SNew(SEditableTextBox)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
							.BackgroundColor(FLinearColor::Transparent)
							.ForegroundColor(FSlateColor::UseForeground())
							.Justification(ETextJustify::Center)
							.SelectAllTextWhenFocused(true)
							.HintText(NSLOCTEXT("LevelSnapshots", "CreationForm_SnapshotNameOverrideHintText", "Override Snapshot Name..."))
							.Text(this, &SLevelSnapshotsEditorCreationForm::GetNameOverrideText)
							.OnTextCommitted(this, &SLevelSnapshotsEditorCreationForm::SetNameOverrideText)
							.ToolTipText(
							NSLOCTEXT("LevelSnapshots", "CreationForm_NameOverrideFieldTooltipText", "Override the name defined in Project Settings while using the Creation Form."))
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						[
							SNew(SButton)
							.IsFocusable(false)
							.ToolTipText(
								NSLOCTEXT("LevelSnapshots", "CreationForm_ResetNameTooltipText", "Reset the overridden name to the one defined in Project Settings."))
							.ButtonStyle(FEditorStyle::Get(), "NoBorder")
							.ContentPadding(0)
							.Visibility(this, &SLevelSnapshotsEditorCreationForm::GetNameDiffersFromDefaultAsVisibility)
							.OnClicked(this, &SLevelSnapshotsEditorCreationForm::OnResetNameClicked)
							.Content()
							[
								SNew(SImage)
								.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
							]
						]
					]
				]
			]

			+SVerticalBox::Slot()
			.Padding(2.f, 10.f, 2.f, 0.f)
			.VAlign(VAlign_Fill)
			[
				SNew(SMultiLineEditableTextBox)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.BackgroundColor(FLinearColor(0.2f, 0.2f, 0.2f))
				.ForegroundColor(FSlateColor::UseForeground())
				.SelectAllTextWhenFocused(true)
				.HintText(NSLOCTEXT("LevelSnapshots", "CreationForm_DescriptionHintText", "Description"))
				.Text(DescriptionText)
				.OnTextCommitted(this, &SLevelSnapshotsEditorCreationForm::SetDescriptionText)
				.AllowMultiLine(true)
				.AutoWrapText(true)
			]
			
			+SVerticalBox::Slot()
			.Padding(2.f, 10.f, 2.f, 0.f)
			.AutoHeight()
			.VAlign(VAlign_Bottom)
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), "NormalText.Important")
				.Text(NSLOCTEXT("LevelSnapshots", "CreationForm_SaveDirLabel", "Save Directory"))
			]

			+SVerticalBox::Slot()
			.Padding(2.f, 2.f, 2.f, 0.f)
			.AutoHeight()
			.VAlign(VAlign_Bottom)
			[
				MakeDataManagementSettingsDetailsWidget()
			]
			
			+SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Bottom)
			.HAlign(HAlign_Fill)
			.Padding(2.f, 5.f)
			[
				SNew(SHorizontalBox)

				// Save Async checkbox
				+SHorizontalBox::Slot()
				.Padding(3.f, 0.f)
				.HAlign(HAlign_Left)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bSaveAsync ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bSaveAsync = NewState == ECheckBoxState::Checked; })
					.ToolTipText(NSLOCTEXT("LevelSnapshots", "CreationForm_SaveAsync_Tooltip", "Enabling may speed up saving for large levels."))
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						.Text(NSLOCTEXT("LevelSnapshots", "CreationForm_SaveAsync", "Save async"))
					]
				]
				
				// Create snapshot button
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
					.ForegroundColor(FSlateColor::UseForeground())
					.OnClicked(this, &SLevelSnapshotsEditorCreationForm::OnCreateButtonPressed)
					[
						SNew(STextBlock)
						.Justification(ETextJustify::Center)
						.TextStyle(FEditorStyle::Get(), "NormalText.Important")
						.Text(NSLOCTEXT("LevelSnapshots", "NotificationFormatText_CreationForm_CreateSnapshotButton", "Create Level Snapshot"))
					]
				]
			]
		]
	];
}

TSharedRef<SWidget> SLevelSnapshotsEditorCreationForm::MakeDataManagementSettingsDetailsWidget() const
{
	check(ProjectSettingsObjectPtr.IsValid());
	
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.bShowScrollBar = false;

	TSharedRef<IDetailsView> Details = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	const TArray<UObject*> ProjectSettingsObjects = {DataManagementSettingsObjectPtr.Get()};
	Details->SetObjects(ProjectSettingsObjects);
	Details->SetEnabled(true);

	return Details;
}

SLevelSnapshotsEditorCreationForm::~SLevelSnapshotsEditorCreationForm()
{
	CallOnCloseDelegate.Unbind();
}

FText SLevelSnapshotsEditorCreationForm::GetNameOverrideText() const
{
	check(DataManagementSettingsObjectPtr.IsValid());

	UWorld* World = ULevelSnapshotsEditorData::GetEditorWorld();
	if (!ensure(World && DataManagementSettingsObjectPtr.IsValid()))
	{
		return FText::FromString(DataManagementSettingsObjectPtr.Get()->GetNameOverride());
	}

	return 	ULevelSnapshotsEditorDataManagementSettings::ParseLevelSnapshotsTokensInText(FText::FromString(
		DataManagementSettingsObjectPtr.Get()->GetNameOverride()), World->GetName());
}

void SLevelSnapshotsEditorCreationForm::SetNameOverrideText(const FText& InNewText, ETextCommit::Type InCommitType)
{
	check(DataManagementSettingsObjectPtr.IsValid());

	FString NameAsString = InNewText.ToString();
	ULevelSnapshotsEditorDataManagementSettings::SanitizePathInline(NameAsString, true);

	DataManagementSettingsObjectPtr->SetNameOverride(NameAsString);

	bNameDiffersFromDefault = DataManagementSettingsObjectPtr.Get()->IsNameOverridden();
}

void SLevelSnapshotsEditorCreationForm::SetDescriptionText(const FText& InNewText, ETextCommit::Type InCommitType)
{
	DescriptionText = InNewText;
}

EVisibility SLevelSnapshotsEditorCreationForm::GetNameDiffersFromDefaultAsVisibility() const
{
	return bNameDiffersFromDefault ? EVisibility::Visible : EVisibility::Hidden;
}

FReply SLevelSnapshotsEditorCreationForm::OnResetNameClicked()
{
	check(DataManagementSettingsObjectPtr.IsValid());

	SetNameOverrideText(FText::FromString(DataManagementSettingsObjectPtr.Get()->DefaultLevelSnapshotName), ETextCommit::OnEnter);

	return FReply::Handled();
}

FReply SLevelSnapshotsEditorCreationForm::OnCreateButtonPressed()
{
	bWasCreateSnapshotPressed = true;

	check(WidgetWindow.IsValid());
	WidgetWindow.Pin()->RequestDestroyWindow();
	
	return FReply::Handled();
}

void SLevelSnapshotsEditorCreationForm::OnWindowClosed(const TSharedRef<SWindow>& ParentWindow) const
{
	if (ProjectSettingsObjectPtr.IsValid())
	{
		const FVector2D WindowSize = ParentWindow->GetClientSizeInScreen();
		
		ProjectSettingsObjectPtr->SetLastCreationWindowSize(WindowSize);
		ProjectSettingsObjectPtr->SaveConfig();
	}

	if (DataManagementSettingsObjectPtr.IsValid())
	{
		DataManagementSettingsObjectPtr->SaveConfig();
	}

	if (bWasCreateSnapshotPressed)
	{
		CallOnCloseDelegate.ExecuteIfBound(DescriptionText, bSaveAsync);
	}
}
