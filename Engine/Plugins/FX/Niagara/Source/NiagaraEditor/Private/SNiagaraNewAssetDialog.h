// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWindow.h"
#include "AssetData.h"
#include "IAssetTypeActions.h"
#include "ContentBrowserDelegates.h"


class SNiagaraAssetPickerList;
class SBox;

/** A modal dialog to collect information needed to create a new niagara system. */
class SNiagaraNewAssetDialog : public SWindow
{
public:
	DECLARE_DELEGATE_OneParam(FOnGetSelectedAssetsFromPicker, TArray<FAssetData>& /* OutSelectedAssets */);
	DECLARE_DELEGATE(FOnSelectionConfirmed);

public:
	class FNiagaraNewAssetDialogOption
	{
	public:
		FText OptionText;
		FText OptionDescription;
		FText AssetPickerHeader;
		TSharedRef<SWidget> AssetPicker;
		FOnGetSelectedAssetsFromPicker OnGetSelectedAssetsFromPicker;
		FOnSelectionConfirmed OnSelectionConfirmed;

		FNiagaraNewAssetDialogOption(FText InOptionText, FText InOptionDescription, FText InAssetPickerHeader, FOnGetSelectedAssetsFromPicker InOnGetSelectedAssetsFromPicker, FOnSelectionConfirmed InOnSelecitonConfirmed, TSharedRef<SWidget> InAssetPicker)
			: OptionText(InOptionText)
			, OptionDescription(InOptionDescription)
			, AssetPickerHeader(InAssetPickerHeader)
			, AssetPicker(InAssetPicker)
			, OnGetSelectedAssetsFromPicker(InOnGetSelectedAssetsFromPicker)
			, OnSelectionConfirmed(InOnSelecitonConfirmed)
		{
		}
	};

public:
	SLATE_BEGIN_ARGS(SNiagaraNewAssetDialog)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FName InSaveConfigKey, FText AssetTypeDisplayName, TArray<FNiagaraNewAssetDialogOption> InOptions);
	void GetAssetPicker();
	void ResetStage();
	bool GetUserConfirmedSelection() const;

protected:
	const TArray<FAssetData>& GetSelectedAssets() const;

	void ConfirmSelection();
	int32 GetSelectedObjectIndex() const { return SelectedOptionIndex; };

protected:
	TSharedPtr<SBox> AssetSettingsPage;

private:

	void OnWindowClosed(const TSharedRef<SWindow>& Window);

	FSlateColor GetOptionBorderColor(int32 OptionIndex) const;

	ECheckBoxState GetOptionCheckBoxState(int32 OptionIndex) const;

	void OptionCheckBoxStateChanged(ECheckBoxState InCheckBoxState, int32 OptionIndex);
	FSlateColor GetOptionTextColor(int32 OptionIndex) const;

	FText GetAssetPickersLabelText() const;
	bool IsOkButtonEnabled() const;
	void OnOkButtonClicked();
	void OnCancelButtonClicked();
	bool HasAssetPage() const;
	void SaveConfig();

private:
	FName SaveConfigKey;
	TArray<FNiagaraNewAssetDialogOption> Options;
	int32 SelectedOptionIndex;
	bool bUserConfirmedSelection;
	TArray<FAssetData> SelectedAssets;
	bool bOnAssetStage;
};