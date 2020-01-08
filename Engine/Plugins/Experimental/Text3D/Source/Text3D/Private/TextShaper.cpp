// Copyright Epic Games, Inc. All Rights Reserved.


#include "TextShaper.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Fonts/FontCache.h"
#include "Internationalization/Text.h"

THIRD_PARTY_INCLUDES_START
#include "hb.h"
#include "hb-ft.h"
THIRD_PARTY_INCLUDES_END

FTextShaper* FTextShaper::Instance = nullptr;

void FTextShaper::Initialize()
{
	if (Instance)
		return;

	Instance = new FTextShaper();
}

void FTextShaper::Cleanup()
{
	delete Instance;
	Instance = nullptr;
}

FTextShaper::FTextShaper()
{
	TextBiDiDetection = TextBiDi::CreateTextBiDi();
}

void FTextShaper::ShapeBidirectionalText(const FT_Face Face, const FString& Text, TArray<FShapedGlyphLine>& OutShapedLines)
{
	TextBiDi::ETextDirection Direction = TextBiDiDetection->ComputeBaseDirection(Text);
	check(Direction != TextBiDi::ETextDirection::Mixed);
	check(Face);

	OutShapedLines.AddDefaulted();

	TArray<TextBiDi::FTextDirectionInfo> TextDirectionInfos;
	TextBiDiDetection->ComputeTextDirection(Text, Direction, TextDirectionInfos);
	for (const TextBiDi::FTextDirectionInfo& TextDirectionInfo : TextDirectionInfos)
	{
		if (TextDirectionInfo.Length == 0)
		{
			continue;
		}

		if (TextDirectionInfo.TextDirection == TextBiDi::ETextDirection::RightToLeft)
		{
			PerformHarfBuzzTextShaping(Face, *Text, TextDirectionInfo.StartIndex, TextDirectionInfo.StartIndex + TextDirectionInfo.Length, OutShapedLines);
		}
		else
		{
			PerformKerningTextShaping(Face, *Text, TextDirectionInfo.StartIndex, TextDirectionInfo.StartIndex + TextDirectionInfo.Length, OutShapedLines);
		}
	}

	for (FShapedGlyphLine& ShapedLine : OutShapedLines)
	{
		for (const FShapedGlyphEntry& Glyph : ShapedLine.GlyphsToRender)
		{
			ShapedLine.Width += Glyph.XOffset + Glyph.XAdvance;
		}
	}
}

