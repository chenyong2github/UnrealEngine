// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertFrontendLogFilter_Size.h"

#include "ConcertFrontendUtils.h"
#include "ConcertTransportEvents.h"
#include "SSimpleComboButton.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Clients/Logging/ConcertLogEntry.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI.FConcertLogFilter_Size"

bool FConcertLogFilter_Size::PassesFilter(const FConcertLogEntry& InItem) const
{
	check(InItem.Log.CustomPayloadUncompressedByteSize >= 0);
	// Note: This only filters activities events - they all use custom events.
	// The filter's default value is to show everything 0 <= x so it will show sync events as well.
	const uint32 ComparisionSizeInBytes = FUnitConversion::Convert(SizeInBytes, DataUnit, EUnit::Bytes);
	switch (FilterMode)
	{
	case ESizeFilterMode::LessThanOrEqual:
		return ComparisionSizeInBytes >= static_cast<uint32>(InItem.Log.CustomPayloadUncompressedByteSize);
	case ESizeFilterMode::BiggerThanOrEqual:
		return ComparisionSizeInBytes <= static_cast<uint32>(InItem.Log.CustomPayloadUncompressedByteSize);
	default:
		checkNoEntry();
		return false;
	}
}

void FConcertLogFilter_Size::AdvanceFilterMode()
{
	switch (FilterMode)
	{
	case ESizeFilterMode::LessThanOrEqual:
		FilterMode = ESizeFilterMode::BiggerThanOrEqual;
		break;
	case ESizeFilterMode::BiggerThanOrEqual:
		FilterMode = ESizeFilterMode::LessThanOrEqual;
		break;
	default:
		checkNoEntry();
	}

	OnChanged().Broadcast();
}

void FConcertLogFilter_Size::SetSizeInBytes(uint32 NewSizeInBytes)
{
	if (NewSizeInBytes != SizeInBytes)
	{
		SizeInBytes = NewSizeInBytes;
		OnChanged().Broadcast();
	}
}

void FConcertLogFilter_Size::SetDataUnit(EUnit NewUnit)
{
	if (DataUnit != NewUnit && ensure(GetAllowedUnits().Contains(NewUnit)))
	{
		DataUnit = NewUnit;
		OnChanged().Broadcast();
	}
}

FConcertFrontendLogFilter_Size::FConcertFrontendLogFilter_Size()
{
	ChildSlot = SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Center)
		[
			SNew(SButton)
			.OnClicked_Lambda([this]()
			{
				Implementation.AdvanceFilterMode();
				return FReply::Handled();
			})
			.ToolTipText_Lambda([this]()
			{
				switch (Implementation.GetFilterMode())
				{
				case ESizeFilterMode::BiggerThanOrEqual: return FText::Format(LOCTEXT("BiggerThanOrEqual.ToolTipFmt", "Size >= {0}"), GetSizeAndUnitAsText());
					case ESizeFilterMode::LessThanOrEqual: return FText::Format(LOCTEXT("LessThanOrEqual.ToolTipFmt", "Size <= {0}"), GetSizeAndUnitAsText());
					default: return FText::GetEmpty();
				}
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					switch (Implementation.GetFilterMode())
					{
						case ESizeFilterMode::BiggerThanOrEqual: return LOCTEXT("BiggerThanOrEqual.Text", ">=") ;
						case ESizeFilterMode::LessThanOrEqual: return LOCTEXT("LessThanOrEqual.Text", "<=") ;
						default: return FText::GetEmpty();
					}
				})
			]
		]
	
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SNumericEntryBox<uint32>)
			.AllowSpin(true)
			.MinDesiredValueWidth(30)
			.MaxSliderValue(1000)
			.OnValueChanged_Lambda([this](uint32 NewValue){ Implementation.SetSizeInBytes(NewValue); })
			.Value_Lambda([this](){ return Implementation.GetSizeInBytes(); })
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SSimpleComboButton)
			.HasDownArrow(true)
			.Text_Lambda([this](){ return FText::FromString(FUnitConversion::GetUnitDisplayString(Implementation.GetDataUnit())); })
			.MenuContent()
			[
				MakeDataUnitMenu()
			]
		];
}

TSharedRef<SWidget> FConcertFrontendLogFilter_Size::MakeDataUnitMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	for (const EUnit Unit : Implementation.GetAllowedUnits())
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString(FUnitConversion::GetUnitDisplayString(Unit)),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, Unit]()
				{
					Implementation.SetDataUnit(Unit);
				}),
				FCanExecuteAction::CreateLambda([] { return true; })),
			NAME_None,
			EUserInterfaceActionType::Button
			);
	}
	
	return MenuBuilder.MakeWidget();
}

FText FConcertFrontendLogFilter_Size::GetSizeAndUnitAsText() const
{
	return FText::Format(
		LOCTEXT("SizeAndUnitAsTextFmt", "{0} {1}"),
		FUnitConversion::Convert(Implementation.GetSizeInBytes(), Implementation.GetDataUnit(), EUnit::Bytes),
		FText::FromString(FUnitConversion::GetUnitDisplayString(Implementation.GetDataUnit()))
		);
}

#undef LOCTEXT_NAMESPACE
