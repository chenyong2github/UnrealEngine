// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Components/Slider.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SSlider.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// USlider

static FSliderStyle* DefaultSliderStyle = nullptr;

USlider::USlider(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MinValue = 0.0f;
	MaxValue = 1.0f;
	Orientation = EOrientation::Orient_Horizontal;
	SliderBarColor = FLinearColor::White;
	SliderHandleColor = FLinearColor::White;
	StepSize = 0.01f;
	IsFocusable = true;
	MouseUsesStep = false;
	RequiresControllerLock = true;

	if (DefaultSliderStyle == nullptr)
	{
		// HACK: THIS SHOULD NOT COME FROM CORESTYLE AND SHOULD INSTEAD BE DEFINED BY ENGINE TEXTURES/PROJECT SETTINGS
		DefaultSliderStyle = new FSliderStyle(FCoreStyle::Get().GetWidgetStyle<FSliderStyle>("Slider"));

		// Unlink UMG default colors from the editor settings colors.
		DefaultSliderStyle->UnlinkColors();
	}

	WidgetStyle = *DefaultSliderStyle;

	AccessibleBehavior = ESlateAccessibleBehavior::Summary;
	bCanChildrenBeAccessible = false;
}

TSharedRef<SWidget> USlider::RebuildWidget()
{
	MySlider = SNew(SSlider)
		.Style(&WidgetStyle)
		.IsFocusable(IsFocusable)
		.OnMouseCaptureBegin(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnMouseCaptureBegin))
		.OnMouseCaptureEnd(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnMouseCaptureEnd))
		.OnControllerCaptureBegin(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnControllerCaptureBegin))
		.OnControllerCaptureEnd(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnControllerCaptureEnd))
		.OnValueChanged(BIND_UOBJECT_DELEGATE(FOnFloatValueChanged, HandleOnValueChanged));

	return MySlider.ToSharedRef();
}

void USlider::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	TAttribute<float> ValueBinding = PROPERTY_BINDING(float, Value);
	
	MySlider->SetOrientation(Orientation);
	MySlider->SetMouseUsesStep(MouseUsesStep);
	MySlider->SetRequiresControllerLock(RequiresControllerLock);
	MySlider->SetSliderBarColor(SliderBarColor);
	MySlider->SetSliderHandleColor(SliderHandleColor);
	MySlider->SetValue(ValueBinding);
	MySlider->SetMinAndMaxValues(MinValue, MaxValue);
	MySlider->SetLocked(Locked);
	MySlider->SetIndentHandle(IndentHandle);
	MySlider->SetStepSize(StepSize);
}

void USlider::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MySlider.Reset();
}

void USlider::HandleOnValueChanged(float InValue)
{
	OnValueChanged.Broadcast(InValue);
}

void USlider::HandleOnMouseCaptureBegin()
{
	OnMouseCaptureBegin.Broadcast();
}

void USlider::HandleOnMouseCaptureEnd()
{
	OnMouseCaptureEnd.Broadcast();
}

void USlider::HandleOnControllerCaptureBegin()
{
	OnControllerCaptureBegin.Broadcast();
}

void USlider::HandleOnControllerCaptureEnd()
{
	OnControllerCaptureEnd.Broadcast();
}

float USlider::GetValue() const
{
	if ( MySlider.IsValid() )
	{
		return MySlider->GetValue();
	}

	return Value;
}

float USlider::GetNormalizedValue() const
{
	if (MySlider.IsValid())
	{
		return MySlider->GetNormalizedValue();
	}

	if (MinValue == MaxValue)
	{
		return 1.0f;
	}
	else
	{
		return (Value - MinValue) / (MaxValue - MinValue);
	}
}

void USlider::SetValue(float InValue)
{
	Value = InValue;
	if ( MySlider.IsValid() )
	{
		MySlider->SetValue(InValue);
	}
}

void USlider::SetMinValue(float InValue)
{
	MinValue = InValue;
	if (MySlider.IsValid())
	{
		// Because SSlider clamps min/max values upon setting them,
		// we have to send both values together to ensure that they
		// don't get out of sync.
		MySlider->SetMinAndMaxValues(MinValue, MaxValue);
	}
}

void USlider::SetMaxValue(float InValue)
{
	MaxValue = InValue;
	if (MySlider.IsValid())
	{
		MySlider->SetMinAndMaxValues(MinValue, MaxValue);
	}
}

void USlider::SetIndentHandle(bool InIndentHandle)
{
	IndentHandle = InIndentHandle;
	if ( MySlider.IsValid() )
	{
		MySlider->SetIndentHandle(InIndentHandle);
	}
}

void USlider::SetLocked(bool InLocked)
{
	Locked = InLocked;
	if ( MySlider.IsValid() )
	{
		MySlider->SetLocked(InLocked);
	}
}

void USlider::SetStepSize(float InValue)
{
	StepSize = InValue;
	if (MySlider.IsValid())
	{
		MySlider->SetStepSize(InValue);
	}
}

void USlider::SetSliderHandleColor(FLinearColor InValue)
{
	SliderHandleColor = InValue;
	if (MySlider.IsValid())
	{
		MySlider->SetSliderHandleColor(InValue);
	}
}

void USlider::SetSliderBarColor(FLinearColor InValue)
{
	SliderBarColor = InValue;
	if (MySlider.IsValid())
	{
		MySlider->SetSliderBarColor(InValue);
	}
}

#if WITH_ACCESSIBILITY
TSharedPtr<SWidget> USlider::GetAccessibleWidget() const
{
	return MySlider;
}
#endif

#if WITH_EDITOR

const FText USlider::GetPaletteCategory()
{
	return LOCTEXT("Common", "Common");
}

#endif

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