void FTextShaper::PerformKerningTextShaping(const FT_Face Face, const TCHAR* Text, const int32 StartIndex, const int32 EndIndex, TArray<FShapedGlyphLine>& OutShapedLines)
{
	const bool bHasKerning = FT_HAS_KERNING(Face) != 0;
	for (int32 Index = StartIndex; Index < EndIndex; Index++)
	{
		if (InsertSubstituteGlyphs(Face, Text, Index, OutShapedLines))
		{
			continue;
		}

		const TCHAR CurrentChar = Text[Index];

		const bool bIsZeroWidthSpace = CurrentChar == TEXT('\u200B');
		const bool bIsWhitespace = bIsZeroWidthSpace || FText::IsWhitespace(CurrentChar);

		FT_Load_Char(Face, CurrentChar, FT_LOAD_DEFAULT);
		uint32 GlyphIndex = FT_Get_Char_Index(Face, CurrentChar);
		if (GlyphIndex == 0)	// Get Space instead of invalid character
		{
			GlyphIndex = FT_Get_Char_Index(Face, ' ');
		}
		

		int16 XAdvance = 0;
		if (!bIsZeroWidthSpace)
		{
			FT_Fixed AdvanceData = 0;
			if (FT_Get_Advance(Face, GlyphIndex, /*FIXME*/0, &AdvanceData) == 0)
			{
				XAdvance = ((AdvanceData + (1 << 9)) >> 10) * FontInverseScale;
			}
		}

		const int32 CurrentGlyphEntryIndex = OutShapedLines.Last().GlyphsToRender.AddDefaulted();
		FShapedGlyphEntry& ShapedGlyphEntry = OutShapedLines.Last().GlyphsToRender[CurrentGlyphEntryIndex];
		//ShapedGlyphEntry.FontFaceData = ShapedGlyphFaceData;
		ShapedGlyphEntry.GlyphIndex = GlyphIndex;
		ShapedGlyphEntry.SourceIndex = Index;
		ShapedGlyphEntry.XAdvance = bIsZeroWidthSpace ? 0 : XAdvance;
		ShapedGlyphEntry.YAdvance = 0;
		ShapedGlyphEntry.XOffset = 0;
		ShapedGlyphEntry.YOffset = 0;
		ShapedGlyphEntry.Kerning = 0;
		ShapedGlyphEntry.NumCharactersInGlyph = 1;
		ShapedGlyphEntry.NumGraphemeClustersInGlyph = 1;
		ShapedGlyphEntry.TextDirection = TextBiDi::ETextDirection::LeftToRight;
		ShapedGlyphEntry.bIsVisible = !bIsWhitespace;

		// Apply the kerning against the previous entry
		if (CurrentGlyphEntryIndex > 0 && bHasKerning && ShapedGlyphEntry.bIsVisible)
		{
			FShapedGlyphEntry& PreviousShapedGlyphEntry = OutShapedLines.Last().GlyphsToRender[CurrentGlyphEntryIndex - 1];

			FT_Vector KerningVector;
			if (FT_Get_Kerning(Face, PreviousShapedGlyphEntry.GlyphIndex, ShapedGlyphEntry.GlyphIndex, FT_KERNING_DEFAULT, &KerningVector) == 0)
			{
				//const int8 Kerning = FreeTypeUtils::Convert26Dot6ToRoundedPixel<int8>(KerningVector.x);
				const int8 Kerning = KerningVector.x * FontInverseScale;
				PreviousShapedGlyphEntry.XAdvance += Kerning;
				PreviousShapedGlyphEntry.Kerning = Kerning;
			}
		}
	}
}

void FTextShaper::PerformHarfBuzzTextShaping(const FT_Face Face, const TCHAR* Text, int32 StartIndex, int32 EndIndex, TArray<FShapedGlyphLine>& OutShapedLines)
{
	bool bHasKerning = FT_HAS_KERNING(Face) != 0;
	const hb_feature_t HarfBuzzFeatures[] = {
		{ HB_TAG('k','e','r','n'), bHasKerning, 0, uint32(-1) }
	};


	TArray<FShapedGlyphLine> LinesToRender;
	LinesToRender.AddDefaulted();
	PerformKerningTextShaping(Face, Text, StartIndex, EndIndex, LinesToRender);

	for (int32 LineIndex = 0; LineIndex < LinesToRender.Num(); LineIndex++)
	{
		if (LineIndex > 0)
		{
			OutShapedLines.AddDefaulted();
		}

		Algo::Reverse(LinesToRender[LineIndex].GlyphsToRender);
		for (FShapedGlyphEntry& GlyphToRender : LinesToRender[LineIndex].GlyphsToRender)
		{
			OutShapedLines.Last().GlyphsToRender.Add(GlyphToRender);
		}
	}

	/*
	const int32 HarfBuzzFeaturesCount = ARRAY_COUNT(HarfBuzzFeatures);

	hb_buffer_t* HarfBuzzTextBuffer = hb_buffer_create();
	for (int32 Index = StartIndex; Index < EndIndex; Index++)
	{
		hb_font_t* HarfBuzzFont = HarfBuzzFontFactory.CreateFont(*HarfBuzzTextSequenceEntry.FaceAndMemory, GlyphFlags, InFontInfo.Size, FinalFontScale);
	}

	hb_buffer_destroy(HarfBuzzTextBuffer);
	*/
}


