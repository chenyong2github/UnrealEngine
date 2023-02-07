// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "UObject/ObjectMacros.h"

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_3
#include "CoreMinimal.h"
#endif

class FMaterial;
class FPrimitiveSceneProxy;
class FRDGBuilder;
class FSceneView;
class FSceneViewFamily;
class FShaderParametersMetadata;

namespace UE::FXRenderingUtils
{
	RENDERER_API bool CanMaterialRenderBeforeFXPostOpaque(const FSceneViewFamily& ViewFamily, const FPrimitiveSceneProxy& SceneProxy, const FMaterial& Material);

	namespace DistanceFields
	{
		RENDERER_API const FShaderParametersMetadata* GetObjectBufferParametersMetadata();
		RENDERER_API const FShaderParametersMetadata* GetAtlasParametersMetadata();

		RENDERER_API bool HasDataToBind(const FSceneView& View);

		RENDERER_API void SetupObjectBufferParameters(FRDGBuilder& GraphBuilder, uint8* DestinationData, const FSceneView* View);
		RENDERER_API void SetupAtlasParameters(FRDGBuilder& GraphBuilder, uint8* DestinationData, const FSceneView* View);
	}
}

/**
 * This class exposes methods required by FX rendering that must access rendering internals.
 */ 
class RENDERER_API FFXRenderingUtils
{
public:
	FFXRenderingUtils() = delete;
	FFXRenderingUtils(const FFXRenderingUtils&) = delete;
	FFXRenderingUtils& operator=(const FFXRenderingUtils&) = delete;

	/** Utility to determine if a material might render before the FXSystem's PostRenderOpaque is called for the view family */
	UE_DEPRECATED(5.3, "Use UE::FXRenderingUtils::CanMaterialRenderBeforeFXPostOpaque")
	static bool CanMaterialRenderBeforeFXPostOpaque(
		const FSceneViewFamily& ViewFamily,
		const FPrimitiveSceneProxy& SceneProxy,
		const FMaterial& Material)
	{
		return UE::FXRenderingUtils::CanMaterialRenderBeforeFXPostOpaque(ViewFamily, SceneProxy, Material);
	}
};
