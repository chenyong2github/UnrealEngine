// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Math/IntPoint.h"
#include "Math/Vector2D.h"


namespace UE::DMXPixelMapping::Rendering::PixelMap::Private
{
	/**
	 * Used in shader permutation for determining number of samples to use in texture blending.
	 * If adding to this you must also adjust the public facing option: 'EPixelBlendingQuality' under the runtime module's DMXPixelMappingOutputComponent.h
	 */
	enum class ECellBlendingQuality : uint8
	{
		Low,
		Medium,
		High,

		MAX
	};

	struct FPixelMapRendererParams
	{
		/** Position in screen pixels of the top left corner of the quad */
		FIntPoint Position;

		/** Position in texels of the top left corner of the quad's UV's */
		FVector2D UV;

		/** Size in texels of the quad's total UV space */
		FVector2D UVSize;

		/** Size in texels of UV.May match UVSize */
		FVector2D UVCellSize;

		/** The quality of color samples in the pixel shader(number of samples) */
		ECellBlendingQuality CellBlendingQuality;

		/** Calculates the UV point to sample purely on the UV position/size. Works best for renderers which represent a single pixel */
		bool bStaticCalculateUV;
	};
}
