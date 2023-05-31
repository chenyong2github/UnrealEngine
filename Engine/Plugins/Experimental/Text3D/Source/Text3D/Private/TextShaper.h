// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Fonts/FontCache.h"
#include "Framework/Text/TextLayout.h"
#include "Internationalization/Text.h"
#include "Templates/UniquePtr.h"
#include "Text3DPrivate.h"

class ITextLayoutMarshaller;
class UFont;

struct FShapedGlyphLine
{
	TArray<FShapedGlyphEntry> GlyphsToRender;
	float Width = 0.0f;

	float GetAdvance(const int32 Index, const float Kerning, const float WordSpacing) const
	{
		check(Index >= 0 && Index < GlyphsToRender.Num());

		const FShapedGlyphEntry& Glyph = GlyphsToRender[Index];
		float Advance = Glyph.XOffset + Glyph.XAdvance;

		if (Index < GlyphsToRender.Num() - 1)
		{
			Advance += Glyph.Kerning + Kerning;

			if (!Glyph.bIsVisible)
			{
				Advance += WordSpacing;
			}
		}

		return Advance;
	}

	void CalculateWidth(const float Kerning, const float WordSpacing)
	{
		Width = 0.0f;
		for (int32 Index = 0; Index < GlyphsToRender.Num(); Index++)
		{
			Width += GetAdvance(Index, Kerning, WordSpacing);
		}
	}
};

class FText3DLayout : public FTextLayout
{
public:
	FText3DLayout(const FTextBlockStyle& InStyle = FTextBlockStyle::GetDefault());

protected:
	FTextBlockStyle TextStyle;
	
	virtual TSharedRef<IRun> CreateDefaultTextRun(
		const TSharedRef<FString>& NewText,
		const FTextRange& NewRange) const override;
};

class FTextShaper final
{
public:
	static TSharedPtr<FTextShaper> Get();

	void ShapeBidirectionalText(
		const FTextBlockStyle& Style,
		const FString& Text,
		const TSharedPtr<FTextLayout>& TextLayout,
		const TSharedPtr<ITextLayoutMarshaller>& TextMarshaller,
		TArray<FShapedGlyphLine>& OutShapedLines);

private:
	struct FPrivateToken { explicit FPrivateToken() = default; };

public:
	FTextShaper(FPrivateToken) { }

private:
	friend class UText3DComponent;

	FTextShaper(const FTextShaper&) = delete;
	FTextShaper& operator=(const FTextShaper&) = delete;
};
