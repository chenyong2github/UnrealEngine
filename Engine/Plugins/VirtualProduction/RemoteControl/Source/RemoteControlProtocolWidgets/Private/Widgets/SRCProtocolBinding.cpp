// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCProtocolBinding.h"

#include "SDropTarget.h"
#include "SRCProtocolRangeList.h"
#include "ViewModels/ProtocolBindingViewModel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SRCProtocolStruct.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RemoteControlProtocolWidgets"

void SRCProtocolBinding::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedRef<FProtocolBindingViewModel>& InViewModel)
{
	constexpr float Padding = 2.0f;
	ViewModel = InViewModel;
	PrimaryColumnSizeData = InArgs._PrimaryColumnSizeData;
	SecondaryColumnSizeData = InArgs._SecondaryColumnSizeData;
	
	TSharedRef<SHorizontalBox> RowBox = SNew(SHorizontalBox);

	RowBox->AddSlot()
	.Padding(Padding)
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Text(ViewModel->GetProtocolName())
	    .TextStyle(FEditorStyle::Get(), "LargeText")
	];

	RowBox->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(0)
	[
		SNew(SButton)
		.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
		.ForegroundColor(FSlateColor::UseForeground())
		.IsFocusable(false)
		.OnClicked(this, &SRCProtocolBinding::OnDelete)
		.Content()
		[
			SNew(STextBlock)
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
			.Text(FText::FromString(FString(TEXT("\xf00d"))))
		]
	];
	
	STableRow::Construct(
		STableRow::FArguments()
		.Padding(Padding)
		.ShowSelection(false)
		.Content()
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(Padding)
			.VAlign(VAlign_Fill)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.Padding(Padding)
				.VAlign(VAlign_Fill)
				.AutoHeight()
				[
					RowBox
				]

				+ SVerticalBox::Slot()
				.Padding(Padding)
				.VAlign(VAlign_Fill)
                .AutoHeight()
				[
					SNew(SRCProtocolStruct, ViewModel.ToSharedRef())
				]
				
				+ SVerticalBox::Slot()
				.Padding(Padding)
				.VAlign(VAlign_Fill)
                .AutoHeight()
				[
					SNew( SHorizontalBox )
	                + SHorizontalBox::Slot()
	                [
						SAssignNew(RangeList, SRCProtocolRangeList, ViewModel.ToSharedRef())
						.PrimaryColumnSizeData(PrimaryColumnSizeData)
						.SecondaryColumnSizeData(SecondaryColumnSizeData)
	                ]
	                + SHorizontalBox::Slot()
	                .AutoWidth()
	                [
	                    SNew( SBox )
	                    .WidthOverride( 16.0f )
	                    [
	                    	SNullWidget::NullWidget
	                    ]
	                ]					
				]
			]
		],
		InOwnerTableView);
}

FReply SRCProtocolBinding::OnDelete() const
{
	ViewModel->Remove();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
