// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"

class SSourceControlDescriptionWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSourceControlDescriptionWidget)
		: _ParentWindow()
		, _Label()
		, _Text()
	{}

		SLATE_ATTRIBUTE(TSharedPtr<SWindow>, ParentWindow)
		SLATE_ATTRIBUTE(FText, Label)
		SLATE_ATTRIBUTE(FText, Text)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	/** Gets dialog result */
	bool GetResult() const { return bResult; }

	/** Used to intercept Escape key press, and interpret it as cancel */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Returns the text currently in the edit box */
	FText GetDescription() const;

private:
	/** Called when the settings of the dialog are to be accepted*/
	FReply OKClicked();

	/** Called when the settings of the dialog are to be ignored*/
	FReply CancelClicked();

private:
	bool bResult = false;

	/** Pointer to the parent modal window */
	TWeakPtr<SWindow> ParentWindow;

	TSharedPtr< SMultiLineEditableTextBox> TextBox;
};

bool GetChangelistDescription(
	const TSharedPtr<SWidget>& ParentWidget,
	const FText& InWindowTitle,
	const FText& InLabel,
	FText& OutDescription);