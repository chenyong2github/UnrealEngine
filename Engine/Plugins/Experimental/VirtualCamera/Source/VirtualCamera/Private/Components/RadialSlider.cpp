// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/RadialSlider.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SRadialSlider.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// URadialSlider

static FSliderStyle* DefaultSliderStyle = nullptr;

URadialSlider::URadialSlider(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MinValue = 0.0f;
	MaxValue = 1.0f;
	SliderHandleStartAngle = 60.0f;
	SliderHandleEndAngle = 300.0f;
	AngularOffset = 0.0f;
	ValueRemapCurve = nullptr;
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

#if WITH_EDITORONLY_DATA
	AccessibleBehavior = ESlateAccessibleBehavior::Summary;
	bCanChildrenBeAccessible = false;
#endif
}

TSharedRef<SWidget> URadialSlider::RebuildWidget()
{
	MyRadialSlider = SNew(SRadialSlider)
		.Style(&WidgetStyle)
		.IsFocusable(IsFocusable)
		.OnMouseCaptureBegin(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnMouseCaptureBegin))
		.OnMouseCaptureEnd(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnMouseCaptureEnd))
		.OnControllerCaptureBegin(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnControllerCaptureBegin))
		.OnControllerCaptureEnd(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnControllerCaptureEnd))
		.OnValueChanged(BIND_UOBJECT_DELEGATE(FOnFloatValueChanged, HandleOnValueChanged));

	return MyRadialSlider.ToSharedRef();
}

void URadialSlider::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	TAttribute<float> ValueBinding = PROPERTY_BINDING(float, Value);
	
	MyRadialSlider->SetMouseUsesStep(MouseUsesStep);
	MyRadialSlider->SetRequiresControllerLock(RequiresControllerLock);
	MyRadialSlider->SetSliderBarColor(SliderBarColor);
	MyRadialSlider->SetSliderHandleColor(SliderHandleColor);
	MyRadialSlider->SetValue(ValueBinding);
	MyRadialSlider->SetMinAndMaxValues(MinValue, MaxValue);
	MyRadialSlider->SetSliderHandleStartAngleAndSliderHandleEndAngle(SliderHandleStartAngle, SliderHandleEndAngle);
	MyRadialSlider->SetAngularOffset(AngularOffset);
	MyRadialSlider->SetValueRemapCurve(ValueRemapCurve);
	MyRadialSlider->SetLocked(Locked);
	MyRadialSlider->SetIndentHandle(IndentHandle);
	MyRadialSlider->SetStepSize(StepSize);
}

void URadialSlider::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyRadialSlider.Reset();
}

void URadialSlider::HandleOnValueChanged(float InValue)
{
	OnValueChanged.Broadcast(InValue);
}

void URadialSlider::HandleOnMouseCaptureBegin()
{
	OnMouseCaptureBegin.Broadcast();
}

void URadialSlider::HandleOnMouseCaptureEnd()
{
	OnMouseCaptureEnd.Broadcast();
}

void URadialSlider::HandleOnControllerCaptureBegin()
{
	OnControllerCaptureBegin.Broadcast();
}

void URadialSlider::HandleOnControllerCaptureEnd()
{
	OnControllerCaptureEnd.Broadcast();
}

float URadialSlider::GetValue() const
{
	if (MyRadialSlider.IsValid() )
	{
		return MyRadialSlider->GetValue();
	}

	return Value;
}

float URadialSlider::GetNormalizedValue() const
{
	if (MyRadialSlider.IsValid())
	{
		return MyRadialSlider->GetNormalizedValue();
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

void URadialSlider::SetValue(float InValue)
{
	Value = InValue;
	if (MyRadialSlider.IsValid() )
	{
		MyRadialSlider->SetValue(InValue);
	}
}

void URadialSlider::SetMinValue(float InValue)
{
	MinValue = InValue;
	if (MyRadialSlider.IsValid())
	{
		// Because SRadialSlider clamps min/max values upon setting them,
		// we have to send both values together to ensure that they
		// don't get out of sync.
		MyRadialSlider->SetMinAndMaxValues(MinValue, MaxValue);
	}
}

void URadialSlider::SetMaxValue(float InValue)
{
	MaxValue = InValue;
	if (MyRadialSlider.IsValid())
	{
		MyRadialSlider->SetMinAndMaxValues(MinValue, MaxValue);
	}
}

void URadialSlider::SetSliderHandleStartAngle(float InValue)
{
	SliderHandleStartAngle = InValue;
	if (MyRadialSlider.IsValid())
	{
		// Because SRadialSlider clamps min/max values upon setting them,
		// we have to send both values together to ensure that they
		// don't get out of sync.
		MyRadialSlider->SetMinAndMaxValues(SliderHandleStartAngle, SliderHandleEndAngle);
	}
}

void URadialSlider::SetSliderHandleEndAngle(float InValue)
{
	SliderHandleEndAngle = InValue;
	if (MyRadialSlider.IsValid())
	{
		MyRadialSlider->SetMinAndMaxValues(SliderHandleStartAngle, SliderHandleEndAngle);
	}
}

void URadialSlider::SetAngularOffset(float InValue)
{
	AngularOffset = InValue;
	if (MyRadialSlider.IsValid())
	{
		MyRadialSlider->SetAngularOffset(AngularOffset);
	}
}

void URadialSlider::SetValueRemapCurve(UCurveFloat* InValueRemapCurve)
{
	ValueRemapCurve = InValueRemapCurve;
	if (MyRadialSlider.IsValid())
	{
		MyRadialSlider->SetValueRemapCurve(ValueRemapCurve);
	}
}

void URadialSlider::SetIndentHandle(bool InIndentHandle)
{
	IndentHandle = InIndentHandle;
	if (MyRadialSlider.IsValid() )
	{
		MyRadialSlider->SetIndentHandle(InIndentHandle);
	}
}

void URadialSlider::SetLocked(bool InLocked)
{
	Locked = InLocked;
	if (MyRadialSlider.IsValid() )
	{
		MyRadialSlider->SetLocked(InLocked);
	}
}

void URadialSlider::SetStepSize(float InValue)
{
	StepSize = InValue;
	if (MyRadialSlider.IsValid())
	{
		MyRadialSlider->SetStepSize(InValue);
	}
}

void URadialSlider::SetSliderHandleColor(FLinearColor InValue)
{
	SliderHandleColor = InValue;
	if (MyRadialSlider.IsValid())
	{
		MyRadialSlider->SetSliderHandleColor(InValue);
	}
}

void URadialSlider::SetSliderBarColor(FLinearColor InValue)
{
	SliderBarColor = InValue;
	if (MyRadialSlider.IsValid())
	{
		MyRadialSlider->SetSliderBarColor(InValue);
	}
}

#if WITH_ACCESSIBILITY
TSharedPtr<SWidget> URadialSlider::GetAccessibleWidget() const
{
	return MyRadialSlider;
}
#endif

#if WITH_EDITOR

const FText URadialSlider::GetPaletteCategory()
{
	return LOCTEXT("Common", "Common");
}

#endif

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