bool FTextShaper::InsertSubstituteGlyphs(const FT_Face Face, const TCHAR* Text, const int32 Index, TArray<FShapedGlyphLine>& OutShapedLines)
{
	TCHAR Char = Text[Index];
	if (TextBiDi::IsControlCharacter(Char))
	{
		// We insert a stub entry for control characters to avoid them being drawn as a visual glyph with size
		const int32 CurrentGlyphEntryIndex = OutShapedLines.Last().GlyphsToRender.AddDefaulted();
		FShapedGlyphEntry& ShapedGlyphEntry = OutShapedLines.Last().GlyphsToRender[CurrentGlyphEntryIndex];
		//ShapedGlyphEntry.FontFaceData = InShapedGlyphFaceData;
		ShapedGlyphEntry.GlyphIndex = 0;
		ShapedGlyphEntry.SourceIndex = Index;
		ShapedGlyphEntry.XAdvance = 0;
		ShapedGlyphEntry.YAdvance = 0;
		ShapedGlyphEntry.XOffset = 0;
		ShapedGlyphEntry.YOffset = 0;
		ShapedGlyphEntry.Kerning = 0;
		ShapedGlyphEntry.NumCharactersInGlyph = 1;
		ShapedGlyphEntry.NumGraphemeClustersInGlyph = 1;
		ShapedGlyphEntry.TextDirection = TextBiDi::ETextDirection::LeftToRight;
		ShapedGlyphEntry.bIsVisible = false;
		return true;
	}

	if (Char == TEXT('\r'))
	{
		return true;
	}

	if (Char == TEXT('\n'))
	{
		OutShapedLines.AddDefaulted();
		return true;
	}

	if (Char == TEXT('\t'))
	{
		uint32 SpaceGlyphIndex = 0;
		int16 SpaceXAdvance = 0;
#if TEXT3D_WITH_FREETYPE
		{
			SpaceGlyphIndex = FT_Get_Char_Index(Face, TEXT(' '));

			FT_Fixed AdvanceData = 0;
			if (FT_Get_Advance(Face, SpaceGlyphIndex, /*FIXME*/0, &AdvanceData) == 0)
			{
				SpaceXAdvance = ((AdvanceData + (1 << 9)) >> 10) * FontInverseScale;
				//SpaceXAdvance = FreeTypeUtils::Convert26Dot6ToRoundedPixel<int16>((CachedAdvanceData + (1 << 9)) >> 10);
			}
		}
#endif // TEXT3D_WITH_FREETYPE

		// We insert a spacer glyph with (up-to) the width of 4 space glyphs in-place of a tab character
		const int32 NumSpacesToInsert = 4 - (OutShapedLines.Last().GlyphsToRender.Num() % 4);
		if (NumSpacesToInsert > 0)
		{
			const int32 CurrentGlyphEntryIndex = OutShapedLines.Last().GlyphsToRender.AddDefaulted();
			FShapedGlyphEntry& ShapedGlyphEntry = OutShapedLines.Last().GlyphsToRender[CurrentGlyphEntryIndex];
			//ShapedGlyphEntry.FontFaceData = InShapedGlyphFaceData;
			ShapedGlyphEntry.GlyphIndex = SpaceGlyphIndex;
			ShapedGlyphEntry.SourceIndex = Index;
			ShapedGlyphEntry.XAdvance = SpaceXAdvance * NumSpacesToInsert;
			ShapedGlyphEntry.YAdvance = 0;
			ShapedGlyphEntry.XOffset = 0;
			ShapedGlyphEntry.YOffset = 0;
			ShapedGlyphEntry.Kerning = 0;
			ShapedGlyphEntry.NumCharactersInGlyph = 1;
			ShapedGlyphEntry.NumGraphemeClustersInGlyph = 1;
			ShapedGlyphEntry.TextDirection = TextBiDi::ETextDirection::LeftToRight;
			ShapedGlyphEntry.bIsVisible = false;
		}

		return true;
	}

	return false;
}
