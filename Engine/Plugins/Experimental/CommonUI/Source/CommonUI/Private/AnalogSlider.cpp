// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnalogSlider.h"
#include "CommonUITypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// UAnalogSlider

UAnalogSlider::UAnalogSlider(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Orientation = EOrientation::Orient_Horizontal;
	SliderBarColor = FLinearColor::White;
	SliderHandleColor = FLinearColor::White;
	StepSize = 0.01f;
	SSlider::FArguments Defaults;
	WidgetStyle = *Defaults._Style;
	IsFocusable = true;
}

TSharedRef<SWidget> UAnalogSlider::RebuildWidget()
{
	MySlider = MyAnalogSlider = SNew(SAnalogSlider)
		.Style(&WidgetStyle)
		.IsFocusable(IsFocusable)
		.OnMouseCaptureBegin(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnMouseCaptureBegin))
		.OnMouseCaptureEnd(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnMouseCaptureEnd))
		.OnControllerCaptureBegin(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnControllerCaptureBegin))
		.OnControllerCaptureEnd(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnControllerCaptureEnd))
		.OnValueChanged(BIND_UOBJECT_DELEGATE(FOnFloatValueChanged, HandleOnValueChanged))
		.OnAnalogCapture(BIND_UOBJECT_DELEGATE(FOnFloatValueChanged, HandleOnAnalogCapture));

	
	if (UCommonInputSubsystem* CommonInputSubsystem = UCommonInputSubsystem::Get(GetOwningLocalPlayer()))
	{
		MyAnalogSlider->SetUsingGamepad(CommonInputSubsystem->GetCurrentInputType() == ECommonInputType::Gamepad);
		CommonInputSubsystem->OnInputMethodChangedNative.AddUObject(this, &UAnalogSlider::HandleInputMethodChanged);
	}

	return MySlider.ToSharedRef();
}

void UAnalogSlider::SynchronizeProperties()
{
	Super::SynchronizeProperties();
}

void UAnalogSlider::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyAnalogSlider.Reset();
	if (UCommonInputSubsystem* CommonInputSubsystem = UCommonInputSubsystem::Get(GetOwningLocalPlayer()))
	{
		CommonInputSubsystem->OnInputMethodChangedNative.RemoveAll(this);
	}
}

void UAnalogSlider::HandleOnAnalogCapture(float InValue)
{
	OnAnalogCapture.Broadcast(InValue);
}

void UAnalogSlider::HandleInputMethodChanged(ECommonInputType CurrentInputType)
{
	MyAnalogSlider->SetUsingGamepad(CurrentInputType == ECommonInputType::Gamepad);
}

#undef LOCTEXT_NAMESPACE
