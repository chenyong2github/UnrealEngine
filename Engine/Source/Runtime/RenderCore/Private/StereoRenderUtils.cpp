// Copyright Epic Games, Inc. All Rights Reserved.

#include "StereoRenderUtils.h"
#include "RenderUtils.h"

namespace UE::StereoRenderUtils
{
	
RENDERCORE_API FStereoShaderAspects::FStereoShaderAspects(EShaderPlatform Platform) :
	bInstancedStereoEnabled(false)
	, bMobileMultiViewEnabled(false)
	, bInstancedMultiViewportEnabled(false)
	, bInstancedStereoNative(false)
	, bMobileMultiViewNative(false)
	, bMobileMultiViewFallback(false)
{
	check(Platform < EShaderPlatform::SP_NumPlatforms);
	
	// Would be nice to use URendererSettings, but not accessible in RenderCore
	static FShaderPlatformCachedIniValue<bool> CVarInstancedStereo(TEXT("vr.InstancedStereo"));
	static FShaderPlatformCachedIniValue<bool> CVarMobileMultiView(TEXT("vr.MobileMultiView"));
	static FShaderPlatformCachedIniValue<bool> CVarMultiViewport(TEXT("vr.MultiViewport"));
	static FShaderPlatformCachedIniValue<bool> CVarMobileHDR(TEXT("r.MobileHDR"));
	
	const bool bInstancedStereo = CVarInstancedStereo.Get(Platform);
	
	const bool bMobilePlatform = IsMobilePlatform(Platform);
	const bool bMobilePostprocessing = CVarMobileHDR.Get(Platform);
	const bool bMobileMultiView = CVarMobileMultiView.Get(Platform);
	const bool bMultiViewport = CVarMultiViewport.Get(Platform);
	
	bInstancedStereoNative = !bMobilePlatform && bInstancedStereo && RHISupportsInstancedStereo(Platform);
	
	const bool bMobileMultiViewCoreSupport = bMobilePlatform && bMobileMultiView && !bMobilePostprocessing;
	if (bMobileMultiViewCoreSupport)
	{
		if (RHISupportsMobileMultiView(Platform))
		{
			bMobileMultiViewNative = true;
		}
		else if (RHISupportsInstancedStereo(Platform))
		{
			bMobileMultiViewFallback = true;
		}
	}
	
	bInstancedStereoEnabled = bInstancedStereoNative || bMobileMultiViewFallback;
	bInstancedMultiViewportEnabled = bInstancedStereoNative && bMultiViewport && GRHISupportsArrayIndexFromAnyShader && RHISupportsMultiViewport(Platform);
	bMobileMultiViewEnabled = bMobileMultiViewNative || bMobileMultiViewFallback;
}
	
RENDERCORE_API bool FStereoShaderAspects::IsInstancedStereoEnabled() const { return bInstancedStereoEnabled; }
RENDERCORE_API bool FStereoShaderAspects::IsMobileMultiViewEnabled() const { return bMobileMultiViewEnabled; }
RENDERCORE_API bool FStereoShaderAspects::IsInstancedMultiViewportEnabled() const { return bInstancedMultiViewportEnabled; }
	
} // namespace UE::RenderUtils
