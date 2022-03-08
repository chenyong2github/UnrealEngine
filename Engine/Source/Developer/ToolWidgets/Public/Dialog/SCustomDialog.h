// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWindow.h"

/**
 * This is a custom dialog class, which allows any Slate widget to be used as the contents,
 * with any number of buttons that have any text. 
 * It also supports adding a custom icon to the dialog.
 * 
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

 * Note: If the content is only text, see SMessageDialog.  
 */
class TOOLWIDGETS_API SCustomDialog : public SWindow
{
public:
	
	struct FButton
	{
		FButton(const FText& InButtonText, const FSimpleDelegate& InOnClicked = FSimpleDelegate())
			: ButtonText(InButtonText)
			, OnClicked(InOnClicked)
		{}

		/** Primary buttons are highlighted */
		FButton& SetPrimary(bool bValue) { bIsPrimary = bValue; return *this; }

		/** Called when the button is clicked */
		FButton& SetOnClicked(FSimpleDelegate InOnClicked) { OnClicked = MoveTemp(InOnClicked); return *this; }

		FText ButtonText;
		FSimpleDelegate OnClicked;
		bool bIsPrimary = false;
	};

	SLATE_BEGIN_ARGS(SCustomDialog) 
		: _HAlignIcon(HAlign_Left)
		, _VAlignIcon(VAlign_Center)
		, _RootPadding(FMargin(4.f))
		, _ButtonAreaPadding(FMargin(20.f, 0.f, 0.f, 0.f))
		, _UseScrollBox(true)
		, _ScrollBoxMaxHeight(300)
	{
		_AccessibleParams = FAccessibleWidgetData(EAccessibleBehavior::Auto);
	}

		/*********** Functional ***********/
		
		/** Title to display for the dialog. */
		SLATE_ARGUMENT(FText, Title)

		/** The content to display above the button; icon is optionally created to the left of it.  */
		SLATE_NAMED_SLOT(FArguments, Content)
	
		/** The buttons that this dialog should have. One or more buttons must be added.*/
		SLATE_ARGUMENT(TArray<FButton>, Buttons)

		/** Event triggered when the dialog is closed, either because one of the buttons is pressed, or the windows is closed. */
		SLATE_EVENT(FSimpleDelegate, OnClosed)
	

		/*********** Cosmetic ***********/

		/** Optional icon to display in the dialog (default: none) */
		SLATE_ARGUMENT(FName, IconBrush)
	
		/** When specified, ignore the brushes size and report the DesiredSizeOverride as the desired image size (default: use icon size) */
		SLATE_ATTRIBUTE(TOptional<FVector2D>, IconDesiredSizeOverride)

		/** Alignment of icon (default: HAlign_Left)*/
		SLATE_ARGUMENT(EHorizontalAlignment, HAlignIcon)

		/** Alignment of icon (default: VAlign_Center) */
		SLATE_ARGUMENT(EVerticalAlignment, VAlignIcon)
	
	
		/** Custom widget placed before the buttons */
		SLATE_NAMED_SLOT(FArguments, BeforeButtons)


		/** Padding to apply to the widget embedded in the window, i.e. to all widgets contained in the window (default: {4,4,4,4} )*/
		SLATE_ATTRIBUTE(FMargin, RootPadding)

		/** Padding to apply around the layout holding the buttons (default: {20,0,0,0}) */
		SLATE_ATTRIBUTE(FMargin, ButtonAreaPadding)

		/** Padding to apply to DialogContent - you can use it to move away from the icon (default: {0,0,0,0}) */
		SLATE_ATTRIBUTE(FMargin, ContentAreaPadding)
	

		/** Should this dialog use a scroll box for over-sized content? (default: true) */
		SLATE_ARGUMENT(bool, UseScrollBox)

		/** Max height for the scroll box (default: 300) */
		SLATE_ARGUMENT(int32, ScrollBoxMaxHeight)


		/********** Legacy - do not use **********/
	
		/** Content for the dialog (deprecated - use Content instead)*/
		SLATE_ARGUMENT_DEPRECATED(TSharedPtr<SWidget>, DialogContent, 5.1, "Use Content() instead.")

	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs);

	/** Show the dialog.
	 * This method will return immediately.
	 */ 
	void Show();

	/** Show a modal dialog. Will block until an input is received.
	 * Returns the index of the button that was pressed.
	 */
	int32 ShowModal();

private:

	TSharedRef<SWidget> CreateContentBox(const FArguments& InArgs);
	TSharedRef<SWidget> CreateButtonBox(const FArguments& InArgs);
	
	FReply OnButtonClicked(FSimpleDelegate OnClicked, int32 ButtonIndex);

	/** The index of the button that was pressed last. */
	int32 LastPressedButton = -1;

	FSimpleDelegate OnClosed;
};