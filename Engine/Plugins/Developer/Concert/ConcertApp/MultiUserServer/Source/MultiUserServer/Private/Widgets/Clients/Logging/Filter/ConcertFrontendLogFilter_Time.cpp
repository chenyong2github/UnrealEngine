// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertFrontendLogFilter_Time.h"

#include "ConcertFrontendUtils.h"
#include "ConcertTransportEvents.h"

#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

FConcertLogFilter_Time::FConcertLogFilter_Time(ETimeFilter FilterMode)
	: FilterMode(FilterMode)
	// Make the filter allow everything by default
	, Time(MakeResetTime())
{}

void FConcertLogFilter_Time::ResetToInfiniteTime()
{
	SetTime(MakeResetTime());
}

FDateTime FConcertLogFilter_Time::MakeResetTime() const
{
	return FilterMode == ETimeFilter::AllowAfter ? FDateTime() : FDateTime::MaxValue();
}

bool FConcertLogFilter_Time::PassesFilter(const FConcertLog& InItem) const
{
	switch (FilterMode)
	{
	case ETimeFilter::AllowAfter: return InItem.Timestamp >= Time;
	case ETimeFilter::AllowBefore: return InItem.Timestamp <= Time;
	default:
		checkNoEntry();
		return true;
	}
}

void FConcertLogFilter_Time::SetFilterMode(ETimeFilter InFilterMode)
{
	if (FilterMode != InFilterMode)
	{
		FilterMode = InFilterMode;
		OnChanged().Broadcast();
	}
}

void FConcertLogFilter_Time::SetTime(const FDateTime& InTime)
{
	if (Time != InTime)
	{
		Time = InTime;
		OnChanged().Broadcast();
	}
}

FConcertFrontendLogFilter_Time::FConcertFrontendLogFilter_Time(ETimeFilter TimeFilter)
	: Super(TimeFilter)
{
	ChildSlot = SNew(SHorizontalBox)
		.ToolTipText(LOCTEXT("TimeFilter.ToolTipText", "Filter logs by local time"))

		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				return Implementation.GetFilterMode() == ETimeFilter::AllowAfter
					? LOCTEXT("TimeFilter.AllowAfter", "After")
					: LOCTEXT("TimeFilter.AllowBefore", "Before");
			})
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0, 0, 0)
		.VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.OnGetMenuContent_Raw(this, &FConcertFrontendLogFilter_Time::CreateDatePicker)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text_Lambda([this](){ return FText::AsDateTime(Implementation.GetTime()); })
			]
		];
}

TSharedRef<SWidget> FConcertFrontendLogFilter_Time::CreateDatePicker()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("TimeFilter.Clear.", "Clear"),
		LOCTEXT("TimeFilter.Clear.Tooltip", "Sets the time so that this filter has no effect"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this](){ Implementation.ResetToInfiniteTime(); }),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked()),
		NAME_None,
		EUserInterfaceActionType::Button
		);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("TimeFilter.Now.", "Now"),
		LOCTEXT("TimeFilter.Now.Tooltip", "Sets the time to now in local time"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this](){ Implementation.SetTime(FDateTime::UtcNow()); }),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked()),
		NAME_None,
		EUserInterfaceActionType::Button
	);
	MenuBuilder.AddSeparator();

	MenuBuilder.AddWidget(
		SNew(SEditableTextBox)
		.Text(FText::FromString(Implementation.GetTime().ToString()))
		.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
		{
			FDateTime Time;
			if (FDateTime::Parse(Text.ToString(), Time))
			{
				Implementation.SetTime(Time);
			}
		}),
		LOCTEXT("TimeFilter.Custom.", "Custom time")
		);
	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
