// Copyright Epic Games, Inc. All Rights Reserved.

#include "CurveExpressionDetailsCustomization.h"

#include "CurveExpressionEditorStyle.h"
#include "K2Node_MakeCurveExpressionMap.h"
#include "DetailWidgetRow.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/SMultiLineEditableText.h"


TSharedRef<IPropertyTypeCustomization> FCurveExpressionListCustomization::MakeInstance()
{
	return MakeShared<FCurveExpressionListCustomization>();
}


void FCurveExpressionListCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> InPropertyHandle,
	FDetailWidgetRow& InHeaderRow,
	IPropertyTypeCustomizationUtils& InCustomizationUtils
	)
{
	AssignmentExpressionsProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCurveExpressionList, AssignmentExpressions));

	HorizontalScrollbar =
		SNew(SScrollBar)
			.AlwaysShowScrollbar(true)
			.Orientation(Orient_Horizontal);

	VerticalScrollbar =
		SNew(SScrollBar)
			.AlwaysShowScrollbar(true)
			.Orientation(Orient_Vertical);

	const FTextBlockStyle &TextStyle = FCurveExpressionEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("TextEditor.NormalText");
	const FSlateFontInfo &Font = TextStyle.Font;
	
	InHeaderRow
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.VAlign(VAlign_Top)
		.AutoHeight()
		[
			InPropertyHandle->CreatePropertyNameWidget()
		]
		+ SVerticalBox::Slot()
		.MaxHeight(400.0f)
		[
			SNew(SBorder)
			.BorderImage(FCurveExpressionEditorStyle::Get().GetBrush("TextEditor.Border"))
			.BorderBackgroundColor(FLinearColor::Black)
			[
				SNew(SGridPanel)
				.FillColumn(0, 1.0f)
				.FillRow(0, 1.0f)
				+SGridPanel::Slot(0, 0)
				[
					SNew(SScrollBox)
					.Orientation(Orient_Vertical)
					.ExternalScrollbar(VerticalScrollbar)
					+ SScrollBox::Slot()
					[
						SAssignNew(TextEditor, SMultiLineEditableText)
						.Font(Font)
						.TextStyle(&TextStyle)
						.Text_Lambda([this]()
						{
							FString Text;
							AssignmentExpressionsProperty->GetValue(Text);
							return FText::FromString(Text);
						})
						.OnTextChanged_Lambda([this](const FText& InText)
						{
							AssignmentExpressionsProperty->SetValue(InText.ToString());
						})
						// By default, the Tab key gets routed to "next widget". We want to disable that behaviour.
						.OnIsTypedCharValid_Lambda([](const TCHAR InChar) { return true; })
						.AutoWrapText(false)
						.HScrollBar(HorizontalScrollbar)
						.VScrollBar(VerticalScrollbar)
					]
				]
				+SGridPanel::Slot(1, 0)
				[
					VerticalScrollbar.ToSharedRef()
				]
				+SGridPanel::Slot(0, 1)
				[
					HorizontalScrollbar.ToSharedRef()
				]
			]
		]
	];
}
