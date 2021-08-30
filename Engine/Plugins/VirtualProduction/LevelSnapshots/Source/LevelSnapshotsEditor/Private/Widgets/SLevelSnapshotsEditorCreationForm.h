// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Settings/LevelSnapshotsEditorProjectSettings.h"
#include "Settings/LevelSnapshotsEditorDataManagementSettings.h"
#include "Dialogs/CustomDialog.h"

DECLARE_DELEGATE_TwoParams(FCloseCreationFormDelegate, const FText& /* Description */, bool /* bSaveAsync */);

class ULevelSnapshotsEditorProjectSettings;
class ULevelSnapshotsEditorDataManagementSettings;
class SWindow;

class SLevelSnapshotsEditorCreationForm : public SCustomDialog
{
public:

	static TSharedRef<SWindow> MakeAndShowCreationWindow(const FCloseCreationFormDelegate& CallOnClose, ULevelSnapshotsEditorProjectSettings* InProjectSettings, ULevelSnapshotsEditorDataManagementSettings* InDataManagementSettings);
	
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorCreationForm)
	{}
	SLATE_END_ARGS()

	void Construct(
		const FArguments& InArgs,
		TWeakPtr<SWindow> InWidgetWindow,
		const FCloseCreationFormDelegate& CallOnClose,
		ULevelSnapshotsEditorProjectSettings* InProjectSettings,
		ULevelSnapshotsEditorDataManagementSettings* InDataManagementSettings
		);

	~SLevelSnapshotsEditorCreationForm();

	TSharedRef<SWidget> MakeDataManagementSettingsDetailsWidget() const;

	FText GetNameOverrideText() const;

	void SetNameOverrideText(const FText& InNewText, ETextCommit::Type InCommitType);
	void SetDescriptionText(const FText& InNewText, ETextCommit::Type InCommitType);

	EVisibility GetNameDiffersFromDefaultAsVisibility() const;

	FReply OnResetNameClicked();
	FReply OnCreateButtonPressed();

	void OnWindowClosed(const TSharedRef<SWindow>& ParentWindow) const;

private:
	
	TWeakPtr< SWindow > WidgetWindow;
	TSharedPtr<SWidget> ResetPathButton;

	TWeakObjectPtr<ULevelSnapshotsEditorProjectSettings> ProjectSettingsObjectPtr;
	TWeakObjectPtr<ULevelSnapshotsEditorDataManagementSettings> DataManagementSettingsObjectPtr;

	bool bNameDiffersFromDefault = false;
	bool bWasCreateSnapshotPressed = false;

	FText DescriptionText;
	bool bSaveAsync = true;

	FCloseCreationFormDelegate CallOnCloseDelegate;
};
