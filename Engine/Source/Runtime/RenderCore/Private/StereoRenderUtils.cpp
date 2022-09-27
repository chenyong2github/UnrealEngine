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
	static FShaderPlatformCachedIniValue<bool> CVarMobileHDR(TEXT("r.MobileHDR"));
	
	const bool bInstancedStereo = CVarInstancedStereo.Get(Platform);
	
	const bool bMobilePlatform = IsMobilePlatform(Platform);
	const bool bMobilePostprocessing = CVarMobileHDR.Get(Platform);
	const bool bMobileMultiView = CVarMobileMultiView.Get(Platform);
	// If we're a cooker, don't check GRHI* setting, as it reflects runtime RHI capabilities.
	const bool bMultiViewportCapable = (GRHISupportsArrayIndexFromAnyShader || IsRunningCookCommandlet()) && RHISupportsMultiViewport(Platform);

	bInstancedStereoNative = !bMobilePlatform && bInstancedStereo && RHISupportsInstancedStereo(Platform);
	
	const bool bMobileMultiViewCoreSupport = bMobilePlatform && bMobileMultiView && !bMobilePostprocessing;
	if (bMobileMultiViewCoreSupport)
	{
		if (RHISupportsMobileMultiView(Platform))
		{
			bMobileMultiViewNative = true;
		}
		else if (RHISupportsInstancedStereo(Platform) && RHISupportsVertexShaderLayer(Platform))
		{
			bMobileMultiViewFallback = true;
		}
	}

	// "instanced stereo" is confusingly used to refer to two two modes:
	// 1) regular aka "native" ISR, where the views are selected via SV_ViewportArrayIndex - uses non-mobile shaders
	// 2) "mobile multiview fallback" ISR, which writes to a texture layer via SV_RenderTargetArrayIndex - uses mobile shaders
	// IsInstancedStereoEnabled() will be true in both cases

	bInstancedMultiViewportEnabled = bInstancedStereoNative && bMultiViewportCapable;
	// Since instanced stereo now relies on multi-viewport capability, it cannot be separately enabled from it.
	bInstancedStereoEnabled = bInstancedStereoNative || bMobileMultiViewFallback;
	bMobileMultiViewEnabled = bMobileMultiViewNative || bMobileMultiViewFallback;
}
	
RENDERCORE_API bool FStereoShaderAspects::IsInstancedStereoEnabled() const { return bInstancedStereoEnabled; }
RENDERCORE_API bool FStereoShaderAspects::IsMobileMultiViewEnabled() const { return bMobileMultiViewEnabled; }
RENDERCORE_API bool FStereoShaderAspects::IsInstancedMultiViewportEnabled() const { return bInstancedMultiViewportEnabled; }
	
} // namespace UE::RenderUtils
