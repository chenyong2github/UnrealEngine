// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioSliderStyle.h"
#include "Components/Widget.h"
#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Styling/SlateTypes.h"
#include "UObject/ObjectMacros.h"
#include "Widgets/SWidget.h"
#include "AudioSlider.generated.h"

class SAudioSliderBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFloatValueChangedEvent, float, Value);

/**
 * An audio slider widget. 
 */
UCLASS(Abstract)
class AUDIOWIDGETS_API UAudioSliderBase: public UWidget
{
	GENERATED_UCLASS_BODY()

public:
	/** The linear value. */
	UPROPERTY(EditAnywhere, Category = Appearance, meta = (UIMin = "0", UIMax = "1"))
	float Value;

	/** Whether to show text label. */
	UPROPERTY(EditAnywhere, Category = Appearance)
	bool AlwaysShowLabel;

	/** Whether to show text label. */
	UPROPERTY(EditAnywhere, Category = Appearance)
	bool ShowUnitsText;

	/** A bindable delegate to allow logic to drive the value of the widget */
	UPROPERTY()
	FGetFloat ValueDelegate;

	/** The color to draw the label background. */
	UPROPERTY(EditAnywhere, Category = Appearance)
	FLinearColor LabelBackgroundColor;

	/** A bindable delegate for the LabelBackgroundColor. */
	UPROPERTY()
	FGetLinearColor LabelBackgroundColorDelegate;

	/** The color to draw the slider background. */
	UPROPERTY(EditAnywhere, Category = Appearance)
	FLinearColor SliderBackgroundColor;

	/** A bindable delegate for the SliderBackgroundColor. */
	UPROPERTY()
	FGetLinearColor SliderBackgroundColorDelegate;

	/** The color to draw the slider bar. */
	UPROPERTY(EditAnywhere, Category = Appearance)
	FLinearColor SliderBarColor;

	/** A bindable delegate for the SliderBarColor. */
	UPROPERTY()
	FGetLinearColor SliderBarColorDelegate;

	/** The color to draw the slider thumb. */
	UPROPERTY(EditAnywhere, Category = Appearance)
	FLinearColor SliderThumbColor;

	/** A bindable delegate for the SliderThumbColor. */
	UPROPERTY()
	FGetLinearColor SliderThumbColorDelegate;

	/** The color to draw the widget background. */
	UPROPERTY(EditAnywhere, Category = Appearance)
	FLinearColor WidgetBackgroundColor;

	/** A bindable delegate for the WidgetBackgroundColor. */
	UPROPERTY()
	FGetLinearColor WidgetBackgroundColorDelegate;

public:
	/** Set the units text (ex. "dB") */
	UFUNCTION(BlueprintCallable, Category = "Behavior")
	void SetUnitsText(const FText UnitsText);

	/** Get output value from linear based on internal lin to output mapping. */
	UFUNCTION(BlueprintCallable, Category = "Behavior")
	float GetOutputValue(const float LinValue);

	/** Get linear value from output based on internal lin to output mapping. */
	UFUNCTION(BlueprintCallable, Category = "Behavior")
	float GetLinValue(const float OutputValue);

	/** Sets whether editable value and units text is read only or not. */
 	UFUNCTION(BlueprintCallable, Category = "Behavior")
 	void SetAllTextReadOnly(const bool bIsReadOnly);
	
	/** Sets whether to show the units text. */
	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetShowUnitsText(const bool bShowUnitsText);

	/** Sets whether editable value text is read only or not. */
	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetAlwaysShowLabel(const bool bSetAlwaysShowLabel);

	/** The slider's orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Appearance)
	TEnumAsByte<EOrientation> Orientation;

	/** Called when the value is changed by slider or typing. */
	UPROPERTY(BlueprintAssignable, Category = "Widget Event")
	FOnFloatValueChangedEvent OnValueChanged;

	/** Sets the label background color */
	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetLabelBackgroundColor(FLinearColor InValue);

	/** Sets the slider background color */
	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetSliderBackgroundColor(FLinearColor InValue);

	/** Sets the slider bar color */
	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetSliderBarColor(FLinearColor InValue);

	/** Sets the slider thumb color */
	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetSliderThumbColor(FLinearColor InValue);

	/** Sets the widget background color */
	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetWidgetBackgroundColor(FLinearColor InValue);

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
	TSharedPtr<SAudioSliderBase> MyAudioSlider;

	void HandleOnValueChanged(float InValue);

	// UWidget interface
	virtual TSharedRef<SWidget> RebuildWidget();
	// End of UWidget interface

	PROPERTY_BINDING_IMPLEMENTATION(float, Value);
	PROPERTY_BINDING_IMPLEMENTATION(FLinearColor, LabelBackgroundColor);
	PROPERTY_BINDING_IMPLEMENTATION(FLinearColor, SliderBackgroundColor);
	PROPERTY_BINDING_IMPLEMENTATION(FLinearColor, SliderBarColor);
	PROPERTY_BINDING_IMPLEMENTATION(FLinearColor, SliderThumbColor);
	PROPERTY_BINDING_IMPLEMENTATION(FLinearColor, WidgetBackgroundColor);
};

/**
 * An audio slider widget with customizable curves.
 */
UCLASS()
class AUDIOWIDGETS_API UAudioSlider : public UAudioSliderBase
{
	GENERATED_UCLASS_BODY()

	/** Curves for mapping linear to output values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	TWeakObjectPtr<const UCurveFloat> LinToOutputCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	TWeakObjectPtr <const UCurveFloat> OutputToLinCurve;

protected:
	virtual void SynchronizeProperties() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
};

/**
 * An audio slider widget with default customizable curves for volume (dB).
 */
UCLASS()
class AUDIOWIDGETS_API UAudioVolumeSlider : public UAudioSlider
{
	GENERATED_UCLASS_BODY()
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
};

/**
 * An audio slider widget, for use with frequency. 
 */
UCLASS()
class AUDIOWIDGETS_API UAudioFrequencySlider : public UAudioSliderBase
{
	GENERATED_UCLASS_BODY()
	
	/** Frequency output range */
	UPROPERTY(EditAnywhere, Category = Behavior)
	FVector2D OutputRange = FVector2D(20.0f, 20000.0f);
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
};
