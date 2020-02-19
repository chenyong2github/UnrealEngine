// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/SlateWidgetStyleAsset.h"

template <typename OptionType>
class SComboBox;

/**  A widget which allows the user to pick a name of a specified list of names. */
class DMXEDITOR_API SNameListPicker
	: public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnValueChanged, FName);

	SLATE_BEGIN_ARGS(SNameListPicker)
		: _ComboButtonStyle(&FCoreStyle::Get().GetWidgetStyle< FComboButtonStyle >("ComboButton"))
		, _ButtonStyle(nullptr)
		, _UpdateOptionsDelegate(nullptr)
		, _ForegroundColor(FCoreStyle::Get().GetSlateColor("InvertedForeground"))
		, _ContentPadding(FMargin(2.f, 0.f))
		, _HasMultipleValues(false)
		, _Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
	{}

		/** The visual style of the combo button */
		SLATE_STYLE_ARGUMENT(FComboButtonStyle, ComboButtonStyle)

		/** The visual style of the button (overrides ComboButtonStyle) */
		SLATE_STYLE_ARGUMENT(FButtonStyle, ButtonStyle)

		SLATE_ARGUMENT(FSimpleMulticastDelegate*, UpdateOptionsDelegate)

		/** List of possible names */
		SLATE_ATTRIBUTE(TArray<FName>, OptionsSource)
	
		/** Foreground color for the picker */
		SLATE_ATTRIBUTE(FSlateColor, ForegroundColor)
	
		/** Content padding for the picker */
		SLATE_ATTRIBUTE(FMargin, ContentPadding)
	
		/** Attribute used to retrieve the current value. */
		SLATE_ATTRIBUTE(FName, Value)
	
		/** Delegate for handling when for when the current value changes. */
		SLATE_EVENT(FOnValueChanged, OnValueChanged)
	
		/** Attribute used to retrieve whether the picker has multiple values. */
		SLATE_ATTRIBUTE(bool, HasMultipleValues)

		/** Sets the font used to draw the text on the button */
		SLATE_ATTRIBUTE(FSlateFontInfo, Font)

	SLATE_END_ARGS()

	/**  Slate widget construction method */
	void Construct(const FArguments& InArgs);

	virtual ~SNameListPicker();

private:
	FText GetCurrentNameLabel() const;

	void UpdateOptionsSource();

	/** Create an option widget for the combo box */
	TSharedRef<SWidget> GenerateNameItemWidget(TSharedPtr<FName> InItem);
	/** Handles a selection change from the combo box */
	void HandleSelectionChanged(const TSharedPtr<FName> Item, ESelectInfo::Type SelectInfo);

	TSharedPtr<FName> GetSelectedItemFromCurrentValue() const;

	/**
	 * Workaround to keep the correct option highlighted in the dropdown menu.
	 * When code changes the current value of the property this button represents, it's possible
	 * that the button will keep the previous value highlighted. So we set the currently highlighted
	 * option every time the menu is opened.
	 */
	void UpdateSelectedOption();

private:
	TWeakPtr<SComboBox<TSharedPtr<FName>>> PickerComboButton;
	TAttribute<TArray<FName>> OptionsSourceAttr;
	TArray<TSharedPtr<FName>> OptionsSource;

	FSimpleMulticastDelegate* UpdateOptionsDelegate;
	FDelegateHandle UpdateOptionsHandle;

	TAttribute<FName> ValueAttribute;
	FOnValueChanged OnValueChangedDelegate;
	TAttribute<bool> HasMultipleValuesAttribute;
};
