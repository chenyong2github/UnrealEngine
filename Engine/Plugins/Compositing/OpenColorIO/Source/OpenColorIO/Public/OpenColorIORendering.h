// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FOpenColorIOColorConversionSettings;
class FOpenColorIOTransformResource;
class FRDGBuilder;
struct FScreenPassRenderTarget;
struct FScreenPassTexture;
class FTextureResource;
class FViewInfo;
class UTexture;
class UTextureRenderTarget2D;
class UWorld;

struct OPENCOLORIO_API FOpenColorIORenderPassResources
{
	FOpenColorIOTransformResource* ShaderResource = nullptr;
	TSortedMap<int32, FTextureResource*> TextureResources = {};
};

/** Entry point to trigger OpenColorIO conversion rendering */
class OPENCOLORIO_API FOpenColorIORendering
{
public:
	FOpenColorIORendering() = delete;

	/**
	 * Applies the color transform described in the settings
	 *
	 * @param InWorld World from which to get the actual shader feature level we need to render
	 * @param InSettings Settings describing the color space transform to apply
	 * @param InTexture Texture in the source color space
	 * @param OutRenderTarget RenderTarget where to draw the input texture in the destination color space
	 * @return True if a rendering command to apply the transform was queued.
	 */
	static bool ApplyColorTransform(UWorld* InWorld, const FOpenColorIOColorConversionSettings& InSettings, UTexture* InTexture, UTextureRenderTarget2D* OutRenderTarget);

	/**
	 * Applies the color transform RDG pass with the provided resources.
	 *
	 * @param GraphBuilder Render graph builder
	 * @param View Scene view with additional information
	 * @param Input Input color texture
	 * @param Output Destination render target
	 * @param InPassInfo OpenColorIO shader and texture resources
	 * @param InGamma Display gamma
	 */
	static void AddPass_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		const FScreenPassTexture& Input,
		const FScreenPassRenderTarget& Output,
		const FOpenColorIORenderPassResources& InPassInfo,
		float InGamma);
};