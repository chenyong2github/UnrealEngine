// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Images/SImage.h"
#include "Rendering/DrawElements.h"
#include "Widgets/IToolTip.h"
#if WITH_ACCESSIBILITY
#include "Widgets/Accessibility/SlateCoreAccessibleWidgets.h"
#endif

void SImage::Construct( const FArguments& InArgs )
{
	Image = FInvalidatableBrushAttribute(InArgs._Image);
	ColorAndOpacity = InArgs._ColorAndOpacity;
	bFlipForRightToLeftFlowDirection = InArgs._FlipForRightToLeftFlowDirection;
	SetOnMouseButtonDown(InArgs._OnMouseButtonDown);
}

int32 SImage::OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
	const FSlateBrush* ImageBrush = Image.GetImage().Get();

	if ((ImageBrush != nullptr) && (ImageBrush->DrawAs != ESlateBrushDrawType::NoDrawType))
	{
		const bool bIsEnabled = ShouldBeEnabled(bParentEnabled);
		const ESlateDrawEffect DrawEffects = bIsEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

		const FLinearColor FinalColorAndOpacity( InWidgetStyle.GetColorAndOpacityTint() * ColorAndOpacity.Get().GetColor(InWidgetStyle) * ImageBrush->GetTint( InWidgetStyle ) );

		if (bFlipForRightToLeftFlowDirection && GSlateFlowDirection == EFlowDirection::RightToLeft)
		{
			const FGeometry FlippedGeometry = AllottedGeometry.MakeChild(FSlateRenderTransform(FScale2D(-1, 1)));
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, FlippedGeometry.ToPaintGeometry(), ImageBrush, DrawEffects, FinalColorAndOpacity);
		}
		else
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), ImageBrush, DrawEffects, FinalColorAndOpacity);
		}
	}

	return LayerId;
}

FVector2D SImage::ComputeDesiredSize( float ) const
{
	const FSlateBrush* ImageBrush = Image.Get();
	if (ImageBrush != nullptr)
	{
		return ImageBrush->ImageSize;
	}
	return FVector2D::ZeroVector;
}

void SImage::SetColorAndOpacity( const TAttribute<FSlateColor>& InColorAndOpacity )
{
	SetAttribute(ColorAndOpacity, InColorAndOpacity, EInvalidateWidgetReason::Paint);
}

void SImage::SetColorAndOpacity( FLinearColor InColorAndOpacity )
{
	SetColorAndOpacity(TAttribute<FSlateColor>(InColorAndOpacity));
}

void SImage::SetImage(TAttribute<const FSlateBrush*> InImage)
{
	Image.SetImage(*this, InImage);
}

#if WITH_ACCESSIBILITY
TSharedRef<FSlateAccessibleWidget> SImage::CreateAccessibleWidget()
{
	return MakeShareable<FSlateAccessibleWidget>(new FSlateAccessibleImage(SharedThis(this)));
}
#endif
