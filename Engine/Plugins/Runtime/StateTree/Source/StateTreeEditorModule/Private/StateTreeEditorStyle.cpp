// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditorStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "EditorStyleSet.h"
#include "Interfaces/IPluginManager.h"

#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( StyleSet->RootToContentDir( RelativePath, TEXT( ".png" ) ), __VA_ARGS__ )
#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush( FMeshEditorStyle::InContent( RelativePath, ".png" ), __VA_ARGS__ )
#define BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush( StyleSet->RootToContentDir( RelativePath, TEXT( ".png" ) ), __VA_ARGS__ )
#define TTF_CORE_FONT( RelativePath, ... ) FSlateFontInfo( StyleSet->RootToCoreContentDir( RelativePath, TEXT( ".ttf" ) ), __VA_ARGS__ )

#define DEFAULT_FONT(...) FCoreStyle::GetDefaultFontStyle(__VA_ARGS__)

TSharedPtr<FSlateStyleSet> FStateTreeEditorStyle::StyleSet = nullptr;

FString FStateTreeEditorStyle::InContent(const FString& RelativePath, const ANSICHAR* Extension)
{
	static FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("StateTreeEditorModule"))->GetContentDir() / TEXT("Slate");
	return (ContentDir / RelativePath) + Extension;
}

void FStateTreeEditorStyle::Initialize()
{
	if (StyleSet.IsValid())
	{
		return;
	}

	const FVector2D Icon8x8(8.0f, 8.0f);

	StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());

	StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
	StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	const FScrollBarStyle ScrollBar = FEditorStyle::GetWidgetStyle<FScrollBarStyle>("ScrollBar");
	const FTextBlockStyle& NormalText = FEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");

	// State
	{
		FTextBlockStyle StateIcon = FTextBlockStyle(NormalText)
			.SetFont(FEditorStyle::Get().GetFontStyle("FontAwesome.12"))
			.SetColorAndOpacity(FLinearColor(230.0f / 255.0f, 230.0f / 255.0f, 230.0f / 255.0f, 0.5f));
		StyleSet->Set("StateTree.Icon", StateIcon);

		FTextBlockStyle StateDetailsIcon = FTextBlockStyle(NormalText)
			.SetFont(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
		    .SetColorAndOpacity(FLinearColor(230.0f / 255.0f, 230.0f / 255.0f, 230.0f / 255.0f, 0.5f));
		StyleSet->Set("StateTree.DetailsIcon", StateDetailsIcon);

		FTextBlockStyle StateTitle = FTextBlockStyle(NormalText)
			.SetFont(TTF_CORE_FONT("Fonts/Roboto-Bold", 12))
			.SetColorAndOpacity(FLinearColor(230.0f / 255.0f, 230.0f / 255.0f, 230.0f / 255.0f, 0.9f));
		StyleSet->Set("StateTree.State.Title", StateTitle);

		FEditableTextBoxStyle StateTitleEditableText = FEditableTextBoxStyle()
			.SetFont(TTF_CORE_FONT("Fonts/Roboto-Bold", 10))
			.SetBackgroundImageNormal(BOX_BRUSH("Common/TextBox", FMargin(4.0f / 16.0f)))
			.SetBackgroundImageHovered(BOX_BRUSH("Common/TextBox_Hovered", FMargin(4.0f / 16.0f)))
			.SetBackgroundImageFocused(BOX_BRUSH("Common/TextBox_Hovered", FMargin(4.0f / 16.0f)))
			.SetBackgroundImageReadOnly(BOX_BRUSH("Common/TextBox_ReadOnly", FMargin(4.0f / 16.0f)))
			.SetScrollBarStyle(ScrollBar);
		StyleSet->Set("StateTree.State.TitleEditableText", StateTitleEditableText);

		StyleSet->Set("StateTree.State.TitleInlineEditableText", FInlineEditableTextBlockStyle()
			.SetTextStyle(StateTitle)
			.SetEditableTextBoxStyle(StateTitleEditableText));
	}

	// Details
	{
		FTextBlockStyle StateTitle = FTextBlockStyle(NormalText)
			.SetFont(TTF_CORE_FONT("Fonts/Roboto-Regular", 10))
			.SetColorAndOpacity(FLinearColor(230.0f / 255.0f, 230.0f / 255.0f, 230.0f / 255.0f, 0.75f));
		StyleSet->Set("StateTree.Details", StateTitle);
	}

	// Details rich text
	{
		StyleSet->Set("Details.Normal", FTextBlockStyle(NormalText)
			.SetFont(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont"))));

		StyleSet->Set("Details.Bold", FTextBlockStyle(NormalText)
			.SetFont(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont"))));

		StyleSet->Set("Details.Subdued", FTextBlockStyle(NormalText)
			.SetColorAndOpacity(FSlateColor::UseSubduedForeground())
			.SetFont(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont"))));
	}

	const FLinearColor SelectionColor = FColor(0, 0, 0, 32);
	const FTableRowStyle& NormalTableRowStyle = FEditorStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");
	StyleSet->Set("StateTree.Selection",
		FTableRowStyle(NormalTableRowStyle)
		.SetActiveBrush(IMAGE_BRUSH("Common/Selection", Icon8x8, SelectionColor))
		.SetActiveHoveredBrush(IMAGE_BRUSH("Common/Selection", Icon8x8, SelectionColor))
		.SetInactiveBrush(IMAGE_BRUSH("Common/Selection", Icon8x8, SelectionColor))
		.SetInactiveHoveredBrush(IMAGE_BRUSH("Common/Selection", Icon8x8, SelectionColor))
		.SetSelectorFocusedBrush(IMAGE_BRUSH("Common/Selection", Icon8x8, SelectionColor))
	);

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}


void FStateTreeEditorStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		ensure(StyleSet.IsUnique());
		StyleSet.Reset();
	}
}


FName FStateTreeEditorStyle::GetStyleSetName()
{
	static FName StyleName("StateTreeEditorStyle");
	return StyleName;
}
