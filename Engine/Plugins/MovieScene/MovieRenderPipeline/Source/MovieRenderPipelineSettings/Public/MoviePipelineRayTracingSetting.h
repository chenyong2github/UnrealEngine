// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineSetting.h"
#include "LevelSequenceActor.h"
#include "MoviePipelineRayTracingSetting.generated.h"

UCLASS(BlueprintType)
class UMoviePipelineRayTracingSetting : public UMoviePipelineSetting
{
	GENERATED_BODY()

	UMoviePipelineRayTracingSetting()
		: bEnableDenoisers(true)
		, AmbientOcclusionSPP(-1)
		, GlobalIlluminationSPP(-1)
		, ReflectionsSPP(-1)
		, ShadowSPP(-1)
		, bUsePathTracer(false)
	{}

public:
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "RayTracingSettingDisplayName", "Raytracing"); }
	virtual FText GetCategoryText() const override { return NSLOCTEXT("MovieRenderPipeline", "RenderingCategoryName_Text", "Rendering"); }
#endif
	virtual bool IsValidOnShots() const override { return true; }
	virtual bool IsValidOnMaster() const override { return true; }
public:
	virtual void OnSetupForShot()
	{
		FString DenoisersEnabled = bEnableDenoisers ? TEXT("1") : TEXT("0");

		 PreviousDenoiserState.Reflections = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Reflections.Denoiser"))->GetInt();
		 PreviousDenoiserState.Shadows = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Denoiser"))->GetInt();
		 PreviousDenoiserState.AmbientOcclusion = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AmbientOcclusion.Denoiser"))->GetInt();
		 PreviousDenoiserState.GlobalIllumination = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Raytracing.GLobalIllumination.Denoiser"))->GetInt();
		 
		 PreviousSamplesPerPixelState.Reflections = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Raytracing.Reflections.SamplesPerPixel"))->GetInt();
		 PreviousSamplesPerPixelState.Shadows = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Raytracing.Shadows.SamplesPerPixel"))->GetInt();
		 PreviousSamplesPerPixelState.AmbientOcclusion = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Raytracing.AmbientOcclusion.SamplesPerPixel"))->GetInt();
		 PreviousSamplesPerPixelState.GlobalIllumination = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Raytracing.GlobalIllumination.SamplesPerPixel"))->GetInt();

		 /*
		 r.RayTracing.GlobalIllumination.SamplesPerPixel 4
		r.RayTracing.Reflections.MaxBounces 2
		r.RayTracing.Reflections.ScreenPercentage 100
		r.RayTracing.Reflections.Shadows 1
		r.RayTracing.Translucency.Shadows 1
		r.RayTracing.GlobalIllumination 2
		r.RayTracing.GlobalIllumination.Denoiser 0
		r.RayTracing.GlobalIllumination.SamplesPerPixel 4
		r.RayTracing.GlobalIllumination.MaxBounces 1
		r.RayTracing.GlobalIllumination.ScreenPercentage 50
		r.RayTracing.Reflections 1
		r.Reflections.Denoiser 1
		r.RayTracing.Reflections.SamplesPerPixel 1
		r.RayTracing.Reflections.MaxBounces 2
		r.RayTracing.Reflections.ScreenPercentage 100
		r.RayTracing.Reflections.Translucency 1
		r.RayTracing.Reflections.Shadows 1
		r.RayTracing.Translucency 1
		r.RayTracing.Translucency.Refraction 0
		r.RayTracing.Translucency.SamplesPerPixel 1
		r.RayTracing.Translucency.MaxRefractionRays 2*/
	}

	/**
	* Enabling the ray tracing denoisers will reduce the number of samples required per frame,
	* but can introduce artifacts (ghosting). If you disable this expect to need 64-128 samples
	* per frame to overcome the lack of denoiser.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition="!bUsePathTracer"), Category = "Movie Pipeline")
	bool bEnableDenoisers;
	
	/**
	* The number of samples per pixel to use during Ambient Occlusion raytracing. Too large of a value
	* will cause a TDR (Timeout Detection and Recovery) which results in a GPU/Engine crash. More effective
	* to use more temporal/spatial samples (better quality, less likely to trigger TDR). Set to -1 to
	* fall back to the Post Processing/Camera settings.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin="-1", UIMax="2"), Category = "Movie Pipeline")
	int32 AmbientOcclusionSPP;

	/**
	* The number of samples per pixel to use during Global Illumination raytracing. Too large of a value
	* will cause a TDR (Timeout Detection and Recovery) which results in a GPU/Engine crash. More effective
	* to use more temporal/spatial samples (better quality, less likely to trigger TDR). Set to -1 to
	* fall back to the Post Processing/Camera settings.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = "-1", UIMax = "2"), Category = "Movie Pipeline")
	int32 GlobalIlluminationSPP;

	/**
	* The number of samples per pixel to use during Reflection raytracing. Too large of a value
	* will cause a TDR (Timeout Detection and Recovery) which results in a GPU/Engine crash. More effective
	* to use more temporal/spatial samples (better quality, less likely to trigger TDR). Set to -1 to
	* fall back to the Post Processing/Camera settings.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = "-1", UIMax = "2"), Category = "Movie Pipeline")
	int32 ReflectionsSPP;

	/**
	* The number of samples per pixel to use during Global Illumination raytracing. Too large of a value
	* will cause a TDR (Timeout Detection and Recovery) which results in a GPU/Engine crash. More effective
	* to use more temporal/spatial samples (better quality, less likely to trigger TDR). Set to -1 to
	* fall back to the Post Processing/Camera settings.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (UIMin = "-1", UIMax = "2"), Category = "Movie Pipeline")
	int32 ShadowSPP;
		
	/**
	* Should we use the Path Tracer instead of the real-time raytracer? The path tracer is a reference implementation
	* and may not support all rendering features ray tracing does. It is a debugging tool to compare the output
	* of realtime raytracing and raster engines compared to a more traditional approach.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	bool bUsePathTracer;

private:
	struct FCVarStateCache
	{
		int32 Reflections;
		int32 Shadows;
		int32 AmbientOcclusion;
		int32 GlobalIllumination;
	};

	FCVarStateCache PreviousDenoiserState;
	FCVarStateCache PreviousSamplesPerPixelState;
	/**
	
	UPROPERTY(Instanced, EditAnywhere, BlueprintReadWrite, Category="Movie Pipeline")
	ULevelSequenceBurnInInitSettings* Settings;*/
};