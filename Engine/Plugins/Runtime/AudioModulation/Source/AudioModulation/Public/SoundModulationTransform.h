// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "SoundModulationTransform.generated.h"


USTRUCT(BlueprintType)
struct FSoundModulationInputTransform
{
	GENERATED_USTRUCT_BODY()

	FSoundModulationInputTransform();

	/** Minimum value to clamp the input to prior to transforming via linear interpolation. */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayName = "Input Min", UIMin = "0", UIMax = "1"))
	float InputMin;

	/** Maximum value to clamp the input to prior to transforming via linear interpolation. */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayName = "Input Max", UIMin = "0", UIMax = "1"))
	float InputMax;

	/** Minimum value to scale the output to. */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayName = "Output Min", UIMin = "0", UIMax = "1"))
	float OutputMin;

	/** Maximum value to scale the output to. */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayName = "Output Max", UIMin = "0", UIMax = "1"))
	float OutputMax;

	/** Applies transform to provided value */
	void Apply(float& OutValue) const;
};

UENUM()
enum class ESoundModulatorOutputCurve : uint8
{
	// Expressions
	Linear		UMETA(DisplayName = "Linear"),
	Exp			UMETA(DisplayName = "Exponential"),
	Exp_Inverse UMETA(DisplayName = "Exponential (Inverse)"),
	Log			UMETA(DisplayName = "Log"),
	Sin			UMETA(DisplayName = "Sin (Quarter)"),
	SCurve		UMETA(DisplayName = "Sin (S-Curve)"),

	// Reference a shared curve asset
	Shared		UMETA(DisplayName = "Shared"),

	// Design a custom curve unique to the owning transform
	Custom		UMETA(DisplayName = "Custom"),

	Count		UMETA(Hidden),
};

USTRUCT(BlueprintType)
struct AUDIOMODULATION_API FSoundModulationOutputTransform
{
	GENERATED_USTRUCT_BODY()

	/** Minimum value to clamp the input to. */
	UPROPERTY()
	float InputMin = 0.0f;

	/** Maximum value to clamp the input to. */
	UPROPERTY()
	float InputMax = 1.0f;

	/** The curve to apply when transforming the output. */
	UPROPERTY(EditAnywhere, Category = Input, BlueprintReadWrite, meta = (DisplayName = "Transform Curve"))
	ESoundModulatorOutputCurve Curve = ESoundModulatorOutputCurve::Linear;

	/** When curve set to log, exponential or exponential inverse, value is factor 'b' in following equations with output 'y' and input 'x':
	 *  Exponential: y = x * 10^-b(1-x)
	 *  Exponential (Inverse): y = ((x - 1) * 10^(-bx)) + 1
	 *  Logarithmic: y = b * log(x) + 1
	 */
	UPROPERTY(EditAnywhere, Category = Input, BlueprintReadWrite, meta = (DisplayName = "Exponential Scalar", ClampMin = "0.1", ClampMax = "10.0", UIMin = "0.1", UIMax = "10.0"))
	float Scalar = 2.5;

	/** Custom curve to apply if output curve type is set to 'Custom.' */
	UPROPERTY()
	FRichCurve CurveCustom;

	/** Asset curve reference to apply if output curve type is set to 'Shared.' */
	UPROPERTY(EditAnywhere, meta = (DisplayName = "Asset"), Category = Curve, BlueprintReadWrite)
	UCurveFloat* CurveShared = nullptr;

	/** Minimum value to clamp output to. */
	UPROPERTY()
	float OutputMin = 0.0f;

	/** Maximum value to clamp output to. */
	UPROPERTY()
	float OutputMax = 1.0f;

	/** Applies transform to provided value */
	void Apply(float& OutValue) const;

private:
	void EvaluateCurve(float& Value) const;
};
