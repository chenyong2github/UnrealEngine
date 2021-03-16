// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Text/SRichTextBlock.h"

#if WITH_FANCY_TEXT

#include "Widgets/Text/SlateTextBlockLayout.h"
#include "Framework/Text/RichTextMarkupProcessing.h"
#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "Types/ReflectionMetadata.h"

SLATE_IMPLEMENT_WIDGET(SRichTextBlock)
void SRichTextBlock::PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)
{
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, BoundText, EInvalidateWidgetReason::Layout);
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, HighlightText, EInvalidateWidgetReason::Layout);
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, WrapTextAt, EInvalidateWidgetReason::Layout);
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, WrappingPolicy, EInvalidateWidgetReason::Layout);
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, TransformPolicy, EInvalidateWidgetReason::Layout);
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, Justification, EInvalidateWidgetReason::Layout);
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, AutoWrapText, EInvalidateWidgetReason::Layout);
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, Margin, EInvalidateWidgetReason::Layout);
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, LineHeightPercentage, EInvalidateWidgetReason::Layout);
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, MinDesiredWidth, EInvalidateWidgetReason::Layout);
}

SRichTextBlock::SRichTextBlock()
	: BoundText(*this)
	, HighlightText(*this)
	, WrapTextAt(*this)
	, WrappingPolicy(*this)
	, TransformPolicy(*this)
	, Justification(*this)
	, AutoWrapText(*this)
	, Margin(*this)
	, LineHeightPercentage(*this)
	, MinDesiredWidth(*this)
{
}

SRichTextBlock::~SRichTextBlock()
{
	// Needed to avoid "deletion of pointer to incomplete type 'FSlateTextBlockLayout'; no destructor called" error when using TUniquePtr
}

void SRichTextBlock::Construct( const FArguments& InArgs )
{
	SetText(InArgs._Text);
	SetHighlightText(InArgs._HighlightText);

	SetTextStyle(*InArgs._TextStyle);
	SetWrapTextAt(InArgs._WrapTextAt);
	SetAutoWrapText(InArgs._AutoWrapText);
	SetWrappingPolicy(InArgs._WrappingPolicy);
	SetTransformPolicy(InArgs._TransformPolicy);
	SetMargin(InArgs._Margin);
	SetLineHeightPercentage(InArgs._LineHeightPercentage);
	SetJustification(InArgs._Justification);
	SetMinDesiredWidth(InArgs._MinDesiredWidth);

	{
		TSharedPtr<IRichTextMarkupParser> Parser = InArgs._Parser;
		if ( !Parser.IsValid() )
		{
			Parser = FDefaultRichTextMarkupParser::GetStaticInstance();
		}

		Marshaller = InArgs._Marshaller;
		if (!Marshaller.IsValid())
		{
			Marshaller = FRichTextLayoutMarshaller::Create(Parser, nullptr, InArgs._Decorators, InArgs._DecoratorStyleSet);
		}
		
		for (const TSharedRef< ITextDecorator >& Decorator : InArgs.InlineDecorators)
		{
			Marshaller->AppendInlineDecorator(Decorator);
		}

		TextLayoutCache = MakeUnique<FSlateTextBlockLayout>(this, TextStyle, InArgs._TextShapingMethod, InArgs._TextFlowDirection, InArgs._CreateSlateTextLayout, Marshaller.ToSharedRef(), nullptr);
		TextLayoutCache->SetDebugSourceInfo(TAttribute<FString>::Create(TAttribute<FString>::FGetter::CreateLambda([this]{ return FReflectionMetaData::GetWidgetDebugInfo(this); })));
	}

	SetCanTick(false);
}

int32 SRichTextBlock::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D LastDesiredSize = TextLayoutCache->GetDesiredSize();

	const FGeometry TextBlockScaledGeometry = AllottedGeometry.MakeChild(AllottedGeometry.GetLocalSize() / TextBlockScale, FSlateLayoutTransform(TextBlockScale));

	// OnPaint will also update the text layout cache if required
	LayerId = TextLayoutCache->OnPaint(Args, TextBlockScaledGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, ShouldBeEnabled(bParentEnabled));

	const FVector2D NewDesiredSize = TextLayoutCache->GetDesiredSize();

	// HACK: Due to the nature of wrapping and layout, we may have been arranged in a different box than what we were cached with.  Which
	// might update wrapping, so make sure we always set the desired size to the current size of the text layout, which may have changed
	// during paint.
	bool bCanWrap = WrapTextAt.Get() > 0.f || AutoWrapText.Get();

	if (bCanWrap && !NewDesiredSize.Equals(LastDesiredSize))
	{
		const_cast<SRichTextBlock*>(this)->Invalidate(EInvalidateWidget::Layout);
	}

	return LayerId;
}

