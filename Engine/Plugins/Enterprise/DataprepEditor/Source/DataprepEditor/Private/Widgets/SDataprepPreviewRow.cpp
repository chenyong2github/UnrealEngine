// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDataprepPreviewRow.h"

#include "DataprepEditorStyle.h"
#include "PreviewSystem/DataprepPreviewSystem.h"

#include "EditorFontGlyphs.h"
#include "EditorStyleSet.h"
#include "Internationalization/Text.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SDataprepPreviewRow::Construct(const FArguments& InArgs, const TSharedPtr<FDataprepPreviewProcessingResult>& InPreviewData)
{
	PreviewData = InPreviewData;

	ChildSlot
	[
		SNew( SHorizontalBox )
		+ SHorizontalBox::Slot()
		.VAlign( VAlign_Center )
		.HAlign( HAlign_Left )
		.AutoWidth()
		[
			SNew( SBox )
			.WidthOverride( 18.f )
			[
				SNew( STextBlock )
				.Font( FEditorStyle::Get().GetFontStyle("FontAwesome.11") )
				.ColorAndOpacity( FDataprepEditorStyle::GetColor("Graph.ActionStepNode.PreviewColor"))
				.Text( this, &SDataprepPreviewRow::GetIcon )
			]
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		[
			SNew( STextBlock )
			.Text( this, &SDataprepPreviewRow::GetLabel )
			.HighlightText( InArgs._HighlightText )
		]
	];
}

FText SDataprepPreviewRow::GetIcon() const
{
	if ( FDataprepPreviewProcessingResult* Result = PreviewData.Get() )
	{
		switch ( Result->Status )
		{
		case EDataprepPreviewStatus::BeingProcessed:
			return FEditorFontGlyphs::Refresh;
			break;
		case EDataprepPreviewStatus::Pass:
			return FEditorFontGlyphs::Check;
			break;
		case EDataprepPreviewStatus::Failed:
			return FText::GetEmpty();
			break;
		case EDataprepPreviewStatus::NotSupported :
			return FEditorFontGlyphs::Question;
			break;
		default:
			break;
		}
	}

	return FEditorFontGlyphs::Bug;
}

FText SDataprepPreviewRow::GetLabel() const
{
	if ( FDataprepPreviewProcessingResult* Result = PreviewData.Get() )
	{
		 return Result->GetFetchedDataAsText();
	}

	return {};
}

