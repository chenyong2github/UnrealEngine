// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMeter.h"
#include "SAudioMeter.h"
#include "Widgets/DeclarativeSyntaxSupport.h"


#define LOCTEXT_NAMESPACE "AUDIO_UMG"

/////////////////////////////////////////////////////
// UAudioMeter

static FAudioMeterStyle* DefaultAudioMeterStyle = nullptr;

UAudioMeter::UAudioMeter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Orientation = EOrientation::Orient_Vertical;

	BackgroundColor = FLinearColor::Black;
	MeterBackgroundColor = FLinearColor::Gray;
	MeterValueColor = FLinearColor::Green;
	MeterPeakColor = FLinearColor::Blue;
	MeterClippingColor = FLinearColor::Red;
	MeterScaleColor = FLinearColor::Gray;
	MeterScaleLabelColor = FLinearColor::White;

	if (DefaultAudioMeterStyle == nullptr)
	{
		// HACK: THIS SHOULD NOT COME FROM CORESTYLE AND SHOULD INSTEAD BE DEFINED BY ENGINE TEXTURES/PROJECT SETTINGS
		DefaultAudioMeterStyle = new FAudioMeterStyle(FCoreStyle::Get().GetWidgetStyle<FAudioMeterStyle>("AudioMeter"));

		// Unlink UMG default colors from the editor settings colors.
		DefaultAudioMeterStyle->UnlinkColors();
	}

	WidgetStyle = *DefaultAudioMeterStyle;

#if WITH_EDITORONLY_DATA
	AccessibleBehavior = ESlateAccessibleBehavior::NotAccessible;
	bCanChildrenBeAccessible = false;
#endif
}

TSharedRef<SWidget> UAudioMeter::RebuildWidget()
{
	MyAudioMeter = SNew(SAudioMeter)
		.Style(&WidgetStyle);

	return MyAudioMeter.ToSharedRef();
}

void UAudioMeter::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	MyAudioMeter->SetOrientation(Orientation);

	MyAudioMeter->SetBackgroundColor(BackgroundColor);
	MyAudioMeter->SetMeterBackgroundColor(MeterBackgroundColor);
	MyAudioMeter->SetMeterValueColor(MeterValueColor);
	MyAudioMeter->SetMeterPeakColor(MeterPeakColor);
	MyAudioMeter->SetMeterClippingColor(MeterClippingColor);
	MyAudioMeter->SetMeterScaleColor(MeterScaleColor);
	MyAudioMeter->SetMeterScaleLabelColor(MeterScaleLabelColor);

	TAttribute<TArray<FMeterChannelInfo>> MeterChannelInfoBinding = PROPERTY_BINDING(TArray<FMeterChannelInfo>, MeterChannelInfo);
	MyAudioMeter->SetMeterChannelInfo(MeterChannelInfoBinding);
}

void UAudioMeter::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyAudioMeter.Reset();
}

TArray<FMeterChannelInfo> UAudioMeter::GetMeterChannelInfo() const
{
	if (MyAudioMeter.IsValid())
	{
		return MyAudioMeter->GetMeterChannelInfo();
	}
	return TArray<FMeterChannelInfo>();
}

void UAudioMeter::SetMeterChannelInfo(const TArray<FMeterChannelInfo>& InMeterChannelInfo)
{
	if (MyAudioMeter.IsValid())
	{
		MyAudioMeter->SetMeterChannelInfo(InMeterChannelInfo);
	}
}

void UAudioMeter::SetBackgroundColor(FLinearColor InValue)
{
	BackgroundColor = InValue;
	if (MyAudioMeter.IsValid())
	{
		MyAudioMeter->SetBackgroundColor(InValue);
	}
}

void UAudioMeter::SetMeterBackgroundColor(FLinearColor InValue)
{
	MeterBackgroundColor = InValue;
	if (MyAudioMeter.IsValid())
	{
		MyAudioMeter->SetMeterBackgroundColor(InValue);
	}
}

void UAudioMeter::SetMeterValueColor(FLinearColor InValue)
{
	MeterValueColor = InValue;
	if (MyAudioMeter.IsValid())
	{
		MyAudioMeter->SetMeterValueColor(InValue);
	}
}

void UAudioMeter::SetMeterPeakColor(FLinearColor InValue)
{
	MeterPeakColor = InValue;
	if (MyAudioMeter.IsValid())
	{
		MyAudioMeter->SetMeterPeakColor(InValue);
	}
}

void UAudioMeter::SetMeterClippingColor(FLinearColor InValue)
{
	MeterClippingColor = InValue;
	if (MyAudioMeter.IsValid())
	{
		MyAudioMeter->SetMeterClippingColor(InValue);
	}
}

void UAudioMeter::SetMeterScaleColor(FLinearColor InValue)
{
	MeterScaleColor = InValue;
	if (MyAudioMeter.IsValid())
	{
		MyAudioMeter->SetMeterScaleColor(InValue);
	}
}

void UAudioMeter::SetMeterScaleLabelColor(FLinearColor InValue)
{
	MeterScaleLabelColor = InValue;
	if (MyAudioMeter.IsValid())
	{
		MyAudioMeter->SetMeterScaleLabelColor(InValue);
	}
}

#if WITH_EDITOR

const FText UAudioMeter::GetPaletteCategory()
{
	return LOCTEXT("Common", "Common");
}

#endif

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE