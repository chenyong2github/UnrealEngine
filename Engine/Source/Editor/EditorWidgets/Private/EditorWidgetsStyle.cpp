// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorWidgetsStyle.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"

TSharedPtr<FSlateStyleSet> FEditorWidgetsStyle::StyleSet = nullptr;

#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( StyleSet->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush( StyleSet->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BORDER_BRUSH( RelativePath, ... ) FSlateBorderBrush( StyleSet->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define DEFAULT_FONT(...) FCoreStyle::GetDefaultFontStyle(__VA_ARGS__)

void FEditorWidgetsStyle::Initialize()
{
	// Only register once
	if(StyleSet.IsValid())
	{
		return;
	}

	StyleSet = MakeShareable(new FSlateStyleSet("EditorWidgets"));

	const FEditableTextBoxStyle& NormalEditableTextBoxStyle = FAppStyle::GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");

	const FTextBlockStyle NormalText = FTextBlockStyle(FAppStyle::GetWidgetStyle<FTextBlockStyle>("TextEditor.NormalText"))
		.SetColorAndOpacity(NormalEditableTextBoxStyle.ForegroundColor)
		.SetHighlightColor(NormalEditableTextBoxStyle.FocusedForegroundColor)
		.SetFont(NormalEditableTextBoxStyle.Font)
		.SetFontSize(NormalEditableTextBoxStyle.Font.Size);

	// Text editor
	{
		StyleSet->Set("TextEditor.NormalText", NormalText);

		StyleSet->Set("SyntaxHighlight.Template.Normal", NormalText);
		StyleSet->Set("SyntaxHighlight.Template.Argument", FAppStyle::GetWidgetStyle<FTextBlockStyle>("RichTextBlock.BoldHighlight"));
	}

	FSlateStyleRegistry::RegisterSlateStyle( *StyleSet.Get() );
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH
#undef DEFAULT_FONT

void FEditorWidgetsStyle::Shutdown()
{
	if(StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		ensure(StyleSet.IsUnique());
		StyleSet.Reset();
	}
}

const ISlateStyle& FEditorWidgetsStyle::Get()
{
	return *(StyleSet.Get());
}

const FName& FEditorWidgetsStyle::GetStyleSetName()
{
	return StyleSet->GetStyleSetName();
}
