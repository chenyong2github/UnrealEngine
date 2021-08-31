// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/SlateDelegates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SComboButton.h"

class EDITORWIDGETS_API SSimpleComboButton : public SComboButton
{
public:

	SLATE_BEGIN_ARGS(SSimpleComboButton)
	{}
		/** The text to display in the button. */
		SLATE_ATTRIBUTE(FText, Text)

		SLATE_ATTRIBUTE(const FSlateBrush*, Icon)
	
		/** The static menu content widget. */
		SLATE_NAMED_SLOT(FArguments, MenuContent)

		SLATE_EVENT(FOnGetContent, OnGetMenuContent)
		SLATE_EVENT(FOnComboBoxOpened, OnComboBoxOpened)
		SLATE_EVENT(FOnIsOpenChanged, OnMenuOpenChanged)

	SLATE_END_ARGS()

	SSimpleComboButton() {}

	void Construct(const FArguments& InArgs);
};