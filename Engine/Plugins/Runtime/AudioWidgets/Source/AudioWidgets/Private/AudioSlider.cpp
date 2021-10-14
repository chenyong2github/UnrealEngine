// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioSlider.h"
#include "SAudioSlider.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

#define LOCTEXT_NAMESPACE "AUDIO_UMG"

// UAudioSliderBase

UAudioSliderBase::UAudioSliderBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Value = 0.0f;
	AlwaysShowLabel = true;
	ShowUnitsText = true;
	Orientation = Orient_Vertical;
	LabelBackgroundColor = FLinearColor(0.01033f, 0.01033f, 0.01033f);
	SliderBackgroundColor = FLinearColor(0.01033f, 0.01033f, 0.01033f);
	SliderBarColor = FLinearColor::Black;
	SliderThumbColor = FLinearColor::Gray;
	WidgetBackgroundColor = FLinearColor::Transparent;

#if WITH_EDITORONLY_DATA
	AccessibleBehavior = ESlateAccessibleBehavior::NotAccessible;
	bCanChildrenBeAccessible = false;
#endif
}

void UAudioSliderBase::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	
	MyAudioSlider->SetAlwaysShowLabel(AlwaysShowLabel);
	MyAudioSlider->SetShowUnitsText(ShowUnitsText);
	MyAudioSlider->SetLabelBackgroundColor(LabelBackgroundColor);
	MyAudioSlider->SetOrientation(Orientation);
	MyAudioSlider->SetSliderBackgroundColor(SliderBackgroundColor);
	MyAudioSlider->SetSliderBarColor(SliderBarColor);
	MyAudioSlider->SetSliderThumbColor(SliderThumbColor);
	MyAudioSlider->SetWidgetBackgroundColor(WidgetBackgroundColor);
}

void UAudioSliderBase::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyAudioSlider.Reset();
}

void UAudioSliderBase::HandleOnValueChanged(float InValue)
{
	OnValueChanged.Broadcast(InValue);
}

void UAudioSliderBase::SetAllTextReadOnly(const bool bIsReadOnly)
{
	MyAudioSlider->SetAllTextReadOnly(bIsReadOnly);
}

void UAudioSliderBase::SetAlwaysShowLabel(const bool bSetAlwaysShowLabel)
{
	AlwaysShowLabel = bSetAlwaysShowLabel;
	MyAudioSlider->SetAlwaysShowLabel(bSetAlwaysShowLabel);
}

void UAudioSliderBase::SetShowUnitsText(const bool bShowUnitsText)
{
	ShowUnitsText = bShowUnitsText;
	MyAudioSlider->SetShowUnitsText(bShowUnitsText);
}

void UAudioSliderBase::SetUnitsText(const FText Units)
{
	MyAudioSlider->SetUnitsText(Units);
}

float UAudioSliderBase::GetOutputValue(const float LinValue)
{
	return MyAudioSlider->GetOutputValue(LinValue);
}

float UAudioSliderBase::GetLinValue(const float OutputValue)
{
	return MyAudioSlider->GetLinValue(OutputValue);
}

void UAudioSliderBase::SetLabelBackgroundColor(FLinearColor InValue)
{
	SliderBackgroundColor = InValue;
	if (MyAudioSlider.IsValid())
	{
		MyAudioSlider->SetLabelBackgroundColor(InValue);
	}
}

void UAudioSliderBase::SetSliderBackgroundColor(FLinearColor InValue)
{
	SliderBackgroundColor = InValue;
	if (MyAudioSlider.IsValid())
	{
		MyAudioSlider->SetSliderBackgroundColor(InValue);
	}
}

void UAudioSliderBase::SetSliderBarColor(FLinearColor InValue)
{
	SliderBarColor = InValue;
	if (MyAudioSlider.IsValid())
	{
		MyAudioSlider->SetSliderBarColor(InValue);
	}
}

void UAudioSliderBase::SetSliderThumbColor(FLinearColor InValue)
{
	SliderThumbColor = InValue;
	if (MyAudioSlider.IsValid())
	{
		MyAudioSlider->SetSliderThumbColor(InValue);
	}
}

void UAudioSliderBase::SetWidgetBackgroundColor(FLinearColor InValue)
{
	WidgetBackgroundColor = InValue;
	if (MyAudioSlider.IsValid())
	{
		MyAudioSlider->SetWidgetBackgroundColor(InValue);
	}
}

TSharedRef<SWidget> UAudioSliderBase::RebuildWidget()
{
	MyAudioSlider = SNew(SAudioSliderBase)
		.OnValueChanged(BIND_UOBJECT_DELEGATE(FOnFloatValueChanged, HandleOnValueChanged));

	return MyAudioSlider.ToSharedRef();
}

#if WITH_EDITOR

const FText UAudioSliderBase::GetPaletteCategory()
{
	return LOCTEXT("Audio", "Audio");
}

#endif

// UAudioSlider
UAudioSlider::UAudioSlider(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UAudioSlider::SynchronizeProperties()
{
	UAudioSliderBase::SynchronizeProperties();

	StaticCastSharedPtr<SAudioSlider>(MyAudioSlider)->SetLinToOutputCurve(LinToOutputCurve);
	StaticCastSharedPtr<SAudioSlider>(MyAudioSlider)->SetOutputToLinCurve(OutputToLinCurve);
}

TSharedRef<SWidget> UAudioSlider::RebuildWidget()
{
	MyAudioSlider = SNew(SAudioSlider)
		.OnValueChanged(BIND_UOBJECT_DELEGATE(FOnFloatValueChanged, HandleOnValueChanged));

	return MyAudioSlider.ToSharedRef();
}

// UAudioVolumeSlider
UAudioVolumeSlider::UAudioVolumeSlider(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TSharedRef<SWidget> UAudioVolumeSlider::RebuildWidget()
{
	MyAudioSlider = SNew(SAudioVolumeSlider)
		.OnValueChanged(BIND_UOBJECT_DELEGATE(FOnFloatValueChanged, HandleOnValueChanged));
	LinToOutputCurve = StaticCastSharedPtr<SAudioSlider>(MyAudioSlider)->GetLinToOutputCurve();
	OutputToLinCurve = StaticCastSharedPtr<SAudioSlider>(MyAudioSlider)->GetOutputToLinCurve();

	return MyAudioSlider.ToSharedRef();
}

// UAudioFrequencySlider
UAudioFrequencySlider::UAudioFrequencySlider(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, OutputRange(FVector2D(20.0f, 20000.0f))
{
}

TSharedRef<SWidget> UAudioFrequencySlider::RebuildWidget()
{
	MyAudioSlider = SNew(SAudioFrequencySlider)
		.OnValueChanged(BIND_UOBJECT_DELEGATE(FOnFloatValueChanged, HandleOnValueChanged));
	StaticCastSharedPtr<SAudioFrequencySlider>(MyAudioSlider)->SetOutputRange(OutputRange);

	return MyAudioSlider.ToSharedRef();
}

#undef LOCTEXT_NAMESPACE