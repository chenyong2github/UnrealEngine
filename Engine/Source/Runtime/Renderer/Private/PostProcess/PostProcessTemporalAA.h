// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScreenPass.h"

class FSceneTextureParameters;
struct FTemporalAAHistory;

/** List of TAA configurations. */
enum class ETAAPassConfig
{
	// Permutations for main scene color TAA.
	Main,
	MainUpsampling,
	MainSuperSampling,

	// Permutation for SSR noise accumulation.
	ScreenSpaceReflections,
	
	// Permutation for light shaft noise accumulation.
	LightShaft,

	// Permutation for DOF that handle Coc.
	DiaphragmDOF,
	DiaphragmDOFUpsampling,

	MAX
};

bool IsTemporalAASceneDownsampleAllowed(const FViewInfo& View);

static FORCEINLINE bool IsTAAUpsamplingConfig(ETAAPassConfig Pass)
{
	return Pass == ETAAPassConfig::MainUpsampling || Pass == ETAAPassConfig::DiaphragmDOFUpsampling || Pass == ETAAPassConfig::MainSuperSampling;
}

static FORCEINLINE bool IsMainTAAConfig(ETAAPassConfig Pass)
{
	return Pass == ETAAPassConfig::Main || Pass == ETAAPassConfig::MainUpsampling || Pass == ETAAPassConfig::MainSuperSampling;
}

static FORCEINLINE bool IsDOFTAAConfig(ETAAPassConfig Pass)
{
	return Pass == ETAAPassConfig::DiaphragmDOF || Pass == ETAAPassConfig::DiaphragmDOFUpsampling;
}

/** GPU Output of the TAA pass. */
struct FTAAOutputs
{
	// Anti aliased scene color.
	// Can have alpha channel, or CoC for DOF.
	FRDGTexture* SceneColor = nullptr;

	// Optional information that get anti aliased, such as separate CoC for DOF.
	FRDGTexture* SceneMetadata = nullptr;

	// Optional scene color output at half the resolution.
	FRDGTexture* DownsampledSceneColor = nullptr;
};


/** Configuration of TAA. */
struct FTAAPassParameters
{
	// TAA pass to run.
	ETAAPassConfig Pass = ETAAPassConfig::Main;

	// Whether to use the faster shader permutation.
	bool bUseFast = false;

	// Whether output texture should be render targetable.
	bool bOutputRenderTargetable = false;

	// Whether downsampled (box filtered, half resolution) frame should be written out.
	bool bDownsample = false;
	EPixelFormat DownsampleOverrideFormat = PF_Unknown;

	// Viewport rectangle of the input and output of TAA at ResolutionDivisor == 1.
	FIntRect InputViewRect;
	FIntRect OutputViewRect;

	// Resolution divisor.
	int32 ResolutionDivisor = 1;

	// Anti aliased scene color.
	// Can have alpha channel, or CoC for DOF.
	FRDGTexture* SceneColorInput = nullptr;

	// Optional information that get anti aliased, such as separate CoC for DOF.
	FRDGTexture* SceneMetadataInput = nullptr;


	FTAAPassParameters(const FViewInfo& View)
		: InputViewRect(View.ViewRect)
		, OutputViewRect(View.ViewRect)
	{ }


	// Customizes the view rectangles for input and output.
	FORCEINLINE void SetupViewRect(const FViewInfo& View, int32 InResolutionDivisor = 1)
	{
		ResolutionDivisor = InResolutionDivisor;

		InputViewRect = View.ViewRect;

		// When upsampling, always upsampling to top left corner to reuse same RT as before upsampling.
		if (IsTAAUpsamplingConfig(Pass))
		{
			OutputViewRect.Min = FIntPoint(0, 0);
			OutputViewRect.Max =  View.GetSecondaryViewRectSize();
		}
		else
		{
			OutputViewRect = InputViewRect;
		}
	}

	// Shifts input and output view rect to top left corner
	FORCEINLINE void TopLeftCornerViewRects()
	{
		InputViewRect.Max -= InputViewRect.Min;
		InputViewRect.Min = FIntPoint::ZeroValue;
		OutputViewRect.Max -= OutputViewRect.Min;
		OutputViewRect.Min = FIntPoint::ZeroValue;
	}
	
	/** Returns the texture resolution that will be output. */
	FIntPoint GetOutputExtent() const;

	/** Validate the settings of TAA, to make sure there is no issue. */
	bool Validate() const;
};

/** Temporal AA pass which emits a filtered scene color and new history. */
FTAAOutputs AddTemporalAAPass(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FTAAPassParameters& Inputs,
	const FTemporalAAHistory& InputHistory,
	FTemporalAAHistory* OutputHistory);

/** Temporal AA helper method which performs filtering on the main pass scene color. Supports upsampled history and,
 *  if requested, will attempt to perform the scene color downsample. Returns the filtered scene color, the downsampled
 *  scene color (or null if it was not performed), and the secondary view rect.
 */
void AddTemporalAAPass(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const bool bAllowDownsampleSceneColor,
	const EPixelFormat DownsampleOverrideFormat,
	FRDGTextureRef InSceneColorTexture,
	FRDGTextureRef* OutSceneColorTexture,
	FIntRect* OutSceneColorViewRect,
	FRDGTextureRef* OutSceneColorHalfResTexture,
	FIntRect* OutSceneColorHalfResViewRect);