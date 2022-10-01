// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateColor.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"


/** Pin Viewer custom details row left indent.
 * Draws a shadow to differentiate the different indent levels.
 * 
 * Copy of  SDetailRowIndent.h. */
class SPinViewerDetailRowIndent : public SCompoundWidget
{ 
public:
	SLATE_BEGIN_ARGS(SPinViewerDetailRowIndent) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<SWidget> Row);

private:
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const;

	TWeakPtr<SWidget> Row;
};
