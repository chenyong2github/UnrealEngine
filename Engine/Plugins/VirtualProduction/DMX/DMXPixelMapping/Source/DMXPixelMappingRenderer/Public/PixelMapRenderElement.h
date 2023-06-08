// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if defined(_MSC_VER) 
#include <atomic>
#else
#include "HAL/CriticalSection.h"
#endif
#include "Math/Color.h"
#include "Math/Vector2D.h"


namespace UE::DMXPixelMapping::Rendering
{
	/**
	 * Used in shader permutation for determining number of samples to use in texture blending.
	 * If adding to this you must also adjust the public facing option: 'EPixelBlendingQuality' under the runtime module's DMXPixelMappingOutputComponent.h
	 */
	enum class DMXPIXELMAPPINGRENDERER_API ECellBlendingQuality : uint8
	{
		Low,
		Medium,
		High,
		
		MAX
	};

	struct DMXPIXELMAPPINGRENDERER_API FPixelMapRenderElementParameters
	{
		/** Position in texels of the top left corner of the quad's UV's */
		FVector2D UV;

		/** Size in texels of the quad's total UV space */
		FVector2D UVSize;

		/** Size in texels of UV.May match UVSize */
		FVector2D UVCellSize;

		/** The quality of color samples in the pixel shader(number of samples) */
		ECellBlendingQuality CellBlendingQuality;

		/** Calculates the UV point to sample purely on the UV position/size. Works best for renderers which represent a single pixel */
		bool bStaticCalculateUV = true;
	};

	/** An element rendered by the pixel map renderer */
	class DMXPIXELMAPPINGRENDERER_API FPixelMapRenderElement
		: public TSharedFromThis<FPixelMapRenderElement>
	{
	public:
		FPixelMapRenderElement(FPixelMapRenderElementParameters InParameters)
			: Parameters(InParameters)
		{}

		virtual ~FPixelMapRenderElement() {};

		/** Sets the parameters, thread-safe */
		void SetParameters(FPixelMapRenderElementParameters InParameters);

		/** Gets the parameters, thread-safe */
		FPixelMapRenderElementParameters GetParameters() const;

		/** Sets the pixel color, thread safe. This is called from the renderer to set the rendered color. */
		void SetColor(const FLinearColor& Color);

		/** Gets the pixel color, thread safe */
		FLinearColor GetColor() const;

	private:
		// 5.3: Significantly better performance with atomics on MSVC. 
		// However not all compilers currently support non-lockfree atomics.
#if defined(_MSC_VER) 
		/** The current parameters */
		std::atomic<FPixelMapRenderElementParameters> Parameters;

		/** The current pixel color */
		std::atomic<FLinearColor> Color;
#else
		/** The current parameters */
		FPixelMapRenderElementParameters Parameters;

		/** The current pixel color */
		FLinearColor Color;

		/** Mutex acccess to parameters */
		mutable FCriticalSection AccessParametersMutex;

		/** Mutex acccess to parameters */
		mutable FCriticalSection AccessColorMutex;
#endif
	};
}
