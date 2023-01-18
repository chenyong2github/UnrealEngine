// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UDMXControlConsoleFaderBase;
template<typename NumericType> class SDMXControlConsoleEditorSpinBoxVertical;

struct FSlateColor;
class SInlineEditableTextBlock;


/** Individual fader UI class */
class SDMXControlConsoleEditorFader
	: public SCompoundWidget
{	
public:
	SLATE_BEGIN_ARGS(SDMXControlConsoleEditorFader)
	{}

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs, const TObjectPtr<UDMXControlConsoleFaderBase>& InFader);

	/** Sets the value of the fader by a percentage value */
	void SetValueByPercentage(float InNewPercentage);

protected:
	//~ Begin SWidget interface
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	//~ End of SWidget interface

private:
	/** Generates ExpanderArrow widget  */
	TSharedRef<SWidget> GenerateMuteButtonWidget();

	/** Gets wheter this Fader is selected or not */
	bool IsSelected() const;

	/** Gets the Fader Name */
	FString GetFaderName() const;

	/**  Gets current FaderName */
	FText GetFaderNameText() const;

	/** Called when the FaderName border was Clicked */
	FReply OnFaderNameBorderClicked(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent);

	/** Called when the fader name changes */
	void OnFaderNameCommitted(const FText& NewFaderName, ETextCommit::Type InCommit);

	/** Gets Fader's value */
	uint32 GetValue() const;

	/** Gets the Fader's minimum value */
	TOptional<uint32> GetMinValue() const;

	/** Returns the min value as text */
	FText GetMinValueAsText() const;

	/** Gets Fader's maximum value */
	TOptional<uint32> GetMaxValue() const;

	/** Returns the max value as text */
	FText GetMaxValueAsText() const;

	/** Handles when the user changes the Fader value */
	void HandleValueChanged(uint32 NewValue);

	/** Called when fader selection changes */
	void OnSelectionChanged(UDMXControlConsoleFaderBase* InFader);

	/** Called when the delete button was clicked */
	FReply OnDeleteClicked();

	/** Called to mute/unmute this Fader */
	FReply OnMuteClicked();

	/** Gets correct text for mute button */
	FSlateColor GetMuteButtonColor() const;

	/** Gets wheter the FaderSpinBox widget should be enabled or not */
	bool GetFaderSpinBoxEnabled() const;

	/**  Gets visibility attribute of the delete button */
	EVisibility GetDeleteButtonVisibility() const;

	/**Change fader background color on hover */
	const FSlateBrush* GetBorderImage() const;

	/** Reference to the Fader being displayed */
	TWeakObjectPtr<UDMXControlConsoleFaderBase> Fader;

	/** Widget showing the freely definable name of the fader */
	TSharedPtr<SInlineEditableTextBlock> FaderNameTextBox;

	/** The actual editable fader */
	TSharedPtr<SDMXControlConsoleEditorSpinBoxVertical<uint32>> FaderSpinBox;
};
