// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWindow.h"

/** This is a custom dialog class, which allows any Slate widget to be used as the contents,
 * with any number of buttons that have any text. 
 * It also supports adding a custom icon to the dialog.
 * Usage:
 * TSharedRef<SCustomDialog> HelloWorldDialog = SNew(SCustomDialog)
		.Title(FText(LOCTEXT("HelloWorldTitleExample", "Hello, World!")))
		.DialogContent( SNew(SImage).Image(FName(TEXT("Hello"))))
		.Buttons({
			SCustomDialog::FButton(LOCTEXT("OK", "OK")),
			SCustomDialog::FButton(LOCTEXT("Cancel", "Cancel"))
		});

   // returns 0 when OK is pressed, 1 when Cancel is pressed, -1 if the window is closed
   const int ButtonPressed = HelloWorldDialog->ShowModal();
 */
class UNREALED_API SCustomDialog : public SWindow
{
public:
	struct FButton
	{
		FButton(const FText& InButtonText, const FSimpleDelegate& InOnClicked = FSimpleDelegate())
			: ButtonText(InButtonText),
			OnClicked(InOnClicked)
		{
		}

		FText ButtonText;
		FSimpleDelegate OnClicked;
	};

	SLATE_BEGIN_ARGS(SCustomDialog) 
		: _UseScrollBox(true)
		, _ScrollBoxMaxHeight(300)
	{
		_AccessibleParams = FAccessibleWidgetData(EAccessibleBehavior::Auto);
	}
		/** Title to display for the dialog. */
		SLATE_ARGUMENT(FText, Title)

		/** Optional icon to display in the dialog. (default: none) */
		SLATE_ARGUMENT(FName, IconBrush)

		/** Should this dialog use a scroll box for over-sized content? (default: true) */
		SLATE_ARGUMENT(bool, UseScrollBox)

		/** Max height for the scroll box (default: 300) */
		SLATE_ARGUMENT(int32, ScrollBoxMaxHeight)

		/** The buttons that this dialog should have. One or more buttons must be added.*/
		SLATE_ARGUMENT(TArray<FButton>, Buttons)

		/** Content for the dialog */
		SLATE_ARGUMENT(TSharedPtr<SWidget>, DialogContent)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Show the dialog.
	 * This method will return immediately.
	 */ 
	void Show();

	/** Show a modal dialog. Will block until an input is received.
	 * Returns the index of the button that was pressed.
	 */
	int ShowModal();

private:
	FReply OnButtonClicked(FSimpleDelegate OnClicked, int ButtonIndex);

	/** The index of the button that was pressed last. */
	int LastPressedButton = -1;
};