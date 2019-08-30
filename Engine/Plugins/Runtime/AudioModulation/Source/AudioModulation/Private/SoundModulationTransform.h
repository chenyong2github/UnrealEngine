// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
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

	/** Minimum value to clamp the input to. */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayName = "Input Min", UIMin = "0", UIMax = "1"))
	float InputMin;

	/** Maximum value to clamp the input to. */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayName = "Input Max", UIMin = "0", UIMax = "1"))
	float InputMax;

	/** Minimum value to clamp to map the output to. */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayName = "Output Min", UIMin = "0", UIMax = "1"))
	float OutputMin;

	/** Maximum value to clamp to map the output to. */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (DisplayName = "Output Max", UIMin = "0", UIMax = "1"))
	float OutputMax;

	/** Applies transform to provided value */
	void Apply(float& OutValue) const;
};

UENUM()
enum class ESoundModulatorOutputCurve
{
	Linear		UMETA(DisplayName = "Linear"),
	Exp			UMETA(DisplayName = "Exponential"),
	Exp_Inverse UMETA(DisplayName = "Exponential (Inverse)"),
	Log			UMETA(DisplayName = "Log"),
	Sin			UMETA(DisplayName = "Sin (Quarter)"),
	SCurve		UMETA(DisplayName = "Sin (S-Curve)"),
	Custom		UMETA(DisplayName = "Custom"),
	Count		UMETA(Hidden),
};

USTRUCT(BlueprintType)
struct FSoundModulationOutputTransform
{
	GENERATED_USTRUCT_BODY()

	FSoundModulationOutputTransform();

	/** Minimum value to clamp the input to. */
	UPROPERTY(EditAnywhere, Category = Input, BlueprintReadWrite, meta = (DisplayName = "Input Min", UIMin = "0", UIMax = "1"))
	float InputMin;

	/** Maximum value to clamp the input to. */
	UPROPERTY(EditAnywhere, Category = Input, BlueprintReadWrite, meta = (DisplayName = "Input Max", UIMin = "0", UIMax = "1"))
	float InputMax;

	/** The curve to apply when transforming the output. */
	UPROPERTY(EditAnywhere, Category = Curve, BlueprintReadWrite)
	ESoundModulatorOutputCurve Curve;

	/** When curve set to log, exponential or exponential inverse, value is factor 'b' in following equations with output 'y' and input 'x':
	 *  Exponential: y = x * 10^-b(1-x)
	 *  Exponential (Inverse): y = ((x - 1) * 10^(-bx)) + 1
	 *  Logarithmic: y = b * log(x) + 1
	 */
	UPROPERTY(EditAnywhere, Category = Input, BlueprintReadWrite, meta = (DisplayName = "Exponential Scalar", ClampMin = "0", ClampMax = "10", UIMin = "0", UIMax = "10"))
	float Scalar;

	/** Custom curve to apply if output curve type is set to custom. */
	UPROPERTY(EditAnywhere, Category = Curve, BlueprintReadWrite)
	FRuntimeFloatCurve FloatCurve;

	/** Minimum value to clamp to map the output to. */
	UPROPERTY(EditAnywhere, Category = Output, BlueprintReadWrite)
	float OutputMin;

	/** Maximum value to clamp to map the output to. */
	UPROPERTY(EditAnywhere, Category = Output, BlueprintReadWrite)
	float OutputMax;

	/** Applies transform to provided value */
	void Apply(float& OutValue) const;

private:
	void EvaluateCurve(float& Value) const;
};
