// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/STableRow.h"


/**
* A collection of widgets and helpers used for the style of the trees in SKismetDebuggingView
*/
namespace PropertyInfoViewStyle
{

	/**
	 * Used to indent within stylized details tree to achieve a layered effect
	 * see KismetDebugViewStyle::SIndent for usage
	 * @param IndentLevel depth of the tree
	 * @param IsHovered will give a lighter color if this line in the tree is hovered
	 * @return the color to set the indent to
	 */
	UNREALED_API FSlateColor GetIndentBackgroundColor(int32 IndentLevel, bool IsHovered);

	/**
	 * calls KismetDebugViewStyle::GetIndentBackgroundColor using the indent level
	 * and hover state of the provided table row
	 * @param Row a table row - likely nested within a tree
	 * @return the color to set the background of the row to
	 */
	UNREALED_API FSlateColor GetRowBackgroundColor(ITableRow* Row);

	/** Helper class to force a widget to fill in a space. Copied from SDetailSingleItemRow.cpp */
	class UNREALED_API SConstrainedBox : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SConstrainedBox){}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_ATTRIBUTE(TOptional<float>, MinWidth)
			SLATE_ATTRIBUTE(TOptional<float>, MaxWidth)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs);

		virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

	private:
		TAttribute< TOptional<float> > MinWidth;
		TAttribute< TOptional<float> > MaxWidth;
	};

	/**
	 * SIndent is a widget used to indent trees in a layered
	 * style. It's based on SDetailRowIndent but generalized to allow for use with
	 * any ITableRow
	 * 
	 * see SDebugLineItem::GenerateWidgetForColumn (SKismetDebuggingView.cpp) for
	 * use example
	 */
	class UNREALED_API SIndent : public SCompoundWidget
	{ 
	public:
			
		SLATE_BEGIN_ARGS(SIndent) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, TSharedRef<ITableRow> DetailsRow);

	private:
		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		                      const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		                      int32 LayerId, const FWidgetStyle& InWidgetStyle,
		                      bool bParentEnabled) const override;

		virtual FOptionalSize GetIndentWidth() const;

		virtual FSlateColor GetRowBackgroundColor(int32 IndentLevel) const;

	private:
		TWeakPtr<ITableRow> Row;
	};

	/**
	 * SExpanderArrow is a widget intended to be used alongside SIndent.
	 * It's based on SDetailExpanderArrow but generalized to allow for use with
	 * any ITableRow
	 * 
	 * see SDebugLineItem::GenerateWidgetForColumn (SKismetDebuggingView.cpp) for
	 * use example
	 */
	class UNREALED_API SExpanderArrow : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SExpanderArrow) {}
			SLATE_ATTRIBUTE(bool, HasChildren)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, TSharedRef<ITableRow> DetailsRow);

	private:
		EVisibility GetExpanderVisibility() const;

		const FSlateBrush* GetExpanderImage() const;

		FReply OnExpanderClicked() const;

	private:
		TWeakPtr<ITableRow> Row;
		TSharedPtr<SButton> ExpanderArrow;
		TAttribute<bool> HasChildren;
	};
}
