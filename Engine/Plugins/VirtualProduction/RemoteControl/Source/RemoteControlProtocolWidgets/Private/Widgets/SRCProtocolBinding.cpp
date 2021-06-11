// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCProtocolBinding.h"

#include "EditorFontGlyphs.h"
#include "SDropTarget.h"
#include "SRCBindingWarning.h"
#include "SRCProtocolRangeList.h"
#include "ViewModels/ProtocolBindingViewModel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SRCProtocolEntity.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RemoteControlProtocolWidgets"

void SRCProtocolBinding::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedRef<FProtocolBindingViewModel>& InViewModel)
{
	constexpr float Padding = 2.0f;
	ViewModel = InViewModel;
	PrimaryColumnSizeData = InArgs._PrimaryColumnSizeData;
	SecondaryColumnSizeData = InArgs._SecondaryColumnSizeData;
	OnStartRecording = InArgs._OnStartRecording;
	OnStopRecording = InArgs._OnStopRecording;

	TSharedRef<SHorizontalBox> RowBox = SNew(SHorizontalBox);

	RowBox->AddSlot()
		.Padding(Padding)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(ViewModel->GetProtocolName())
			.TextStyle(FEditorStyle::Get(), "LargeText")
		];

	// Validation warning
	RowBox->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SRCBindingWarning)
			.Status_Lambda([this]()
			{
				FText StatusMessage;
				return ViewModel->IsValid(StatusMessage) ? ERCBindingWarningStatus::Ok : ERCBindingWarningStatus::Warning;
			})
			.StatusMessage_Lambda([this]()
			{
				FText StatusMessage;
				ViewModel->IsValid(StatusMessage);
				return StatusMessage;
			})
		];

	// Delete binding
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

	// Record binding input
	RowBox->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.ToolTipText(LOCTEXT("RecordingButtonToolTip", "Status of the protocol entity binding"))
			.ForegroundColor(FSlateColor::UseForeground())
			.OnClicked(this, &SRCProtocolBinding::ToggleRecording)
			.Content()
			[
				SNew(STextBlock)
				.ColorAndOpacity_Raw(this, &SRCProtocolBinding::GetRecordingButtonColor)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
				.Text(FEditorFontGlyphs::Circle)
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
					SNew(SRCProtocolEntity, ViewModel.ToSharedRef())
				]

				+ SVerticalBox::Slot()
				.Padding(Padding)
				.VAlign(VAlign_Fill)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					[
						SAssignNew(RangeList, SRCProtocolRangeList, ViewModel.ToSharedRef())
						.PrimaryColumnSizeData(PrimaryColumnSizeData)
						.SecondaryColumnSizeData(SecondaryColumnSizeData)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox)
						.WidthOverride(16.0f)
						[
							SNullWidget::NullWidget
						]
					]
				]
			]
		],
		InOwnerTableView);
}

FReply SRCProtocolBinding::OnDelete()
{
	ViewModel->Remove();
	SetVisibility(EVisibility::Collapsed);
	return FReply::Handled();
}

FReply SRCProtocolBinding::ToggleRecording() const
{
	// Binding can't be nullptr
	FRemoteControlProtocolBinding* Binding = ViewModel->GetBinding();
	check(Binding);

	if (const TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>> ProtocolEntity = Binding->GetRemoteControlProtocolEntityPtr())
	{
		const ERCBindingStatus BindingStatus = (*ProtocolEntity)->ToggleBindingStatus();
		if (BindingStatus == ERCBindingStatus::Awaiting)
		{
			OnStartRecording.ExecuteIfBound(ProtocolEntity);
		}
		else if (BindingStatus == ERCBindingStatus::Bound)
		{
			OnStopRecording.ExecuteIfBound(ProtocolEntity);
		}
		else
		{
			checkNoEntry();
		}
	}

	return FReply::Handled();
}

FSlateColor SRCProtocolBinding::GetRecordingButtonColor() const
{
	// Binding can't be nullptr
	FRemoteControlProtocolBinding* Binding = ViewModel->GetBinding();
	check(Binding);

	if (const TSharedPtr<TStructOnScope<FRemoteControlProtocolEntity>> ProtocolEntity = Binding->GetRemoteControlProtocolEntityPtr())
	{
		const ERCBindingStatus BindingStatus = (*ProtocolEntity)->GetBindingStatus();

		switch (BindingStatus)
		{
		case ERCBindingStatus::Awaiting:
			return FLinearColor::Red;
		case ERCBindingStatus::Bound:
			return FLinearColor::Green;
		case ERCBindingStatus::Unassigned:
			return FLinearColor::Gray;
		default:
			checkNoEntry();
		}
	}

	ensure(false);
	return FLinearColor::Black;
}

#undef LOCTEXT_NAMESPACE
