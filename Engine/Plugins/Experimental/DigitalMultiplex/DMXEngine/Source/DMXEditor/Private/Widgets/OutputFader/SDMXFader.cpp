// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/OutputFader/SDMXFader.h"

#include "Interfaces/IDMXProtocol.h"
#include "DMXEditor.h"
#include "Library/DMXEntityFader.h"
#include "DMXEditorLog.h"
#include "DMXProtocolCommon.h"

#include "Widgets/Common/SSpinBoxVertical.h"
#include "Widgets/OutputFader/SDMXOutputFaderList.h"
#include "Widgets/OutputFader/SDMXFaderChannel.h"

#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"

#define LOCTEXT_NAMESPACE "SDMXFader"

void SDMXFader::Construct(const FArguments& InArgs)
{
	WeakDMXEditor = InArgs._DMXEditor;
	CurrentFaderValue = 0;

	OnValueChanged = InArgs._OnValueChanged;
	OnSendStateChanged = InArgs._OnSendStateChanged;

	ChildSlot
		.Padding(0.f)
		[
			SNew(SBox)
			.WidthOverride(85)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Fill)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Fill)
					.FillWidth(25)
					[

						SAssignNew(BackgroundBorder, SBorder)
						.BorderImage(this, &SDMXFader::GetBorderImage)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.VAlign(VAlign_Top)
							.HAlign(HAlign_Fill)
							.AutoHeight()
							[
			
								SNew(SVerticalBox)
		
								+ SVerticalBox::Slot()
								.VAlign(VAlign_Top)
								.HAlign(HAlign_Fill)
								.AutoHeight()
								[
									SAssignNew(CustomFaderLabel, STextBlock)
									.Justification(ETextJustify::Center)
									.Text(InArgs._InText)
								]
							]
							+ SVerticalBox::Slot()
							.VAlign(VAlign_Top)
							.HAlign(HAlign_Center)
							.AutoHeight()
							[
								SNew(SVerticalBox)

								+ SVerticalBox::Slot()
								.VAlign(VAlign_Top)
								.HAlign(HAlign_Center)
								.Padding(0.f, 5.f, 0.f, 0.f)
								.AutoHeight()
								[
									SAssignNew(FaderBoxVertical, SSpinBoxVertical<uint8>)
									.MinValue(0).MaxValue(255)
									.MinSliderValue(0).MaxSliderValue(255)
									.OnValueChanged(this, &SDMXFader::HandleFaderChanged)
									.MinDesiredWidth(25)
							
								]
							]
							+ SVerticalBox::Slot()
								.VAlign(VAlign_Top)
								.HAlign(HAlign_Center)
								.AutoHeight()
								[
									SNew(SVerticalBox)

									+ SVerticalBox::Slot()
								.VAlign(VAlign_Top)
								.HAlign(HAlign_Center)
								.Padding(0.f, 5.f, 0.f, 0.f)
								.AutoHeight()
								[
									SAssignNew(SendDMXCheckBox, SCheckBox)
									.IsChecked(ECheckBoxState::Checked)
									.OnCheckStateChanged(this, &SDMXFader::HandleSendDMXCheckChanged)
								]
							]
							+ SVerticalBox::Slot()
							.VAlign(VAlign_Top)
							.HAlign(HAlign_Fill)
							.AutoHeight()
							[
								SNew(SVerticalBox)

								+ SVerticalBox::Slot()
								.VAlign(VAlign_Top)
								.HAlign(HAlign_Fill)
								.Padding(0.f, 5.f, 0.f, 0.f)
								.AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.VAlign(VAlign_Center)
									.HAlign(HAlign_Fill)
									.FillWidth(1)
									[
										SNew(SVerticalBox)
										+ SVerticalBox::Slot()
										.VAlign(VAlign_Fill)
										.HAlign(HAlign_Fill)
										.AutoHeight()
										[
											SNew(STextBlock)
											.Justification(ETextJustify::Center)
											.Text(LOCTEXT("Universe", "Uni"))
										]
										+SVerticalBox::Slot()
										.VAlign(VAlign_Fill)
										.HAlign(HAlign_Fill)
										.AutoHeight()
										[
											SNew(SSeparator)
											.Orientation(EOrientation::Orient_Horizontal)
										]
								
									]
									+ SHorizontalBox::Slot()
									.VAlign(VAlign_Center)
									.HAlign(HAlign_Fill)
									.FillWidth(1)
									[
										SNew(SVerticalBox)

										+ SVerticalBox::Slot()
										.VAlign(VAlign_Fill)
										.HAlign(HAlign_Fill)
										.AutoHeight()
										[
											SNew(STextBlock)
											.Justification(ETextJustify::Center)
											.Text(LOCTEXT("Address", "Addr"))
										]
										+ SVerticalBox::Slot()
										.VAlign(VAlign_Fill)
										.HAlign(HAlign_Fill)
										.AutoHeight()
										[
											SNew(SSeparator)
											.Orientation(EOrientation::Orient_Horizontal)
										]
									]
								]
								+ SVerticalBox::Slot()
								.VAlign(VAlign_Top)
								.HAlign(HAlign_Fill)
								.Padding(0.f, 5.f, 0.f, 0.f)
								.MaxHeight(100)
								[
									SAssignNew(FaderChannelSlots, SScrollBox)
									.Orientation(EOrientation::Orient_Vertical)
									.ScrollBarAlwaysVisible(false)
									.ScrollBarThickness(FVector2D(0.0f))
									.ScrollBarPadding(0.f)
								]
							]
						]
					]
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Fill)
					.HAlign(HAlign_Fill)
					.FillWidth(1)
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryMiddle_Hovered"))
						.Padding(FMargin(0, 5, 0, 5))
					]
				]
				+ SVerticalBox::Slot()
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Justification(ETextJustify::Center)
					.Text(this, &SDMXFader::GetProtocolText)
				]
				+ SVerticalBox::Slot()
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Fill)
				.AutoHeight()
				[
					SNew(SSeparator)
					.Orientation(EOrientation::Orient_Horizontal)
				]
				+ SVerticalBox::Slot()
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Center)
				.AutoHeight()
				[
					SNew(SBox)
					.HeightOverride(20)
					.WidthOverride(20)
					[
						SNew(SButton)
						.Text(LOCTEXT("Close", "X"))
						.HAlign(HAlign_Center)
						.OnClicked(this, &SDMXFader::HandleRemoveFaderClicked)
					]
				]
			]
		];
}

