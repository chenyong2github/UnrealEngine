// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Settings/LevelSnapshotsEditorProjectSettings.h"
#include "Settings/LevelSnapshotsEditorDataManagementSettings.h"
#include "Dialogs/CustomDialog.h"

DECLARE_DELEGATE_TwoParams(FCloseCreationFormDelegate, bool, FText)

class ULevelSnapshotsEditorProjectSettings;
class ULevelSnapshotsEditorDataManagementSettings;
class SWindow;

struct FLevelSnapshotsEditorCreationForm
{
	static TSharedRef<SWindow> MakeAndShowCreationWindow(
		const FCloseCreationFormDelegate& CallOnClose, 
		ULevelSnapshotsEditorProjectSettings* InProjectSettings, ULevelSnapshotsEditorDataManagementSettings* InDataManagementSettings);
};

class SLevelSnapshotsEditorCreationForm : public SCustomDialog
{
public:

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorCreationForm)
	{}
	SLATE_END_ARGS()

	void Construct(
		const FArguments& InArgs, TWeakPtr< SWindow > InWidgetWindow, const FCloseCreationFormDelegate& CallOnClose, 
		ULevelSnapshotsEditorProjectSettings* InProjectSettings, ULevelSnapshotsEditorDataManagementSettings* InDataManagementSettings);

	~SLevelSnapshotsEditorCreationForm();

	TSharedRef<SWidget> MakeDataManagementSettingsDetailsWidget() const;

	FText GetNameOverrideText() const;

	void SetNameOverrideText(const FText& InNewText, ETextCommit::Type InCommitType);
	void SetDescriptionText(const FText& InNewText, ETextCommit::Type InCommitType);

	FText GetPathOverrideText() const;
	void SetPathOverrideText(const FText& InNewText, ETextCommit::Type InCommitType);

	EVisibility GetNameDiffersFromDefaultAsVisibility() const;
	EVisibility GetDirDiffersFromDefaultAsVisibility() const;

	FReply OnResetNameClicked();
	FReply OnResetDirClicked();
	FReply OnCreateButtonPressed();

	void OnWindowClosed(const TSharedRef<SWindow>& ParentWindow) const;

private:
	
	TWeakPtr< SWindow > WidgetWindow;
	TSharedPtr<SWidget> ResetPathButton;

	TWeakObjectPtr<ULevelSnapshotsEditorProjectSettings> ProjectSettingsObjectPtr;
	TWeakObjectPtr<ULevelSnapshotsEditorDataManagementSettings> DataManagementSettingsObjectPtr;

	bool bNameDiffersFromDefault = false;
	bool bDirDiffersFromDefault = false;
	bool bWasCreateSnapshotPressed = false;

	FText DescriptionText;

	FCloseCreationFormDelegate CallOnCloseDelegate;

};
