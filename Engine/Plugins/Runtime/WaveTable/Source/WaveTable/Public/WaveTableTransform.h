// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "HAL/Platform.h"
#include "Templates/Function.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "WaveTableSettings.h"

#include "WaveTableTransform.generated.h"


USTRUCT(BlueprintType)
struct WAVETABLE_API FWaveTableTransform
{
	GENERATED_USTRUCT_BODY()

	/** The curve to apply when transforming the output. */
	UPROPERTY(EditAnywhere, Category = Input, BlueprintReadWrite, meta = (DisplayName = "Curve Type"))
	EWaveTableCurve Curve = EWaveTableCurve::Linear;

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

	UPROPERTY()
	TArray<float> WaveTable;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = WaveTable)
	FWaveTableSettings WaveTableSettings;
#endif // WITH_EDITORONLY_DATA

	void Apply(float& InOutValue, bool bInBipolar = false) const;

	// Builds a single period of a WaveTable using this transform in place of the provided ArrayView.
	// Size of the WaveTable is dictated by the provided ArrayView's size.
	void BuildWaveTable(TArrayView<float> InOutTable, bool bInBipolar) const;

#if WITH_EDITOR
	// No-ops if curve is set to WaveTable.
	void CacheWaveTable(EWaveTableResolution InResolution, bool bInBipolar);
#endif // WITH_EDITOR

	/** Caches curve data.  Should a shared curve be selected, curve points are copied locally
	  * to CurveCustom and Curve type is set accordingly.  Can be used to both avoid keeping a
	  * uobject reference should the transform be used on a non-game thread and potentially to
	  * further customize a referenced curve locally.
	  */
	void CacheCurve();

private:
	/** Clamps & applies transform to provided values as bipolar signal*/
	void SampleCurveBipolar(TArrayView<float> InOutValues) const;

	/** Clamps & applies transform to provided values as unipolar signal */
	void SampleCurveUnipolar(TArrayView<float> InOutValues) const;
};