FReply SDMXFader::HandleRemoveFaderClicked()
{
	if (TSharedPtr<SDMXOutputFaderList> FaderList = WeakFaderList.Pin())
	{
		if (TSharedPtr<SDMXFader> FaderPtr = FaderList->GetWeakSelectedFaderWidget().Pin())
		{
			if (FaderPtr.Get() == this)
			{
				FaderList->DeselectFaders();
			}
		}
		FaderList->RemoveFader(SharedThis(this));
	}

	return FReply::Handled();
}

void SDMXFader::HandleSendDMXCheckChanged(ECheckBoxState NewState)
{

}

FReply SDMXFader::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		SelectThisFader();
	}

	return FReply::Handled();
}

void SDMXFader::SetFaderEntity(const TWeakObjectPtr<UDMXEntityFader> InFaderEntity)
{
	if (!InFaderEntity.IsValid())
	{
		UE_LOG_DMXEDITOR(Warning, TEXT("InFaderWidget is invalid pointer"));
		return;
	}

	WeakFaderEntity = InFaderEntity;
}

void SDMXFader::SetParentFaderList(const TSharedPtr<SDMXOutputFaderList>& InFaderList)
{
	WeakFaderList = InFaderList;
}

void SDMXFader::SetFaderLabel(const FString& InLabel)
{
	CustomFaderLabel->SetText(FText::FromString(InLabel));
}

void SDMXFader::AddChannelWidget(const FString& InUniverse, const FString& InChannel, uint16 InUniverseNumber, uint32 InChannelNumber)
{
	TSharedPtr<SDMXFaderChannel> NewChannel = SNew(SDMXFaderChannel)
		.DMXEditor(WeakDMXEditor)
		.UniverseNumber(InUniverseNumber)
		.ChannelNumber(InChannelNumber);

	FaderChannels.Add(NewChannel);

	FaderChannelSlots->AddSlot()
		[
			NewChannel.ToSharedRef()
		];

	if (NewChannel->GetUniverseValue().IsValid())
	{
		NewChannel->GetUniverseValue()->SetText(FText::FromString(InUniverse));
	}
	if (NewChannel->GetChannelValue().IsValid())
	{
		NewChannel->GetChannelValue()->SetText(FText::FromString(InChannel));
	}
}

void SDMXFader::RemoveAllChannelWidgets()
{
	FaderChannelSlots->ClearChildren();
	FaderChannels.Empty();
}

void SDMXFader::UpdateFaderTemplateProperties()
{
	if (TSharedPtr<SDMXOutputFaderList> FaderList = WeakFaderList.Pin())
	{
		FaderList->UpdateFaderTemplateObject(WeakFaderEntity);
	}
}

void SDMXFader::SelectThisFader()
{
	if (TSharedPtr<SDMXOutputFaderList> FaderList = WeakFaderList.Pin())
	{
		FaderList->ResetFaderBackgrounds();
		FaderList->WeakSelectedFaderWidget = SharedThis(this);
		UpdateFaderTemplateProperties();
		BackgroundBorder->SetBorderImage(FEditorStyle::GetBrush("DetailsView.CategoryMiddle_Highlighted"));
	}
}

bool SDMXFader::ShouldSendDMX() const
{
	return SendDMXCheckBox->IsChecked();
}

const FSlateBrush* SDMXFader::GetBorderImage() const
{
	if (IsHovered())
	{
		return FEditorStyle::GetBrush("DetailsView.CategoryMiddle_Hovered");
	}
	else
	{
		return FEditorStyle::GetBrush("DetailsView.CategoryMiddle");
	}
}

void SDMXFader::HandleFaderChanged(uint8 NewValue)
{
	CurrentFaderValue = NewValue;

	if (WeakFaderEntity.IsValid() && CachedProtocol.IsValid()
		&& SendDMXCheckBox.IsValid() && SendDMXCheckBox->IsChecked()
		&& OnValueChanged.IsBound())
	{
		OnValueChanged.Execute(SharedThis(this));
	}
}
FText SDMXFader::GetProtocolText() const
{
	if (WeakFaderEntity.IsValid())
	{
		return FText::FromName(CachedProtocol);
	}

	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
