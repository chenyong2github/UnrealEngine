// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Images/SLayeredImage.h"

void SLayeredImage::Construct(const FArguments& InArgs, const TArray<ImageLayer>& InLayers)
{
	SImage::Construct(InArgs);

	Layers = InLayers;
}

void SLayeredImage::Construct(const SLayeredImage::FArguments& InArgs, TArray<ImageLayer>&& InLayers)
{
	SImage::Construct(InArgs);

	Layers = MoveTemp(InLayers);
}

void SLayeredImage::Construct(const FArguments& InArgs, TAttribute<const FSlateBrush*> Brush, TAttribute<FSlateColor> Color)
{
	SImage::Construct(InArgs);

	AddLayer(MoveTemp(Brush), MoveTemp(Color));
}

void SLayeredImage::Construct(const FArguments& InArgs, int32 NumLayers)
{
	SImage::Construct(InArgs);

	if (NumLayers > 0)
	{
		Layers.AddDefaulted(NumLayers);

		// replace the default fuschia color with white
		for (ImageLayer& Layer : Layers)
		{
			Layer.Value.Set(FLinearColor::White);
		}
	}
}

int32 SLayeredImage::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// this will draw Image[0]:
	SImage::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	const bool bIsEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bIsEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	
	// draw rest of the images, we reuse the LayerId because images are assumed to note overlap:
	for (const ImageLayer& Layer : Layers)
	{
		const FSlateBrush* LayerImageResolved = Layer.Key.Get();
		if (LayerImageResolved && LayerImageResolved->DrawAs != ESlateBrushDrawType::NoDrawType)
		{
			const FLinearColor FinalColorAndOpacity(InWidgetStyle.GetColorAndOpacityTint() * Layer.Value.Get().GetColor(InWidgetStyle) * LayerImageResolved->GetTint(InWidgetStyle));
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), LayerImageResolved, DrawEffects, FinalColorAndOpacity);
		}
	}

	return LayerId;
}

void SLayeredImage::AddLayer(TAttribute<const FSlateBrush*> Brush, TAttribute<FSlateColor> Color)
{
	Layers.Emplace(MoveTemp(Brush), MoveTemp(Color));
}

int32 SLayeredImage::GetNumLayers() const
{
	return Layers.Num() + 1;
}

bool SLayeredImage::IsValidIndex(int32 Index) const
{
	// Index 0 is our local SImage
	return Index == 0 || Layers.IsValidIndex(Index - 1);
}

const FSlateBrush* SLayeredImage::GetLayerBrush(int32 Index) const
{
	if (Index == 0)
	{
		return Image.Get();
	}
	else if (Layers.IsValidIndex(Index - 1))
	{
		return Layers[Index - 1].Key.Get();
	}
	else
	{
		return nullptr;
	}
}

void SLayeredImage::SetLayerBrush(int32 Index, const TAttribute<const FSlateBrush*>& Brush)
{
	if (Index == 0)
	{
		Image = Brush;
	}
	else if (Layers.IsValidIndex(Index - 1))
	{
		Layers[Index - 1].Key = Brush;
	}
	else
	{
		// That layer doesn't exist
	}
}

void SLayeredImage::SetLayerBrush(int32 Index, TAttribute<const FSlateBrush*>&& Brush)
{
	if (Index == 0)
	{
		Image = MoveTemp(Brush);
	}
	else if (Layers.IsValidIndex(Index - 1))
	{
		Layers[Index - 1].Key = MoveTemp(Brush);
	}
	else
	{
		// That layer doesn't exist
	}
}

FSlateColor SLayeredImage::GetLayerColor(int32 Index) const
{
	if (Index == 0)
	{
		return ColorAndOpacity.Get();
	}
	else if (Layers.IsValidIndex(Index - 1))
	{
		return Layers[Index - 1].Value.Get();
	}
	else
	{
		return FSlateColor();
	}
}

void SLayeredImage::SetLayerColor(int32 Index, const TAttribute<FSlateColor>& Color)
{
	if (Index == 0)
	{
		ColorAndOpacity = Color;
	}
	else if (Layers.IsValidIndex(Index - 1))
	{
		Layers[Index - 1].Value = Color;
	}
	else
	{
		// That layer doesn't exist!
	}
}

void SLayeredImage::SetLayerColor(int32 Index, TAttribute<FSlateColor>&& Color)
{
	if (Index == 0)
	{
		ColorAndOpacity = MoveTemp(Color);
	}
	else if (Layers.IsValidIndex(Index - 1))
	{
		Layers[Index - 1].Value = MoveTemp(Color);
	}
	else
	{
		// That layer doesn't exist!
	}
}
