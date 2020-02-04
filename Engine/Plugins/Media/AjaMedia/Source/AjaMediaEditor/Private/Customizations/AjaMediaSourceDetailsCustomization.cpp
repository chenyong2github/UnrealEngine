// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/AjaMediaSourceDetailsCustomization.h"

#include "AjaDeviceProvider.h"
#include "AjaMediaSource.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EditorStyleSet.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "AjaMediaSourceDetailsCustomization"

/**
 *
 */
TSharedRef<IDetailCustomization> FAjaMediaSourceDetailsCustomization::MakeInstance()
{
	return MakeShared<FAjaMediaSourceDetailsCustomization>();
}


void FAjaMediaSourceDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	DetailLayout.GetObjectsBeingCustomized(CustomizedObjects);

	if (CustomizedObjects.Num())
	{
		DeviceProvider.Reset(new FAjaDeviceProvider);
		bAutoDetectRequested = false;

		IDetailCategoryBuilder& AJACategory = DetailLayout.EditCategory(TEXT("AJA"));
		AJACategory.AddCustomRow(LOCTEXT("AJA", "AJA"), false)
		.WholeRowWidget
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.LightGroupBorder"))
			[
				SNew(SButton)
				.OnClicked(FOnClicked::CreateRaw(this, &FAjaMediaSourceDetailsCustomization::OnAutoDetectClicked))
				.IsEnabled_Raw(this, &FAjaMediaSourceDetailsCustomization::IsAutoDetectEnabled)
				.HAlign(HAlign_Center)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SCircularThrobber)
						.Visibility_Raw(this, &FAjaMediaSourceDetailsCustomization::GetThrobberVisibility)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AutoDetect", "Auto Detect"))
						.TextStyle(&FCoreStyle::Get().GetWidgetStyle< FTextBlockStyle >("NormalText"))
					]
				]
			]
		];
	}
}


FReply FAjaMediaSourceDetailsCustomization::OnAutoDetectClicked()
{
	if (CustomizedObjects.Num())
	{
		bAutoDetectRequested = true;
		DeviceProvider->AutoDetectConfiguration(FAjaDeviceProvider::FOnConfigurationAutoDetected::CreateRaw(this, &FAjaMediaSourceDetailsCustomization::OnAutoDetected));
	}
	return FReply::Handled();
}

bool FAjaMediaSourceDetailsCustomization::IsAutoDetectEnabled() const
{
	return CustomizedObjects.Num() && !bAutoDetectRequested;
}

EVisibility FAjaMediaSourceDetailsCustomization::GetThrobberVisibility() const
{
	return (CustomizedObjects.Num() && bAutoDetectRequested) ? EVisibility::HitTestInvisible : EVisibility::Hidden;
}

void FAjaMediaSourceDetailsCustomization::OnAutoDetected(TArray<FAjaDeviceProvider::FMediaIOConfigurationWithTimecodeFormat> Configurations)
{
	bAutoDetectRequested = false;

	if (Configurations.Num())
	{
		for (TWeakObjectPtr<UObject> EditingObject : CustomizedObjects)
		{
			if (UAjaMediaSource* MediaSource = Cast<UAjaMediaSource>(EditingObject.Get()))
			{
				MediaSource->MediaConfiguration = Configurations[0].Configuration;
				MediaSource->TimecodeFormat = Configurations[0].TimecodeFormat;
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
