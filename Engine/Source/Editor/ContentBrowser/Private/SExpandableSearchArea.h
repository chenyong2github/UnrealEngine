// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateTypes.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateWidgetStyleAsset.h"

class SSearchBox;

class SExpandableSearchArea : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SExpandableSearchArea)
		: _Style(&FAppStyle::Get().GetWidgetStyle<FSearchBoxStyle>("SearchBox"))
	{}
		/** Style used to draw this search box */
		SLATE_STYLE_ARGUMENT(FSearchBoxStyle, Style)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<SSearchBox> SearchBox);

	/** @return true if the search area is expanded and the search box exposed */
	bool IsExpanded() const { return bIsExpanded; }

	/** Sets whether or not the search area is expanded to expose the search box */
	void SetExpanded(bool bInExpanded);
private:

	FReply OnExpandSearchClicked();
	EVisibility GetSearchBoxVisibility() const;
	EVisibility GetSearchGlassVisibility() const;
	const FSlateBrush* GetExpandSearchImage() const;
private:
	const FSearchBoxStyle* SearchStyle;
	bool bIsExpanded;
	TWeakPtr<SSearchBox> SearchBoxPtr;
};
