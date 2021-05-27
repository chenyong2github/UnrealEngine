// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Input/SComboButton.h"

class EDITORWIDGETS_API SEnumComboBox : public SComboButton
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
	TSharedRef<SWidget> OnGetMenuContent();

private:

	struct FEnumInfo
	{
		FEnumInfo() = default;
		FEnumInfo(const int32 InIndex, const int32 InValue, const FText InDisplayName, const FText InTooltipText)
			: Index(InIndex), Value(InValue), DisplayName(InDisplayName), TooltipText(InTooltipText)
		{}
		
		int32 Index = 0;
		int32 Value = 0;
		FText DisplayName;
		FText TooltipText;
	};
	
	const UEnum* Enum;

	TAttribute<int32> CurrentValue;

	TAttribute<FSlateFontInfo> Font;

	TArray<FEnumInfo> VisibleEnums;

	bool bUpdatingSelectionInternally;

	bool bIsBitflagsEnum;
	
	FOnGetToolTipForValue OnGetToolTipForValue;

	FOnEnumSelectionChanged OnEnumSelectionChangedDelegate;
};
