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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render")
	bool bAllowOverride = false;

	// This texture resolved to target
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render")
	UTexture2D* SourceTexture = nullptr;

	// Use TextureRegion rect on SourceTexture to resolve
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render", meta = (DisplayName = "Use Texture Crop"))
	bool bShouldUseTextureRegion = false;

	// Resolve this region from OverrideTexture
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render", meta = (DisplayName = "Texture Crop"))
	FDisplayClusterConfigurationRectangle TextureRegion;
};

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationPostRender_BlurPostprocess
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render")
	EDisplayClusterConfiguration_PostRenderBlur Mode = EDisplayClusterConfiguration_PostRenderBlur::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render")
	int   KernelRadius = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render")
	float KernelScale = 1;
};

USTRUCT(Blueprintable)
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationPostRender_GenerateMips
{
	GENERATED_BODY()

public:
	// Allow autogenerate num mips for this target
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render")
	bool bAutoGenerateMips = false;

	// Control mips generator settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render")
	TEnumAsByte<enum TextureFilter> MipsSamplerFilter = TF_Trilinear;

	/**  AutoGenerateMips sampler address mode for U channel. Defaults to clamp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render")
	TEnumAsByte<enum TextureAddress> MipsAddressU = TA_Clamp;

	/**  AutoGenerateMips sampler address mode for V channel. Defaults to clamp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render")
	TEnumAsByte<enum TextureAddress> MipsAddressV = TA_Clamp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NDisplay Render")
	bool bShouldUseMaxNumMips = false;

	UPROPERTY(EditAnywhere, Category = "NDisplay Render", meta = (DisplayName = "Maximum Number of Mips"))
	int MaxNumMips = 0;
};
