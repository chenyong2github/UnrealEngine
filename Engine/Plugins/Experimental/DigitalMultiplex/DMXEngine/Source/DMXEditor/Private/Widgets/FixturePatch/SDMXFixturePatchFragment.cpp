// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXFixturePatchFragment.h"

#include "DMXEditor.h"
#include "DMXFixturePatchNode.h"
#include "DMXFixturePatchEditorDefinitions.h"
#include "DMXEntityDragDropOp.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixturePatch.h"

#include "EditorStyleSet.h"
#include "SlateOptMacros.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"

#define LOCTEXT_NAMESPACE "SDMXFixturePatchFragment"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SDMXFixturePatchFragment::Construct(const FArguments& InArgs, TSharedPtr<FDMXFixturePatchNode> InPatchNode)
{
	check(InPatchNode.IsValid());
	check(Column != -1);
	check(Row != -1);
	check(ColumnSpan != -1);

	PatchNode = InPatchNode;
		
	DMXEditorPtr = InArgs._DMXEditor;
	Column = InArgs._Column;
	Row = InArgs._Row;
	ColumnSpan = InArgs._ColumnSpan;
	bHighlight = InArgs._bHighlight;
	OnSelected = InArgs._OnSelected;	

	FMargin MinimalTextMargin = FMargin(3.0f, 2.0f, 4.0f, 1.0f);

	ShadowSize = FEditorStyle::GetVector(TEXT("Graph.Node.ShadowSize"));

	// We do not need graph node feats, but mimic its visuals
	ChildSlot
		[
			SNew(SBox)
			.ToolTipText(this, &SDMXFixturePatchFragment::GetToolTipText)
			.MaxDesiredHeight(1.0f)
			.MaxDesiredWidth(1.0f)			
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SImage)
					.Image( FEditorStyle::GetBrush("Graph.VarNode.Body") )		
					.ColorAndOpacity( this, &SDMXFixturePatchFragment::GetColor )
				]
				+ SOverlay::Slot()
				.VAlign(VAlign_Top)
				[
					SNew(SImage)
					.Image( FEditorStyle::GetBrush("Graph.VarNode.ColorSpill") )
					.ColorAndOpacity( this, &SDMXFixturePatchFragment::GetColor )
				]
				+ SOverlay::Slot()
				[
					SNew(SImage)
					.Image( FEditorStyle::GetBrush("Graph.VarNode.Gloss") )
					.ColorAndOpacity( this, &SDMXFixturePatchFragment::GetColor )
				]
				+ SOverlay::Slot()
				[		
					SNew(SBorder)
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					.BorderImage(FEditorStyle::GetBrush("NoBorder"))
					.Padding(MinimalTextMargin)
					.BorderBackgroundColor( this, &SDMXFixturePatchFragment::GetColor )
					.OnMouseButtonDown(this, &SDMXFixturePatchFragment::OnMouseButtonDown)
					[	
						SNew(STextBlock)
						.Text(this, &SDMXFixturePatchFragment::GetText)
						.TextStyle(FEditorStyle::Get(), "SmallText")
						.ColorAndOpacity(FLinearColor::White)						
					]
				]
				+SOverlay::Slot()
				.VAlign(VAlign_Top)
				[
					SNew(SBorder)
					.Visibility(EVisibility::HitTestInvisible)			
					.BorderImage( FEditorStyle::GetBrush( "Graph.Node.TitleHighlight" ) )
					.BorderBackgroundColor( FLinearColor::White )
					[
						SNew(SSpacer)
						.Size(FVector2D(20,20))
					]
				]
			]
		];

}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SDMXFixturePatchFragment::SetHighlight(bool bEnabled)
{
	bHighlight = bEnabled;
}

int32 SDMXFixturePatchFragment::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	// Draw a shadow	
	const FSlateBrush* ShadowBrush = GetShadowBrush(bHighlight);
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToInflatedPaintGeometry(ShadowSize),
		ShadowBrush
	);

	return LayerId;
}

FText SDMXFixturePatchFragment::GetText() const
{
	check(PatchNode.IsValid());

	FText Text;
	if (UDMXEntityFixturePatch* Patch = PatchNode->GetFixturePatch().Get())
	{
		Text = FText::FromString(Patch->GetDisplayName());
	}

	return Text;
}

FText SDMXFixturePatchFragment::GetToolTipText() const
{		
	// Useful when the fragment cannot show the whole 
	// name of the patch on top of it.
	return GetText();
}

FSlateColor SDMXFixturePatchFragment::GetColor() const
{
	check(PatchNode.IsValid());

	if (PatchNode->GetFixturePatch().IsValid())
	{
		return PatchNode->GetFixturePatch()->EditorColor;
	}

	return FLinearColor::White;
}

const FSlateBrush* SDMXFixturePatchFragment::GetShadowBrush(bool bSelected) const
{
	return bSelected ? FEditorStyle::GetBrush(TEXT("Graph.VarNode.ShadowSelected")) : FEditorStyle::GetBrush(TEXT("Graph.VarNode.Shadow"));
}

FReply SDMXFixturePatchFragment::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	check(PatchNode.IsValid());

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		check(PatchNode.IsValid());
		UDMXEntityFixturePatch* Patch = PatchNode->GetFixturePatch().Get();

		if (Patch)
		{
			OnSelected.ExecuteIfBound(SharedThis(this));
			return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
		}
	}
	return FReply::Unhandled();
}

FReply SDMXFixturePatchFragment::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UDMXLibrary* DMXLibrary = GetDMXLibrary();

	if (DMXLibrary)
	{
		check(PatchNode.IsValid());
		UDMXEntityFixturePatch* Patch = PatchNode->GetFixturePatch().Get();

		if (Patch)
		{
			OnSelected.ExecuteIfBound(SharedThis(this));

			PatchNode->SetVisiblity(EVisibility::HitTestInvisible);

			TSharedRef<FDMXEntityDragDropOperation> DragDropOp = MakeShared<FDMXEntityDragDropOperation>(DMXLibrary, TArray<TWeakObjectPtr<UDMXEntity>>{ Patch });

			return FReply::Handled().BeginDragDrop(DragDropOp);
		}
	}

	return FReply::Unhandled();
}

UDMXLibrary* SDMXFixturePatchFragment::GetDMXLibrary() const
{
	if (TSharedPtr<FDMXEditor> DMXEditor = DMXEditorPtr.Pin())
	{
		return DMXEditor->GetDMXLibrary();
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
