// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "DisplayClusterConfigurationTypes_Enums.h"
#include "DisplayClusterConfigurationTypes_Base.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"

#include "DisplayClusterConfigurationTypes_PostRender.generated.h"

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationPostRender_Override
{
	GENERATED_BODY()

public:
	// Disable default render, and resolve SourceTexture to viewport
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render", meta = (DisplayName = "Enable Viewport Texture Replacement"))
	bool bAllowReplace = false;

	// This texture resolved to target
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render", meta = (EditCondition = "bAllowReplace"))
	UTexture2D* SourceTexture = nullptr;

	// Use TextureRegion rect on SourceTexture to resolve
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render", meta = (DisplayName = "Use Texture Crop", EditCondition = "bAllowReplace"))
	bool bShouldUseTextureRegion = false;

	// Resolve this region from OverrideTexture
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render", meta = (DisplayName = "Texture Crop", EditCondition = "bAllowReplace && bShouldUseTextureRegion"))
	FDisplayClusterReplaceTextureCropRectangle TextureRegion;
};

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationPostRender_BlurPostprocess
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render")
	EDisplayClusterConfiguration_PostRenderBlur Mode = EDisplayClusterConfiguration_PostRenderBlur::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render", meta = (EditCondition = "Mode != EDisplayClusterConfiguration_PostRenderBlur::None"))
	int   KernelRadius = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render", meta = (EditCondition = "Mode != EDisplayClusterConfiguration_PostRenderBlur::None"))
	float KernelScale = 20;
};

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationPostRender_GenerateMips
{
	GENERATED_BODY()

	// Allow autogenerate num mips for this target
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render")
	bool bAutoGenerateMips = false;

	// Control mips generator settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render", meta = (EditCondition = "bAutoGenerateMips"))
	TEnumAsByte<enum TextureFilter> MipsSamplerFilter = TF_Trilinear;

	/**  AutoGenerateMips sampler address mode for U channel. Defaults to clamp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render", meta = (EditCondition = "bAutoGenerateMips"))
	TEnumAsByte<enum TextureAddress> MipsAddressU = TA_Clamp;

	/**  AutoGenerateMips sampler address mode for V channel. Defaults to clamp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render", meta = (EditCondition = "bAutoGenerateMips"))
	TEnumAsByte<enum TextureAddress> MipsAddressV = TA_Clamp;

	// Performance: Allows a limited number of MIPs for high resolution.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render", meta = (DisplayName = "Enable Maximum Number of Mips", EditCondition = "bAutoGenerateMips"))
	bool bEnabledMaxNumMips = false;

	// Performance: Use this value as the maximum number of MIPs for high resolution.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render", meta = (DisplayName = "Maximum Number of Mips", EditCondition = "bAutoGenerateMips && bEnabledMaxNumMips"))
	int MaxNumMips = 0;
};