FVector2D SRichTextBlock::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	// ComputeDesiredSize will also update the text layout cache if required
	const FVector2D TextSize = TextLayoutCache->ComputeDesiredSize(
		FSlateTextBlockLayout::FWidgetDesiredSizeArgs(
			BoundText.Get(),
			HighlightText.Get(),
			WrapTextAt.Get(),
			AutoWrapText.Get(),
			WrappingPolicy.Get(),
			TransformPolicy.Get(),
			Margin.Get(),
			LineHeightPercentage.Get(),
			Justification.Get()),
		LayoutScaleMultiplier * TextBlockScale, TextStyle) * TextBlockScale;

	return FVector2D(FMath::Max(TextSize.X, MinDesiredWidth.Get()), TextSize.Y);
}

FChildren* SRichTextBlock::GetChildren()
{
	return TextLayoutCache->GetChildren();
}

void SRichTextBlock::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const FGeometry TextBlockScaledGeometry = AllottedGeometry.MakeChild(AllottedGeometry.GetLocalSize() / TextBlockScale, FSlateLayoutTransform(TextBlockScale));

	TextLayoutCache->ArrangeChildren(TextBlockScaledGeometry, ArrangedChildren);
}

void SRichTextBlock::SetText(TAttribute<FText> InTextAttr)
{
	BoundText.Assign(*this, MoveTemp(InTextAttr), FText::GetEmpty());
	InvalidatePrepass();
}

void SRichTextBlock::SetHighlightText(TAttribute<FText> InHighlightText)
{
	HighlightText.Assign(*this, MoveTemp(InHighlightText), FText::GetEmpty());
}

void SRichTextBlock::SetTextShapingMethod(const TOptional<ETextShapingMethod>& InTextShapingMethod)
{
	TextLayoutCache->SetTextShapingMethod(InTextShapingMethod);
	Invalidate(EInvalidateWidget::Layout);
}

void SRichTextBlock::SetTextFlowDirection(const TOptional<ETextFlowDirection>& InTextFlowDirection)
{
	TextLayoutCache->SetTextFlowDirection(InTextFlowDirection);
	Invalidate(EInvalidateWidget::Layout);
}

void SRichTextBlock::SetWrapTextAt(TAttribute<float> InWrapTextAt)
{
	WrapTextAt.Assign(*this, MoveTemp(InWrapTextAt), 0.f);
}

void SRichTextBlock::SetAutoWrapText(TAttribute<bool> InAutoWrapText)
{
	AutoWrapText.Assign(*this, MoveTemp(InAutoWrapText), false);
	InvalidatePrepass();
}

void SRichTextBlock::SetWrappingPolicy(TAttribute<ETextWrappingPolicy> InWrappingPolicy)
{
	WrappingPolicy.Assign(*this, MoveTemp(InWrappingPolicy));
}

void SRichTextBlock::SetTransformPolicy(TAttribute<ETextTransformPolicy> InTransformPolicy)
{
	TransformPolicy.Assign(*this, MoveTemp(InTransformPolicy));
}

void SRichTextBlock::SetLineHeightPercentage(TAttribute<float> InLineHeightPercentage)
{
	LineHeightPercentage.Assign(*this, MoveTemp(InLineHeightPercentage));
}

void SRichTextBlock::SetMargin(TAttribute<FMargin> InMargin)
{
	Margin.Assign(*this, MoveTemp(InMargin));
}

void SRichTextBlock::SetJustification(TAttribute<ETextJustify::Type> InJustification)
{
	Justification.Assign(*this, MoveTemp(InJustification));
}

void SRichTextBlock::SetTextStyle(const FTextBlockStyle& InTextStyle)
{
	TextStyle = InTextStyle;
	Invalidate(EInvalidateWidget::Layout);
}

void SRichTextBlock::SetMinDesiredWidth(TAttribute<float> InMinDesiredWidth)
{
	MinDesiredWidth.Assign(*this, MoveTemp(InMinDesiredWidth));
}

void SRichTextBlock::SetDecoratorStyleSet(const ISlateStyle* NewDecoratorStyleSet)
{
	if (Marshaller.IsValid())
	{
		Marshaller->SetDecoratorStyleSet(NewDecoratorStyleSet);
		Refresh();
	}
}

void SRichTextBlock::SetTextBlockScale(const float NewTextBlockScale)
{
	TextBlockScale = NewTextBlockScale;
	Invalidate(EInvalidateWidget::Layout);
	InvalidatePrepass();
}

void SRichTextBlock::Refresh()
{
	TextLayoutCache->DirtyContent();
	Invalidate(EInvalidateWidget::Layout);
}

#endif //WITH_FANCY_TEXT
