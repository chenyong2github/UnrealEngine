// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "OptimusEditorStyle.h"

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Framework/Text/SyntaxTokenizer.h"
#include "Framework/Text/SyntaxHighlighterTextLayoutMarshaller.h"

// TEMP
#include "Types/OptimusType_ShaderText.h"

class FTextLayout;

/**
 * Get/set the raw text to/from a text layout, and also inject syntax highlighting for our rich-text markup
 */
class FOptimusHLSLSyntaxHighlighter : public FSyntaxHighlighterTextLayoutMarshaller
{
public:

	struct FSyntaxTextStyle
	{
		FSyntaxTextStyle() :
			NormalTextStyle(FOptimusEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("SyntaxHighlight.HLSL.Normal")),
			OperatorTextStyle(FOptimusEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("SyntaxHighlight.HLSL.Operator")),
			KeywordTextStyle(FOptimusEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("SyntaxHighlight.HLSL.Keyword")),
			StringTextStyle(FOptimusEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("SyntaxHighlight.HLSL.String")),
			NumberTextStyle(FOptimusEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("SyntaxHighlight.HLSL.Number")),
			CommentTextStyle(FOptimusEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("SyntaxHighlight.HLSL.Comment")),
			PreProcessorKeywordTextStyle(FOptimusEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("SyntaxHighlight.HLSL.PreProcessorKeyword")),
			ErrorTextStyle(FOptimusEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("SyntaxHighlight.HLSL.Error"))
		{
		}

		FSyntaxTextStyle(
			const FTextBlockStyle& InNormalTextStyle,
			const FTextBlockStyle& InOperatorTextStyle,
			const FTextBlockStyle& InKeywordTextStyle,
			const FTextBlockStyle& InStringTextStyle,
			const FTextBlockStyle& InNumberTextStyle,
			const FTextBlockStyle& InCommentTextStyle,
			const FTextBlockStyle& InPreProcessorKeywordTextStyle,
			const FTextBlockStyle& InErrorTextStyle
			) :
			NormalTextStyle(InNormalTextStyle),
			OperatorTextStyle(InOperatorTextStyle),
			KeywordTextStyle(InKeywordTextStyle),
			StringTextStyle(InStringTextStyle),
			NumberTextStyle(InNumberTextStyle),
			CommentTextStyle(InCommentTextStyle),
			PreProcessorKeywordTextStyle(InPreProcessorKeywordTextStyle),
			ErrorTextStyle(InErrorTextStyle)
		{
		}

		FTextBlockStyle NormalTextStyle;
		FTextBlockStyle OperatorTextStyle;
		FTextBlockStyle KeywordTextStyle;
		FTextBlockStyle StringTextStyle;
		FTextBlockStyle NumberTextStyle;
		FTextBlockStyle CommentTextStyle;
		FTextBlockStyle PreProcessorKeywordTextStyle;
		FTextBlockStyle ErrorTextStyle;
	};

	static TSharedRef< FOptimusHLSLSyntaxHighlighter > Create(const FSyntaxTextStyle& InSyntaxTextStyle);

	void SetErrorLocations(const TArray<FOptimusSourceLocation> &InErrorLocations);


	virtual ~FOptimusHLSLSyntaxHighlighter();

protected:

	virtual void ParseTokens(const FString& SourceString, FTextLayout& TargetTextLayout, TArray<FSyntaxTokenizer::FTokenizedLine> TokenizedLines) override;

	FOptimusHLSLSyntaxHighlighter(TSharedPtr< FSyntaxTokenizer > InTokenizer, const FSyntaxTextStyle& InSyntaxTextStyle);

	/** Styles used to display the text */
	FSyntaxTextStyle SyntaxTextStyle;

	/** String representing tabs */
	FString TabString;

	TMultiMap<int32 /*Line*/, FOptimusSourceLocation> ErrorLocations;
};
