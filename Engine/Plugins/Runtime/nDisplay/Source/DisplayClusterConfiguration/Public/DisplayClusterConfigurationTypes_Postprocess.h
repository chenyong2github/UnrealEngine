// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "DisplayClusterConfigurationStrings.h"

#include "Engine/Scene.h"

#include "DisplayClusterConfigurationTypes_Postprocess.generated.h"

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationViewport_CustomPostprocessSettings
{
	GENERATED_BODY()

public:
	// Enable custom postprocess
	UPROPERTY(EditAnywhere, Category = "NDisplay Viewport")
	bool bIsEnabled = false;

	// Apply postprocess for one frame
	UPROPERTY(EditAnywhere, Category = "NDisplay Viewport")
	bool bIsOneFrame = false;

	// Custom postprocess settings
	UPROPERTY(EditAnywhere, Category = "NDisplay Viewport")
	FPostProcessSettings PostProcessSettings;

	// Override blend weight
	UPROPERTY(EditAnywhere, Category = "NDisplay Viewport")
	float BlendWeight = 1;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationViewport_CustomPostprocess
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FDisplayClusterConfigurationViewport_CustomPostprocessSettings Start;

	UPROPERTY()
	FDisplayClusterConfigurationViewport_CustomPostprocessSettings Override;

	UPROPERTY()
	FDisplayClusterConfigurationViewport_CustomPostprocessSettings Final;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationViewport_ColorGradingSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Overrides", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_Saturation:1;
	UPROPERTY(EditAnywhere, Category = "Overrides", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_Contrast:1;
	UPROPERTY(EditAnywhere, Category = "Overrides", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_Gamma:1;
	UPROPERTY(EditAnywhere, Category = "Overrides", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_Gain:1;
	UPROPERTY(EditAnywhere, Category = "Overrides", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_Offset:1;

	// Saturation
	UPROPERTY(EditAnywhere, Category = "Color Grading", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01", EditCondition = "bOverride_Saturation", ColorGradingMode = "saturation"))
	FVector4 Saturation = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Contrast
	UPROPERTY(EditAnywhere, Category = "Color Grading", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01", EditCondition = "bOverride_Contrast", ColorGradingMode = "contrast"))
	FVector4 Contrast = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Gamma
	UPROPERTY(EditAnywhere, Category = "Color Grading", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01", EditCondition = "bOverride_Gamma", ColorGradingMode = "gamma"))
	FVector4 Gamma = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Gain
	UPROPERTY(EditAnywhere, Category = "Color Grading", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01", EditCondition = "bOverride_Gain", ColorGradingMode = "gain"))
	FVector4 Gain = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Offset
	UPROPERTY(EditAnywhere, Category = "Color Grading", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.001", EditCondition = "bOverride_Offset", ColorGradingMode = "offset"))
	FVector4 Offset = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationViewport_PerViewportSettings
{
	GENERATED_BODY()

	FDisplayClusterConfigurationViewport_PerViewportSettings() 
		: bOverride_WhiteTemp(0)
		, bOverride_WhiteTint(0)
		, bOverride_AutoExposureBias(0)
	{};

	UPROPERTY(EditAnywhere, Category = "Overrides", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_WhiteTemp:1;
	UPROPERTY(EditAnywhere, Category = "Overrides", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_WhiteTint:1;
	UPROPERTY(EditAnywhere, Category = "Overrides", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_AutoExposureBias:1;

	// Blend weight
	UPROPERTY(EditAnywhere, Category = "Viewport Settings", Meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BlendWeight = 1.0f;

	// White temperature
	UPROPERTY(EditAnywhere, Category = "Viewport Settings", Meta = (UIMin = "1500.0", UIMax = "15000.0", EditCondition = "bOverride_WhiteTemp"))
	float WhiteTemp = 6500.0f;

	// White tint
	UPROPERTY(EditAnywhere, Category = "Viewport Settings", Meta = (UIMin = "-1.0", UIMax = "1.0", EditCondition = "bOverride_WhiteTint"))
	float WhiteTint = 0.0f;

	// Exposure compensation
	UPROPERTY(EditAnywhere, Category = "Viewport Settings", Meta = (UIMin = "-15.0", UIMax = "15.0", EditCondition = "bOverride_AutoExposureBias"))
	float AutoExposureBias = 0.0f;

	// Global color grading
	UPROPERTY(EditAnywhere, Category = "Viewport Settings")
	FDisplayClusterConfigurationViewport_ColorGradingSettings Global;

	// Shadows color grading
	UPROPERTY(EditAnywhere, Category = "Viewport Settings")
	FDisplayClusterConfigurationViewport_ColorGradingSettings Shadows;

	// Midtones color grading
	UPROPERTY(EditAnywhere, Category = "Viewport Settings")
	FDisplayClusterConfigurationViewport_ColorGradingSettings Midtones;

	// Highlights color grading
	UPROPERTY(EditAnywhere, Category = "Viewport Settings")
	FDisplayClusterConfigurationViewport_ColorGradingSettings Highlights;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationViewport_PostProcessSettings
{
	GENERATED_BODY()

	// Exclude this viewport from the global cluster post process
	UPROPERTY(EditAnywhere, Category = "PostProcess Settings")
	bool bExcludeFromOverallClusterPostProcess = false;

	// Allow using a separate post process for this viewport
	UPROPERTY(EditAnywhere, Category = "PostProcess Settings")
	bool bIsEnabled = false;

	// Post process settings
	UPROPERTY(EditAnywhere, Category = "PostProcess Settings", Meta = (EditCondition = "bIsEnabled"))
	FDisplayClusterConfigurationViewport_PerViewportSettings ViewportSettings;
};

