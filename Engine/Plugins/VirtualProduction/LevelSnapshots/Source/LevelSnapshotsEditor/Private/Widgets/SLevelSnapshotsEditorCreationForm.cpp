// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLevelSnapshotsEditorCreationForm.h"

#include "Settings/LevelSnapshotsEditorProjectSettings.h"
#include "LevelSnapshotsEditorStyle.h"

#include "CoreMinimal.h"
#include "Dialogs/CustomDialog.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWindow> FLevelSnapshotsEditorCreationForm::MakeAndShowCreationWindow(
	const FCloseCreationFormDelegate& CallOnClose, ULevelSnapshotsEditorProjectSettings* InProjectSettings)
{
	check(InProjectSettings);
	
	// Compute centered window position based on max window size, which include when all categories are expanded
	const float BaseWidth = 350.0f;
	const float BaseHeight = 200.0f;
	const FVector2D BaseWindowSize = FVector2D(BaseWidth, BaseHeight); // Max window size it can get based on current slate


	const FSlateRect WorkAreaRect = FSlateApplicationBase::Get().GetPreferredWorkArea();
	const FVector2D DisplayTopLeft(WorkAreaRect.Left, WorkAreaRect.Top);
	const FVector2D DisplaySize(WorkAreaRect.Right - WorkAreaRect.Left, WorkAreaRect.Bottom - WorkAreaRect.Top);

	const FVector2D WindowPosition = (DisplayTopLeft + (DisplaySize - BaseWindowSize) / 2.0f);

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(NSLOCTEXT("LevelSnapshots", "LevelSnapshots_CreationForm_Title", "Create Level Snapshot"))
		.SizingRule(ESizingRule::Autosized)
		.AutoCenter(EAutoCenter::PrimaryWorkArea)
		.ClientSize(BaseWindowSize)
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.ScreenPosition(WindowPosition);

	const TSharedRef<SLevelSnapshotsEditorCreationForm> CreationForm = SNew(SLevelSnapshotsEditorCreationForm, Window, CallOnClose, InProjectSettings);
	
	Window->SetContent
	(
		CreationForm
	);

	Window->SetOnWindowClosed(FOnWindowClosed::CreateSP(CreationForm, &SLevelSnapshotsEditorCreationForm::OnWindowClosed));

	FSlateApplication::Get().AddWindow(Window);

	return Window;
}

void SLevelSnapshotsEditorCreationForm::Construct(
	const FArguments& InArgs, TWeakPtr< SWindow > InWidgetWindow, const FCloseCreationFormDelegate& CallOnClose, ULevelSnapshotsEditorProjectSettings* InProjectSettings)
{
	check(InProjectSettings);

	ProjectSettingsObjectPtr = InProjectSettings;
	
	WidgetWindow = InWidgetWindow;
	CallOnCloseDelegate = CallOnClose;

	bNameDiffersFromDefault = ProjectSettingsObjectPtr.Get()->IsNameOverridden();
	bDirDiffersFromDefault = ProjectSettingsObjectPtr.Get()->IsPathOverridden();
	
	ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.ActorGroupBorder"))
			[
				SNew(SVerticalBox)

				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), "NormalText.Important")
					.Text(NSLOCTEXT("LevelSnapshots", "CreationForm_SnapshotNameLabel", "Name"))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
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

				+SVerticalBox::Slot()
				.Padding(8, 20, 8, 20)
				.AutoHeight()
				[
					SNew(SMultiLineEditableTextBox)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.BackgroundColor(FLinearColor(0.2f, 0.2f, 0.2f))
					.ForegroundColor(FSlateColor::UseForeground())
					.SelectAllTextWhenFocused(true)
					.HintText(NSLOCTEXT("LevelSnapshots", "CreationForm_DescriptionHintText", "<description>"))
					.Text(DescriptionText)
					.OnTextCommitted(this, &SLevelSnapshotsEditorCreationForm::SetDescriptionText)
				]
				
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), "NormalText.Important")
					.Text(NSLOCTEXT("LevelSnapshots", "CreationForm_SaveDirLabel", "Save Directory"))
				]

				+SVerticalBox::Slot()
				[
					SNew(SHorizontalBox)

					+SHorizontalBox::Slot()
					[
						SNew(SEditableTextBox)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
						.BackgroundColor(FLinearColor::Transparent)
						.ForegroundColor(FSlateColor::UseForeground())
						.Justification(ETextJustify::Center)
						.SelectAllTextWhenFocused(true)
						.HintText(NSLOCTEXT("LevelSnapshots", "CreationForm_SaveDirOverrideHintText", "Override save directory..."))
						.Text(this, &SLevelSnapshotsEditorCreationForm::GetPathOverrideText)
						.OnTextCommitted(this, &SLevelSnapshotsEditorCreationForm::SetPathOverrideText)
						.ToolTipText(
						NSLOCTEXT("LevelSnapshots", "CreationForm_PathOverrideFieldTooltipText", "Override the save directory defined in Project Settings while using the Creation Form."))
					]

					+SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.IsFocusable(false)
						.ToolTipText(
							NSLOCTEXT("LevelSnapshots", "CreationForm_ResetDirTooltipText", "Reset the overridden save directory to the one defined in Project Settings."))
						.ButtonStyle(FEditorStyle::Get(), "NoBorder")
						.ContentPadding(0)
						.Visibility(this, &SLevelSnapshotsEditorCreationForm::GetDirDiffersFromDefaultAsVisibility)
						.OnClicked(this, &SLevelSnapshotsEditorCreationForm::OnResetDirClicked)
						.Content()
						[
							SNew(SImage)
							.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
						]
						
					]
				]

				+SVerticalBox::Slot()
				.VAlign(VAlign_Bottom)
				.HAlign(HAlign_Right)
				.Padding(2.f, 5.f)
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
		];
}

