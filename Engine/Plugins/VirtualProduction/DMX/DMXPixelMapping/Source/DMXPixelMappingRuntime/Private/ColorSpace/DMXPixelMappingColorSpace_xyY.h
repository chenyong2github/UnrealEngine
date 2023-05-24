// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXAttribute.h"
#include "ColorSpace/DMXPixelMappingColorSpace.h"

#include "ColorSpace.h"

#include "DMXPixelMappingColorSpace_xyY.generated.h"


UCLASS(meta = (DisplayName = "CIE 1931 xyY"))
class DMXPIXELMAPPINGRUNTIME_API UDMXPixelMappingColorSpace_xyY
	: public UDMXPixelMappingColorSpace
{
	GENERATED_BODY()

public:
	/** Constructor */
	UDMXPixelMappingColorSpace_xyY();

	//~ Begin DMXPixelMappingColorSpace interface
	virtual void SetRGBA(const FLinearColor& InColor) override;
	//~ End DMXPixelMappingColorSpace interface

	/** Attribute sent for x */
	UPROPERTY(EditAnywhere, Category = "XY", Meta = (DisplayName = "x Attribute"))
	FDMXAttributeName XAttribute;

	/** Attribute sent for y */
	UPROPERTY(EditAnywhere, Category = "XY", Meta = (DisplayName = "y Attribute"))
	FDMXAttributeName YAttribute;

	/** Sets the range of the xyY color space. CIE 1931 is 100%. */
	UPROPERTY(EditAnywhere, Transient, Category = "XY", Meta = (ClampMin = 0.1, ClampMax = 1000.0, UIMin = 50.0, UIMax = 100.0, SliderExponent = 3, DisplayName = "Color Space Range"))
	float ColorSpaceRangePercents = 100.f;

	/** Attribute sent for Y */
	UPROPERTY(EditAnywhere, Category = "Luminance")
	FDMXAttributeName LuminanceAttribute;

	/** Min Luminance */
	UPROPERTY(EditAnywhere, Category = "Luminance", Meta = (ClampMin = 0.0, ClampMax = 1.0, UIMin = 0.0, UIMax = 1.0, EditConditionHides, EditCondition = "LuminanceType == EDMXPixelMappingLuminanceType_RGBCMY::FromWhite || LuminanceType == EDMXPixelMappingLuminanceType_RGBCMY::FromAlpha"))
	float MinLuminance = 0.f;

	/** Max Luminance */
	UPROPERTY(EditAnywhere, Category = "Luminance", Meta = (ClampMin = 0.0, ClampMax = 1.0, UIMin = 0.0, UIMax = 1.0, EditConditionHides, EditCondition = "LuminanceType == EDMXPixelMappingLuminanceType_RGBCMY::FromWhite || LuminanceType == EDMXPixelMappingLuminanceType_RGBCMY::FromAlpha"))
	float MaxLuminance = 1.f;

protected:
	//~ Begin UObject Interface
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

private:
	/** The actual range property */
	UPROPERTY()
	float ColorSpaceRange = 1.0;

	/** Cached sRGB color space, to avoid instantiating on each conversion */
	UE::Color::FColorSpace SRGBColorSpace;
};
