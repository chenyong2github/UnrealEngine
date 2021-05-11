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

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationPostRender_Override
	//: public UDisplayClusterConfigurationData_Base
{
	GENERATED_BODY()

public:
	//UDisplayClusterConfigurationPostRender_Override();

public:
	// Disable default render, and resolve SourceTexture to viewport
	UPROPERTY(EditAnywhere, Category = "NDisplay Render")
	bool bAllowOverride = false;

	// This texture resolved to target
	UPROPERTY(EditAnywhere, Category = "NDisplay Render")
	UTexture2D* SourceTexture = nullptr;

	// Use TextureRegion rect on SourceTexture to resolve
	UPROPERTY(EditAnywhere, Category = "NDisplay Render")
	bool bShouldUseTextureRegion = false;

	// Resolve this region from OverrideTexture
	UPROPERTY(EditAnywhere, Category = "NDisplay Render")
	FDisplayClusterConfigurationRectangle TextureRegion;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationPostRender_BlurPostprocess
	//: public UDisplayClusterConfigurationData_Base
{
	GENERATED_BODY()

public:
	//UDisplayClusterConfigurationPostRender_BlurPostprocess();

public:
	UPROPERTY(EditAnywhere, Category = "NDisplay Render")
	EDisplayClusterConfiguration_PostRenderBlur Mode = EDisplayClusterConfiguration_PostRenderBlur::None;

	UPROPERTY(EditAnywhere, Category = "NDisplay Render")
	int   KernelRadius = 1;

	UPROPERTY(EditAnywhere, Category = "NDisplay Render")
	float KernelScale = 1;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationPostRender_GenerateMips
	//: public UDisplayClusterConfigurationData_Base
{
	GENERATED_BODY()

public:
	//UDisplayClusterConfigurationPostRender_GenerateMips();

public:
	// Allow autogenerate num mips for this target
	UPROPERTY(EditAnywhere, Category = "NDisplay Render")
	bool bAutoGenerateMips = false;

	// Control mips generator settings
	UPROPERTY(EditAnywhere, Category = "NDisplay Render")
	TEnumAsByte<enum TextureFilter> MipsSamplerFilter = TF_Trilinear;

	/**  AutoGenerateMips sampler address mode for U channel. Defaults to clamp. */
	UPROPERTY(EditAnywhere, Category = "NDisplay Render")
	TEnumAsByte<enum TextureAddress> MipsAddressU = TA_Clamp;

	/**  AutoGenerateMips sampler address mode for V channel. Defaults to clamp. */
	UPROPERTY(EditAnywhere, Category = "NDisplay Render")
	TEnumAsByte<enum TextureAddress> MipsAddressV = TA_Clamp;

	UPROPERTY(EditAnywhere, Category = "NDisplay Render")
	bool bShouldUseMaxNumMips = false;

	UPROPERTY(EditAnywhere, Category = "NDisplay Render")
	int MaxNumMips = 0;
};
