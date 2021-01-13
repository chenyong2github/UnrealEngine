// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Input/SComboBox.h"

class EDITORWIDGETS_API SEnumComboBox : public SComboBox<TSharedPtr<int32>>
{
public:
	DECLARE_DELEGATE_TwoParams(FOnEnumSelectionChanged, int32, ESelectInfo::Type);
	DECLARE_DELEGATE_RetVal_OneParam(FText, FOnGetToolTipForValue, int32 /* Value */)

public:
	SLATE_BEGIN_ARGS(SEnumComboBox)
		: _CurrentValue()
		, _ContentPadding(FMargin(4.0, 2.0))
		, _ButtonStyle(nullptr)
	{}

		SLATE_ATTRIBUTE(int32, CurrentValue)
		SLATE_ATTRIBUTE(FMargin, ContentPadding)
		SLATE_ATTRIBUTE(FSlateFontInfo, Font)
		SLATE_EVENT(FOnEnumSelectionChanged, OnEnumSelectionChanged)
		SLATE_EVENT(FOnGetToolTipForValue, OnGetToolTipForValue)
		SLATE_STYLE_ARGUMENT(FButtonStyle, ButtonStyle)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const UEnum* InEnum);

private:
	FText GetCurrentValueText() const;
	FText GetCurrentValueTooltip() const;
	bool GetValueIsEnabled(TWeakPtr<int32> InValueWeak) const;
	TSharedRef<SWidget> OnGenerateWidget(TSharedPtr<int32> InItem);
	void OnComboSelectionChanged(TSharedPtr<int32> InSelectedItem, ESelectInfo::Type SelectInfo);
	void OnComboMenuOpening();

private:
	const UEnum* Enum;

	TAttribute<int32> CurrentValue;

	TAttribute<FSlateFontInfo> Font;

	TArray<TSharedPtr<int32>> VisibleEnumNameIndices;

	bool bUpdatingSelectionInternally;

	FOnGetToolTipForValue OnGetToolTipForValue;

	FOnEnumSelectionChanged OnEnumSelectionChangedDelegate;
};
