// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SCheckBox.h"

struct FOpenVDBGridInfo;
struct FOpenVDBImportOptions;
struct FOpenVDBImportChannel;

class SOpenVDBImportWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOpenVDBImportWindow)
		: _ImportOptions()
		,_OpenVDBGridInfo()
		, _WidgetWindow()
		, _FullPath()
		, _MaxWindowHeight(0.0f)
		, _MaxWindowWidth(0.0f)
	{}

		SLATE_ARGUMENT(FOpenVDBImportOptions*, ImportOptions)
		SLATE_ARGUMENT(TArray<TSharedPtr<FOpenVDBGridInfo>>*, OpenVDBGridInfo)
		SLATE_ARGUMENT(TSharedPtr<SWindow>, WidgetWindow)
		SLATE_ARGUMENT(FText, FullPath)
		SLATE_ARGUMENT(float, MaxWindowHeight)
		SLATE_ARGUMENT(float, MaxWindowWidth)
	SLATE_END_ARGS()

public:
	virtual bool SupportsKeyboardFocus() const override { return true; }
	void Construct(const FArguments& InArgs);
	FReply OnImport();
	FReply OnCancel();
	bool ShouldImport() const;

private:
	FOpenVDBImportOptions*								ImportOptions;
	TArray<TSharedPtr<FOpenVDBGridInfo>>*				OpenVDBGridInfo;
	TSharedPtr<SComboBox<TSharedPtr<FOpenVDBGridInfo>>>	DensityGridComboBox;
	TSharedPtr<SCheckBox>								DensityGridCheckBox;
	TSharedPtr<SButton>									ImportButton;
	TWeakPtr<SWindow>									WidgetWindow;
	bool												bShouldImport;

	EActiveTimerReturnType SetFocusPostConstruct(double InCurrentTime, float InDeltaTime);
	bool CanImport()  const;
	FReply OnResetToDefaultClick();
	FText GetImportTypeDisplayText() const;
	void SetDefaultGridAssignment();
	TSharedRef<SWidget> CreateGridSelector(TSharedPtr<SComboBox<TSharedPtr<FOpenVDBGridInfo>>>& ComboBox, TSharedPtr<SCheckBox>& CheckBox, FOpenVDBImportChannel& Channel, const FText& Label);
};