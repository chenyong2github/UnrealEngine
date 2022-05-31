// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "EditorWidgetsStyle.h"
#include "Framework/Text/SyntaxTokenizer.h"
#include "Framework/Text/SyntaxHighlighterTextLayoutMarshaller.h"

class FTextLayout;

/**
 * Get/set the raw text to/from a text layout, and also inject syntax highlighting for our rich-text markup
 */
class FTemplateStringSyntaxHighlighterMarshaller : public FSyntaxHighlighterTextLayoutMarshaller
{
public:
	struct FSyntaxTextStyle
	{
		FSyntaxTextStyle()
			: NormalTextStyle(FEditorWidgetsStyle::Get().GetWidgetStyle<FTextBlockStyle>("SyntaxHighlight.Template.Normal"))
			, ArgumentTextStyle(FEditorWidgetsStyle::Get().GetWidgetStyle<FTextBlockStyle>("SyntaxHighlight.Template.Argument"))
		{
		}

		FSyntaxTextStyle(const FTextBlockStyle& InNormalTextStyle, const FTextBlockStyle& InArgumentTextStyle)
			: NormalTextStyle(InNormalTextStyle)
			, ArgumentTextStyle(InArgumentTextStyle)
		{
		}

		FTextBlockStyle NormalTextStyle;
		FTextBlockStyle ArgumentTextStyle;
	};

	static TSharedRef<FTemplateStringSyntaxHighlighterMarshaller> Create(const FSyntaxTextStyle& InSyntaxTextStyle);

	virtual ~FTemplateStringSyntaxHighlighterMarshaller() override = default;

protected:
	// Allows MakeShared with private constructor
	friend class SharedPointerInternals::TIntrusiveReferenceController<FTemplateStringSyntaxHighlighterMarshaller, ESPMode::ThreadSafe>;
	
	virtual void ParseTokens(const FString& SourceString, FTextLayout& TargetTextLayout, TArray<ISyntaxTokenizer::FTokenizedLine> TokenizedLines) override;

	FTemplateStringSyntaxHighlighterMarshaller(TSharedPtr<ISyntaxTokenizer> InTokenizer, const FSyntaxTextStyle& InSyntaxTextStyle);

	/** Styles used to display the text */
	FSyntaxTextStyle SyntaxTextStyle;
};
