// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateColor.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FDMXEditor;
class FDMXFixturePatchNode;
class UDMXEntityFixturePatch;
class UDMXLibrary;


class SDMXFixturePatchFragment
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXFixturePatchFragment)
		: _DMXEditor(nullptr)
		, _Text(FText::GetEmpty())
		, _Column(-1)
		, _Row(-1)
		, _ColumnSpan(1)
		, _bHighlight(false)
	{}
		SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)

		SLATE_ATTRIBUTE(FText, Text)

		SLATE_ARGUMENT(int32, Column)

		SLATE_ARGUMENT(int32, Row)

		SLATE_ARGUMENT(int32, ColumnSpan)

		SLATE_ARGUMENT(bool, bHighlight)

	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs, TSharedPtr<FDMXFixturePatchNode> InPatchNode);

	/** Sets wether the fragment should be highlit */
	void SetHighlight(bool bEnabled);
	
	/** Returns the Column of the fragment */
	int32 GetColumn() const { return Column; }

	/** Sets the Column of the fragment */
	void SetColumn(int32 NewColumn) { Column = NewColumn; }

	/** Returns the Row of the fragment */
	int32 GetRow() const { return Row; }

	/** Sets the Row of the fragment */
	void SetRow(int32 NewRow) { Row = NewRow; }

	/** Returns the Column span of the fragment */
	int32 GetColumnSpan() const { return ColumnSpan; }

	/** Sets the Column span of the fragment */
	void SetColumnSpan(int32 NewColumnSpan) { ColumnSpan = NewColumnSpan; }

protected:
	// Begin SWidget interface
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	// End SWidget interface

	/** Gets the text displayed on the fragment */
	FText GetText() const;

	/** Gets the tooltip text displayed on the fragment */
	FText GetToolTipText() const;

	/** Gets the color of the fragment */
	FSlateColor GetColor() const;

	/** Gets the shadow brush of the fragment, depending on wether it's selected */
	const FSlateBrush* GetShadowBrush(bool bSelected) const;

	/** Column of the fragment */
	int32 Column;

	/** Row of the fragment */
	int32 Row;

	/** Column span of the fragment */
	int32 ColumnSpan;

	/** If true, the widget shows a highlight */
	bool bHighlight = false;

	/** Size of the shadow */
	FVector2D ShadowSize;

	/** Fixture Patch Node being displayed by this widget */
	TSharedPtr<FDMXFixturePatchNode> PatchNode;

	/** Weak DMXEditor refrence */
	TWeakPtr<FDMXEditor> DMXEditorPtr;
};
