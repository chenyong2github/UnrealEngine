// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDMXPixelMappingRenderInputTextureProxy.generated.h"

class UMaterialInstanceDynamic;
class UTexture;


/** Parameters to render the input texture */
USTRUCT(BlueprintType)
struct FDMXPixelMappingRenderInputTextureParameters
{
	GENERATED_BODY()

	/** Post process material applied to the rendered input */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PostProcessMID;

	/** If true, the post process material is applied each downsample pass, otherwise only once after the last pass */
	UPROPERTY(EditAnywhere, Category = "Post Process")
	bool bApplyPostProcessMaterialEachDownsamplePass = true;

	/** Number of times the pixelmapping input is downsampled */
	UPROPERTY(EditAnywhere, Category = "Post Process", Meta = (ClampMin = "0", ClampMax = "256", UIMin = "0", UIMax = "16"))
	int32 NumDownSamplePasses = 0;

	/** Blur distance applied, only applicable if the post process matierial has a "BlurDistance" parameter */
	UPROPERTY(EditAnywhere, Category = "Post Process", Meta = (UIMin = "0", UIMax = "1", SliderExponent = 2.0))
	float BlurDistance = 0.02f;

	/** Parameter name of the post process material to receive the input texture */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Post Process")
	FName InputTextureParameterName = "InputTexture";

	/** The blur distance parameter name */
	FName BlurDistanceParamtereName = "BlurDistance";

	/** Size of the rendered texture */
	FVector2D OutputSize{ 1.f, 1.f };
};

namespace UE::DMXPixelMapping::Rendering::Private
{
	/** Renders the input to be used by pixel mapping. */
	class IDMXPixelMappingRenderInputTextureProxy
		: public TSharedFromThis<IDMXPixelMappingRenderInputTextureProxy>
	{
	public:
		virtual ~IDMXPixelMappingRenderInputTextureProxy() {}

		/** Updates parameters */
		virtual void UpdateParameters(const FDMXPixelMappingRenderInputTextureParameters& InParameters) = 0;

		/** Renders the input texture, optionally using a post process material */
		virtual void Render() = 0;

		/** Returns the currently rendered texture, or nullptr if no texture was rendered yet */
		virtual UTexture* GetRenderedTexture() const = 0;
	};

}
