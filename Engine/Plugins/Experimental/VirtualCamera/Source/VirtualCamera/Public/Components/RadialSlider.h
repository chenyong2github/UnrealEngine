// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SWidget.h"
#include "Components/Widget.h"
#include "RadialSlider.generated.h"

class SRadialSlider;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMouseCaptureBeginEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMouseCaptureEndEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnControllerCaptureBeginEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnControllerCaptureEndEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFloatValueChangedEvent, float, Value);

/**
 * A simple widget that shows a sliding bar with a handle that allows you to control the value between 0..1.
 *
 * * No Children
 */

UCLASS()
class VIRTUALCAMERA_API URadialSlider : public UWidget
{
	GENERATED_UCLASS_BODY()

public:
	/** The volume value to display. */
	UPROPERTY(EditAnywhere, Category=Appearance, meta=(UIMin="0", UIMax="1"))
	float Value;

	/** A bindable delegate to allow logic to drive the value of the widget */
	UPROPERTY()
	FGetFloat ValueDelegate;

	/** The minimum value the slider can be set to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Appearance)
	float MinValue;

	/** The maximum value the slider can be set to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Appearance)
	float MaxValue;

	/** The angle at which the Slider Handle will start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=( ClampMin = "0", ClampMax = "360" ), Category = Appearance)
	float SliderHandleStartAngle;

	/** The angle at which the Slider Handle will end. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0", ClampMax = "360"), Category = Appearance)
	float SliderHandleEndAngle;

	/** Rotates radial slider by arbitrary offset to support full gamut of configurations. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0", ClampMax = "360"), Category = Appearance)
	float AngularOffset;

	/** Use to sample values from the radial slider non-linearly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Appearance)
	UCurveFloat* ValueRemapCurve;

public:
	
	/** The progress bar style */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Style", meta=( DisplayName="Style" ))
	FSliderStyle WidgetStyle;

	/** The color to draw the slider bar in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Appearance)
	FLinearColor SliderBarColor;

	/** The color to draw the slider handle in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Appearance)
	FLinearColor SliderHandleColor;

	/** Whether the slidable area should be indented to fit the handle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Appearance, AdvancedDisplay)
	bool IndentHandle;

	/** Whether the handle is interactive or fixed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Appearance, AdvancedDisplay)
	bool Locked;

	/** Sets new value if mouse position is greater/less than half the step size. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Appearance, AdvancedDisplay)
	bool MouseUsesStep;

	/** Sets whether we have to lock input to change the slider value. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Appearance, AdvancedDisplay)
	bool RequiresControllerLock;

	/** The amount to adjust the value by, when using a controller or keyboard */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Appearance, meta=(UIMin="0", UIMax="1"))
	float StepSize;

	/** Should the slider be focusable? */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Interaction")
	bool IsFocusable;

public:

	/** Invoked when the mouse is pressed and a capture begins. */
	UPROPERTY(BlueprintAssignable, Category="Widget Event")
	FOnMouseCaptureBeginEvent OnMouseCaptureBegin;

	/** Invoked when the mouse is released and a capture ends. */
	UPROPERTY(BlueprintAssignable, Category="Widget Event")
	FOnMouseCaptureEndEvent OnMouseCaptureEnd;

	/** Invoked when the controller capture begins. */
	UPROPERTY(BlueprintAssignable, Category = "Widget Event")
	FOnControllerCaptureBeginEvent OnControllerCaptureBegin;

	/** Invoked when the controller capture ends. */
	UPROPERTY(BlueprintAssignable, Category = "Widget Event")
	FOnControllerCaptureEndEvent OnControllerCaptureEnd;

	/** Called when the value is changed by slider or typing. */
	UPROPERTY(BlueprintAssignable, Category="Widget Event")
	FOnFloatValueChangedEvent OnValueChanged;

	/** Gets the current value of the slider. */
	UFUNCTION(BlueprintCallable, Category="Behavior")
	float GetValue() const;

	/** Get the current raw slider alpha from 0 to 1 */
	UFUNCTION(BlueprintCallable, Category = "Behavior")
	float GetNormalizedValue() const;

	/** Sets the current value of the slider. */
	UFUNCTION(BlueprintCallable, Category="Behavior")
	void SetValue(float InValue);

	/** Sets the minimum value of the slider. */
	UFUNCTION(BlueprintCallable, Category = "Behavior")
	void SetMinValue(float InValue);

	/** Sets the maximum value of the slider. */
	UFUNCTION(BlueprintCallable, Category = "Behavior")
	void SetMaxValue(float InValue);

	/** Sets the minimum value of the slider. */
	UFUNCTION(BlueprintCallable, Category = "Behavior")
	void SetSliderHandleStartAngle(float InValue);

	/** Sets the maximum value of the slider. */
	UFUNCTION(BlueprintCallable, Category = "Behavior")
	void SetSliderHandleEndAngle(float InValue);

	/** Sets the Angular Offset for the slider. */
	UFUNCTION(BlueprintCallable, Category = "Behaviour")
	void SetAngularOffset(float InValue);

	/** Sets the Remap Curve for the slider. */
	UFUNCTION(BlueprintCallable, Category = "Behaviour")
	void SetValueRemapCurve(UCurveFloat* InValueRemapCurve);

	/** Sets if the slidable area should be indented to fit the handle */
	UFUNCTION(BlueprintCallable, Category="Behavior")
	void SetIndentHandle(bool InValue);

	/** Sets the handle to be interactive or fixed */
	UFUNCTION(BlueprintCallable, Category="Behavior")
	void SetLocked(bool InValue);

	/** Sets the amount to adjust the value by, when using a controller or keyboard */
	UFUNCTION(BlueprintCallable, Category="Behavior")
	void SetStepSize(float InValue);

	/** Sets the color of the slider bar */
	UFUNCTION(BlueprintCallable, Category="Appearance")
	void SetSliderBarColor(FLinearColor InValue);

	/** Sets the color of the handle bar */
	UFUNCTION(BlueprintCallable, Category="Appearance")
	void SetSliderHandleColor(FLinearColor InValue);
	
	// UWidget interface
	virtual void SynchronizeProperties() override;
	// End of UWidget interface

	// UVisual interface
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	// End of UVisual interface

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	/** Native Slate Widget */
	TSharedPtr<SRadialSlider> MyRadialSlider;

	// UWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	// End of UWidget interface

	void HandleOnValueChanged(float InValue);
	void HandleOnMouseCaptureBegin();
	void HandleOnMouseCaptureEnd();
	void HandleOnControllerCaptureBegin();
	void HandleOnControllerCaptureEnd();

#if WITH_ACCESSIBILITY
	virtual TSharedPtr<SWidget> GetAccessibleWidget() const override;
#endif

	PROPERTY_BINDING_IMPLEMENTATION(float, Value);
};
