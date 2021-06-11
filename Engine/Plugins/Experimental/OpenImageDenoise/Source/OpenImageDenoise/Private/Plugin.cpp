// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Renderer/Public/PathTracingDenoiser.h"
#include "RHI.h"
#include "RHIResources.h"

#include "OpenImageDenoise/oidn.hpp"

class FOpenImageDenoiseModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

#if WITH_EDITOR
DECLARE_LOG_CATEGORY_EXTERN(LogOpenImageDenoise, Log, All);
DEFINE_LOG_CATEGORY(LogOpenImageDenoise);
#endif

IMPLEMENT_MODULE(FOpenImageDenoiseModule, OpenImageDenoise)

static void Denoise(FRHICommandListImmediate& RHICmdList, FRHITexture2D* ColorTex, FRHITexture2D* AlbedoTex, FRHITexture2D* NormalTex, FRHITexture2D* OutputTex)
{
	const int DenoiserMode = 2; // TODO: Expose setting for this

#if WITH_EDITOR
	uint64 FilterExecuteTime = 0;
	FilterExecuteTime -= FPlatformTime::Cycles64();
#endif

	FIntPoint Size = ColorTex->GetSizeXY();
	FIntRect Rect = FIntRect(0, 0, Size.X, Size.Y);
	TArray<FLinearColor> RawPixels;
	TArray<FLinearColor> RawAlbedo;
	TArray<FLinearColor> RawNormal;
	FReadSurfaceDataFlags ReadDataFlags(ERangeCompressionMode::RCM_MinMax);
	ReadDataFlags.SetLinearToGamma(false);
	RHICmdList.ReadSurfaceData(ColorTex, Rect, RawPixels, ReadDataFlags);
	if (DenoiserMode >= 2)
	{
		RHICmdList.ReadSurfaceData(AlbedoTex, Rect, RawAlbedo, ReadDataFlags);
		RHICmdList.ReadSurfaceData(NormalTex, Rect, RawNormal, ReadDataFlags);
	}

	check(RawPixels.Num() == Size.X * Size.Y);

	uint32_t DestStride;
	FLinearColor* DestBuffer = (FLinearColor*)RHICmdList.LockTexture2D(OutputTex, 0, RLM_WriteOnly, DestStride, false);

	// create device only once?
	oidn::DeviceRef OIDNDevice = oidn::newDevice();
	OIDNDevice.commit();
	oidn::FilterRef OIDNFilter = OIDNDevice.newFilter("RT");
	OIDNFilter.setImage("color", RawPixels.GetData(), oidn::Format::Float3, Size.X, Size.Y, 0, sizeof(FLinearColor), sizeof(FLinearColor) * Size.X);
	if (DenoiserMode >= 2)
	{
		OIDNFilter.setImage("albedo", RawAlbedo.GetData(), oidn::Format::Float3, Size.X, Size.Y, 0, sizeof(FLinearColor), sizeof(FLinearColor) * Size.X);
		OIDNFilter.setImage("normal", RawNormal.GetData(), oidn::Format::Float3, Size.X, Size.Y, 0, sizeof(FLinearColor), sizeof(FLinearColor) * Size.X);
	}
	if (DenoiserMode >= 3)
	{
		OIDNFilter.set("cleanAux", true);
	}
	OIDNFilter.setImage("output", DestBuffer, oidn::Format::Float3, Size.X, Size.Y, 0, sizeof(FLinearColor), DestStride);
	OIDNFilter.set("hdr", true);
	OIDNFilter.commit();

	OIDNFilter.execute();

	RHICmdList.UnlockTexture2D(OutputTex, 0, false);

#if WITH_EDITOR
	const char* errorMessage;
	if (OIDNDevice.getError(errorMessage) != oidn::Error::None)
	{
		UE_LOG(LogOpenImageDenoise, Warning, TEXT("Denoiser failed: %s"), *FString(errorMessage));
		return;
	}

	FilterExecuteTime += FPlatformTime::Cycles64();
	const double FilterExecuteTimeMS = 1000.0 * FPlatformTime::ToSeconds64(FilterExecuteTime);
	UE_LOG(LogOpenImageDenoise, Log, TEXT("Denoised %d x %d pixels in %.2f ms"), Size.X, Size.Y, FilterExecuteTimeMS);
#endif
}


void FOpenImageDenoiseModule::StartupModule()
{
#if WITH_EDITOR
	UE_LOG(LogOpenImageDenoise, Log, TEXT("OIDN starting up"));
#endif
	GPathTracingDenoiserFunc = &Denoise;
}

void FOpenImageDenoiseModule::ShutdownModule()
{
#if WITH_EDITOR
	UE_LOG(LogOpenImageDenoise, Log, TEXT("OIDN shutting down"));
#endif
	GPathTracingDenoiserFunc = nullptr;
}
