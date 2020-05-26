// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXInputInfoUniverseCounts.h"
#include "Widgets/SDMXInputInfoUniverseMonitor.h"
#include "Widgets/SDMXInputInfoUniverseChannelView.h"
#include "Interfaces/IDMXProtocol.h"
#include "DMXEditorLog.h"
#include "DMXProtocolConstants.h"
#include "DMXEditorStyle.h"

#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Views/SListView.h"
#include "Brushes/SlateColorBrush.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Font.h"

#define LOCTEXT_NAMESPACE "SDMXInputInfo"

const float SDMXInputInfoUniverseCounts::NewValueChangedAnimDuration = 0.8f;

const FLinearColor SDMXInputInfoUniverseCounts::IDColor = FLinearColor(1, 1, 1, 0.6f);
const FLinearColor SDMXInputInfoUniverseCounts::ValueColor = FLinearColor(1, 1, 1, 0.9f);

void SDMXInputInfoUniverseCounts::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::SelfHitTestInvisible);
	SetCanTick(false);

	BoundID = InArgs._ID;
	BoundValue = InArgs._Value;
	NewValueFreshness = 0.0f;

	// Initialize to show nothing.
	for (int i = 0; i < 512; i++)
		ChannelValuesViews.Emplace();

	const float PaddingInfo = 3.0f;

	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			[
				SNew(SHorizontalBox)

				// Universe Label
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				.MaxWidth(50.0f)
				.FillWidth(6.0f)
				[
					SNew(STextBlock)
					.Text(this, &SDMXInputInfoUniverseCounts::GetIDLabel)
					.ColorAndOpacity(FSlateColor(IDColor))
					.MinDesiredWidth(23.0f)
					.Justification(ETextJustify::Right)
					.Font(FDMXEditorStyle::Get().GetFontStyle("DMXEditor.Font.InputUniverseID"))
				]
					
				// Channels View
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Fill)
				.FillWidth(94.0f)
				[
					SAssignNew(ChannelsView, SListView<TSharedPtr<SDMXInputInfoUniverseChannelView>>)
					.OnGenerateRow(this, &SDMXInputInfoUniverseCounts::GenerateRow)
					.ListItemsSource(&ChannelValuesViews)
					.Orientation(EOrientation::Orient_Horizontal)
					.ScrollbarVisibility(EVisibility::Collapsed)
					.ItemHeight(40.0f)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSeparator)
			]
		];
}

void SDMXInputInfoUniverseCounts::SetID(const TAttribute<uint32>& NewID)
{
	BoundID = NewID;
}

void SDMXInputInfoUniverseCounts::SetValue(const TAttribute<TSharedPtr<FUniverseCount>>& NewValue)
{
	// is NewValue a different value from current one?
	if (NewValue.Get() != BoundValue.Get())
	{
		// Activate timer to animate value bar color
		if (!AnimationTimerHandle.IsValid())
		{
			AnimationTimerHandle = RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SDMXInputInfoUniverseCounts::UpdateValueChangedAnim));
		}
		// restart value change animation
		NewValueFreshness = 1.0f;
	}
	BoundValue = NewValue;
	UpdateChannelsView();
}

void SDMXInputInfoUniverseCounts::UpdateChannelsView()
{
	const TSharedPtr<FUniverseCount>& Value = BoundValue.Get();
	if (Value.IsValid())
	{
		TMap<int, uint8>& NewValues = Value->GetChannelValues();
		bool HasChanges = false;
		for (auto KV : NewValues)
		{
			if (ChannelValuesViews[KV.Key].IsValid())
			{
				if (ChannelValuesViews[KV.Key]->GetValue() != KV.Value)
				{
					ChannelValuesViews[KV.Key]->SetValue(KV.Value);
					HasChanges = true;
				}
			}
			else
			{
				ChannelValuesViews[KV.Key] =
					SNew(SDMXInputInfoUniverseChannelView)
					.ID(KV.Key)
					.Value(KV.Value);
				HasChanges = true;
			}
		}
		if (ChannelsView.IsValid() && HasChanges)
		{
			ChannelsView->RequestListRefresh();
		}
	}
}

TSharedRef<ITableRow> SDMXInputInfoUniverseCounts::GenerateRow(TSharedPtr<SDMXInputInfoUniverseChannelView> InChannelView, const TSharedRef<STableViewBase>& OwnerTable)
{
	typedef STableRow<TSharedPtr<SDMXInputInfoUniverseChannelView>> RowType;

	TSharedRef<RowType> NewRow = SNew(RowType, OwnerTable);
	NewRow->SetContent(InChannelView.ToSharedRef());
		
	return NewRow;
}

EActiveTimerReturnType SDMXInputInfoUniverseCounts::UpdateValueChangedAnim(double InCurrentTime, float InDeltaTime)
{
	NewValueFreshness = FMath::Max(NewValueFreshness - InDeltaTime / NewValueChangedAnimDuration, 0.0f);

	// disable timer when the value bar color animation ends
	if (NewValueFreshness <= 0.0f)
	{
		TSharedPtr<FActiveTimerHandle> PinnedTimerHandle = AnimationTimerHandle.Pin();
		if (PinnedTimerHandle.IsValid())
		{
			UnRegisterActiveTimer(PinnedTimerHandle.ToSharedRef());
		}
	}
	return EActiveTimerReturnType::Continue;
}

FText SDMXInputInfoUniverseCounts::GetIDLabel() const
{
	return FText::AsNumber(BoundID.Get());
}

FSlateColor SDMXInputInfoUniverseCounts::GetBackgroundColor() const
{
	const float CurrentPercent = 0.5;

	// Intensities to be animated when a new value is set and then multiplied by the background color
	static const float NormalIntensity = 0.3f;
	static const float FreshValueIntensity = 0.7f;
	// lerp intensity depending on NewValueFreshness^2 to make it pop for a while when it has just been updated
	const float ValueFreshnessIntensity = FMath::Lerp(NormalIntensity, FreshValueIntensity, NewValueFreshness * NewValueFreshness);

	// color variations for low and high channel values
	static const FVector LowValueColor = FVector(0, 0.045f, 0.15f);
	static const FVector HighValueColor = FVector(0, 0.3f, 1.0f);
	const FVector ColorFromChannelValue = FMath::Lerp(LowValueColor, HighValueColor, CurrentPercent);

	// returning a FVector, a new FSlateColor will be created from it with (RGB = vector, Alpha = 1.0)
	FVector Result = ColorFromChannelValue * ValueFreshnessIntensity;
	return FSlateColor(FLinearColor(Result.X, Result.Y, Result.Z));
}

#undef LOCTEXT_NAMESPACE
