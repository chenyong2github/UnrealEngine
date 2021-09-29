// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateTypes.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateWidgetStyleAsset.h"

class SSearchBox;

DECLARE_DELEGATE(FOnSearchBoxShown)

class SSearchToggleButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSearchToggleButton)
		: _Style(&FAppStyle::Get().GetWidgetStyle<FSearchBoxStyle>("SearchBox"))
	{}
		/** Search box style (used to match the glass icon) */
		SLATE_STYLE_ARGUMENT(FSearchBoxStyle, Style)

		/** Event fired when the associated search box is made visible */
		SLATE_EVENT(FOnSearchBoxShown, OnSearchBoxShown)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<SSearchBox> SearchBox);

	/** @return true if the search area is expanded and the search box exposed */
	bool IsExpanded() const { return bIsExpanded; }

	/** Sets whether or not the search area is expanded to expose the search box */
	void SetExpanded(bool bInExpanded);
private:

	ECheckBoxState GetToggleButtonState() const;
	void OnToggleButtonStateChanged(ECheckBoxState CheckBoxState);
	EVisibility GetSearchBoxVisibility() const;
private:
	const FSearchBoxStyle* SearchStyle;
	bool bIsExpanded;
	FOnSearchBoxShown OnSearchBoxShown;
	TWeakPtr<SSearchBox> SearchBoxPtr;
};
