// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/RendererSettings.h"
#include "PixelFormat.h"
#include "RHI.h"
#include "GPUSkinVertexFactory.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "Misc/MessageDialog.h"
#include "UnrealEdMisc.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformFilemanager.h"

/** The editor object. */
extern UNREALED_API class UEditorEngine* GEditor;
#endif // #if WITH_EDITOR

#define LOCTEXT_NAMESPACE "RendererSettings"

namespace EAlphaChannelMode
{
	EAlphaChannelMode::Type FromInt(int32 InAlphaChannelMode)
	{
		return static_cast<EAlphaChannelMode::Type>(FMath::Clamp(InAlphaChannelMode, (int32)Disabled, (int32)AllowThroughTonemapper));
	}
}

namespace EDefaultBackBufferPixelFormat
{
	EPixelFormat Convert2PixelFormat(EDefaultBackBufferPixelFormat::Type InDefaultBackBufferPixelFormat)
	{
		const int32 ValidIndex = FMath::Clamp((int32)InDefaultBackBufferPixelFormat, 0, (int32)DBBPF_MAX - 1);
		static EPixelFormat SPixelFormat[] = { PF_B8G8R8A8, PF_B8G8R8A8, PF_FloatRGBA, PF_FloatRGBA, PF_A2B10G10R10 };
		return SPixelFormat[ValidIndex];
	}

	int32 NumberOfBitForAlpha(EDefaultBackBufferPixelFormat::Type InDefaultBackBufferPixelFormat)
	{
		switch (InDefaultBackBufferPixelFormat)
		{
			case DBBPF_A16B16G16R16_DEPRECATED:
			case DBBPF_B8G8R8A8:
			case DBBPF_FloatRGB_DEPRECATED:
			case DBBPF_FloatRGBA:
				return 8;
			case DBBPF_A2B10G10R10:
				return 2;
			default:
				return 0;
		}
		return 0;
	}

	EDefaultBackBufferPixelFormat::Type FromInt(int32 InDefaultBackBufferPixelFormat)
	{
		const int32 ValidIndex = FMath::Clamp(InDefaultBackBufferPixelFormat, 0, (int32)DBBPF_MAX - 1);
		static EDefaultBackBufferPixelFormat::Type SPixelFormat[] = { DBBPF_B8G8R8A8, DBBPF_B8G8R8A8, DBBPF_FloatRGBA, DBBPF_FloatRGBA, DBBPF_A2B10G10R10 };
		return SPixelFormat[ValidIndex];
	}
}

URendererSettings::URendererSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SectionName = TEXT("Rendering");
	TranslucentSortAxis = FVector(0.0f, -1.0f, 0.0f);
	bSupportStationarySkylight = true;
	bSupportPointLightWholeSceneShadows = true;
	bSupportAtmosphericFog = true;
	bSupportSkyAtmosphere = true;
	bSupportSkinCacheShaders = false;
	GPUSimulationTextureSizeX = 1024;
	GPUSimulationTextureSizeY = 1024;
	bEnableRayTracing = 0;
	bEnableRayTracingTextureLOD = 0;
	bLPV = 1;
	MaxSkinBones = FGPUBaseSkinVertexFactory::GHardwareMaxGPUSkinBones;
}

void URendererSettings::PostInitProperties()
{
	Super::PostInitProperties();
	
	SanatizeReflectionCaptureResolution();

#if WITH_EDITOR
	if (IsTemplate())
	{
		ImportConsoleVariableValues();
	}
#endif // #if WITH_EDITOR
}

#if WITH_EDITOR
void URendererSettings::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	PreEditReflectionCaptureResolution = ReflectionCaptureResolution;
}

void URendererSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	SanatizeReflectionCaptureResolution();

	if (PropertyChangedEvent.Property)
	{
		// round up GPU sim texture sizes to nearest power of two, and constrain to sensible values
		if ( PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(URendererSettings, GPUSimulationTextureSizeX) 
			|| PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(URendererSettings, GPUSimulationTextureSizeY) ) 
		{
			static const int32 MinGPUSimTextureSize = 32;
			static const int32 MaxGPUSimTextureSize = 8192;
			GPUSimulationTextureSizeX = FMath::RoundUpToPowerOfTwo( FMath::Clamp(GPUSimulationTextureSizeX, MinGPUSimTextureSize, MaxGPUSimTextureSize) );
			GPUSimulationTextureSizeY = FMath::RoundUpToPowerOfTwo( FMath::Clamp(GPUSimulationTextureSizeY, MinGPUSimTextureSize, MaxGPUSimTextureSize) );
		}

		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(URendererSettings, bEnableRayTracing) 
			&& bEnableRayTracing 
			&& !bSupportSkinCacheShaders)
		{
			FString FullPath = FPaths::ConvertRelativePathToFull(GetDefaultConfigFilename());
			FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*FullPath, false);

			if (FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("Skin Cache Disabled", "Ray Tracing requires enabling skin cache. Do you want to automatically enable skin cache now?")) == EAppReturnType::Yes)
			{
				bSupportSkinCacheShaders = 1;

				for (TFieldIterator<FProperty> PropIt(GetClass()); PropIt; ++PropIt)
				{
					FProperty* Property = *PropIt;
					if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(URendererSettings, bSupportSkinCacheShaders))
					{
						UpdateSinglePropertyInConfigFile(Property, GetDefaultConfigFilename());
					}
				}
			}
			else
			{
				bEnableRayTracing = 0;

				for (TFieldIterator<FProperty> PropIt(GetClass()); PropIt; ++PropIt)
				{
					FProperty* Property = *PropIt;
					if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(URendererSettings, bEnableRayTracing))
					{
						UpdateSinglePropertyInConfigFile(Property, GetDefaultConfigFilename());
					}
				}
			}
		}

		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(URendererSettings, VirtualTextureTileSize))
		{
			VirtualTextureTileSize = FMath::RoundUpToPowerOfTwo(VirtualTextureTileSize);
		}

		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(URendererSettings, VirtualTextureTileBorderSize))
		{
			VirtualTextureTileBorderSize = FMath::RoundUpToPowerOfTwo(VirtualTextureTileBorderSize);
		}

		if ((PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(URendererSettings, bSupportSkyAtmosphere)))
		{
			if (!bSupportSkyAtmosphere)
			{
				bSupportSkyAtmosphereAffectsHeightFog = 0; // Always disable sky affecting height fog if sky is disabled.
			}
		}

		ExportValuesToConsoleVariables(PropertyChangedEvent.Property);

		if ((PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(URendererSettings, ReflectionCaptureResolution) ||
			(PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(URendererSettings, bReflectionCaptureCompression))) &&
			PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
		{
			if (GEditor != nullptr)
			{
				if (GWorld != nullptr && GWorld->FeatureLevel == ERHIFeatureLevel::ES3_1)
				{
					// When we feature change from SM5 to ES31 we call BuildReflectionCapture if we have Unbuilt Reflection Components, so no reason to call it again here
					// This is to make sure that we have valid data for Mobile Preview.

					// ES31->SM5 to be able to capture
					GEditor->ToggleFeatureLevelPreview();
					// SM5->ES31 BuildReflectionCaptures are triggered here on callback
					GEditor->ToggleFeatureLevelPreview();
				}
				else
				{
					GEditor->BuildReflectionCaptures();
				}
			}
		}
	}
}

bool URendererSettings::CanEditChange(const FProperty* InProperty) const
{
	const bool ParentVal = Super::CanEditChange(InProperty);

	if ((InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(URendererSettings, bSupportSkinCacheShaders)))
	{
		//only allow DISABLE of skincache shaders if raytracing is also disabled as skincache is a dependency of raytracing.
		return ParentVal && (!bSupportSkinCacheShaders || !bEnableRayTracing);
	}

	if ((InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(URendererSettings, bSupportSkyAtmosphereAffectsHeightFog)))
	{
		return ParentVal && bSupportSkyAtmosphere;
	}

	return ParentVal;
}
#endif // #if WITH_EDITOR

void URendererSettings::SanatizeReflectionCaptureResolution()
{
	const int32 MaxCubemapResolution = GetMaxCubeTextureDimension();
	const int32 MinCubemapResolution = 8;

	ReflectionCaptureResolution = FMath::Clamp(int32(FMath::RoundUpToPowerOfTwo(ReflectionCaptureResolution)), MinCubemapResolution, MaxCubemapResolution);

#if WITH_EDITOR
	if (FApp::CanEverRender() && !FApp::IsUnattended())
	{
		SIZE_T TexMemRequired = CalcTextureSize(ReflectionCaptureResolution, ReflectionCaptureResolution, PF_FloatRGBA, FMath::CeilLogTwo(ReflectionCaptureResolution) + 1) * CubeFace_MAX;

		FTextureMemoryStats TextureMemStats;
		RHIGetTextureMemoryStats(TextureMemStats);

		if (TextureMemStats.DedicatedVideoMemory > 0 && TexMemRequired > SIZE_T(TextureMemStats.DedicatedVideoMemory / 8))
		{
			FNumberFormattingOptions FmtOpts = FNumberFormattingOptions()
				.SetUseGrouping(false)
				.SetMaximumFractionalDigits(2)
				.SetMinimumFractionalDigits(0)
				.SetRoundingMode(HalfFromZero);

			EAppReturnType::Type Response = FPlatformMisc::MessageBoxExt(
				EAppMsgType::YesNo,
				*FText::Format(
					LOCTEXT("MemAllocWarning_Message_ReflectionCubemap", "A resolution of {0} will require {1} of video memory PER reflection capture component. Are you sure?"),
					FText::AsNumber(ReflectionCaptureResolution, &FmtOpts),
					FText::AsMemory(TexMemRequired, &FmtOpts)
				).ToString(),
				*LOCTEXT("MemAllocWarning_Title_ReflectionCubemap", "Memory Allocation Warning").ToString()
			);

			if (Response == EAppReturnType::No)
			{
				ReflectionCaptureResolution = PreEditReflectionCaptureResolution;
			}
		}
		PreEditReflectionCaptureResolution = ReflectionCaptureResolution;
	}
#endif // WITH_EDITOR
}

URendererOverrideSettings::URendererOverrideSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SectionName = TEXT("Rendering Overrides");	
}

void URendererOverrideSettings::PostInitProperties()
{
	Super::PostInitProperties();

#if WITH_EDITOR
	if (IsTemplate())
	{
		ImportConsoleVariableValues();
	}
#endif // #if WITH_EDITOR
}

#if WITH_EDITOR
void URendererOverrideSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);	

	if (PropertyChangedEvent.Property)
	{
		ExportValuesToConsoleVariables(PropertyChangedEvent.Property);		
	}
}
#endif // #if WITH_EDITOR

#undef LOCTEXT_NAMESPACE