SLevelSnapshotsEditorCreationForm::~SLevelSnapshotsEditorCreationForm()
{
	CallOnCloseDelegate.Unbind();
}

FText SLevelSnapshotsEditorCreationForm::GetNameOverrideText() const
{
	check(ProjectSettingsObjectPtr.IsValid());

	return FText::FromString(ProjectSettingsObjectPtr.Get()->GetNameOverride());
}

void SLevelSnapshotsEditorCreationForm::SetNameOverrideText(const FText& InNewText, ETextCommit::Type InCommitType)
{
	check(ProjectSettingsObjectPtr.IsValid());

	FString NameAsString = InNewText.ToString();
	ULevelSnapshotsEditorProjectSettings::SanitizePathInline(NameAsString, true);

	ProjectSettingsObjectPtr->SetNameOverride(NameAsString);

	bNameDiffersFromDefault = ProjectSettingsObjectPtr.Get()->IsNameOverridden();
}

void SLevelSnapshotsEditorCreationForm::SetDescriptionText(const FText& InNewText, ETextCommit::Type InCommitType)
{
	check(ProjectSettingsObjectPtr.IsValid());

	DescriptionText = InNewText;
}

FText SLevelSnapshotsEditorCreationForm::GetPathOverrideText() const
{
	check(ProjectSettingsObjectPtr.IsValid());

	return FText::FromString(ProjectSettingsObjectPtr.Get()->GetSaveDirOverride());
}

void SLevelSnapshotsEditorCreationForm::SetPathOverrideText(const FText& InNewText, ETextCommit::Type InCommitType)
{
	check(ProjectSettingsObjectPtr.IsValid());
	
	FString PathAsString = InNewText.ToString();
	ULevelSnapshotsEditorProjectSettings::SanitizePathInline(PathAsString, true);

	ProjectSettingsObjectPtr->SetSaveDirOverride(PathAsString);

	bDirDiffersFromDefault = ProjectSettingsObjectPtr.Get()->IsPathOverridden();
}

EVisibility SLevelSnapshotsEditorCreationForm::GetNameDiffersFromDefaultAsVisibility() const
{
	return bNameDiffersFromDefault ? EVisibility::Visible : EVisibility::Hidden;
}

EVisibility SLevelSnapshotsEditorCreationForm::GetDirDiffersFromDefaultAsVisibility() const
{
	return bDirDiffersFromDefault ? EVisibility::Visible : EVisibility::Hidden;
}

FReply SLevelSnapshotsEditorCreationForm::OnResetNameClicked()
{
	check(ProjectSettingsObjectPtr.IsValid());

	SetNameOverrideText(FText::FromString(ProjectSettingsObjectPtr.Get()->DefaultLevelSnapshotName), ETextCommit::OnEnter);

	return FReply::Handled();
}

FReply SLevelSnapshotsEditorCreationForm::OnResetDirClicked()
{
	check(ProjectSettingsObjectPtr.IsValid());
	
	SetPathOverrideText(FText::FromString(ProjectSettingsObjectPtr.Get()->LevelSnapshotSaveDir), ETextCommit::OnEnter);

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
	CallOnCloseDelegate.ExecuteIfBound(bWasCreateSnapshotPressed, DescriptionText);
}
