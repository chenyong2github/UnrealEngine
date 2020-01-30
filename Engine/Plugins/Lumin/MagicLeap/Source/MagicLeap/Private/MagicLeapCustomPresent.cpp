// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapCustomPresent.h"
#include "MagicLeapHMD.h"
#include "RenderingThread.h"
#include "Lumin/CAPIShims/LuminAPILifecycle.h"

#include "RHICommandList.h"
#include "GlobalShader.h"
#include "ScreenRendering.h"
#include "CommonRenderResources.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"

#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN
#include "OpenGLDrvPrivate.h"
#include "MagicLeapHelperOpenGL.h"
#endif // PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN

#include "MagicLeapGraphics.h"

#if PLATFORM_WINDOWS || PLATFORM_LUMIN
#include "XRThreadUtils.h"
#endif // PLATFORM_WINDOWS || PLATFORM_LUMIN

#include "Containers/Union.h"

DECLARE_FLOAT_COUNTER_STAT(TEXT("Far Clipping Plane"), STAT_FarClip, STATGROUP_MagicLeap);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Timewarp Stabilization Depth"), STAT_StabilizationDepth, STATGROUP_MagicLeap);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Focus Distance"), STAT_FocusDistance, STATGROUP_MagicLeap);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Near Clipping Plane"), STAT_NearClip, STATGROUP_MagicLeap);

FMagicLeapCustomPresent::FMagicLeapCustomPresent(FMagicLeapHMD* plugin)
: FRHICustomPresent()
, Plugin(plugin)
, bNotifyLifecycleOfFirstPresent(true)
, bCustomPresentIsSet(false)
, PlatformAPILevel(FMagicLeapAPISetup::GetPlatformLevel())
, HFOV(80)
, VFOV(60)
{
	bool bManualCallToAppReady = false;
	if (GConfig->GetBool(TEXT("/Script/LuminRuntimeSettings.LuminRuntimeSettings"), TEXT("bManualCallToAppReady"), bManualCallToAppReady, GEngineIni))
	{
		bNotifyLifecycleOfFirstPresent = !bManualCallToAppReady;
	}
}

bool FMagicLeapCustomPresent::NeedsNativePresent()
{
	return (Plugin->GetWindowMirrorMode() > 0);
}

void FMagicLeapCustomPresent::OnBackBufferResize()
{}

bool FMagicLeapCustomPresent::Present(int32& SyncInterval)
{
	check(IsInRenderingThread() || IsInRHIThread());

	// turn off VSync for the 'normal Present'.
	SyncInterval = 0;
	// We don't do any mirroring on Lumin as we render direct to the device only.
#if PLATFORM_LUMIN
	bool bHostPresent = false;
#else
	bool bHostPresent = Plugin->GetWindowMirrorMode() > 0;
#endif

	FinishRendering();

	bCustomPresentIsSet = false;

	return bHostPresent;
}

void FMagicLeapCustomPresent::GetFieldOfView(float& OutHFOVInDegrees, float& OutVFOVInDegrees)
{
	OutHFOVInDegrees = static_cast<float>(FPlatformAtomics::AtomicRead(&HFOV));
	OutVFOVInDegrees = static_cast<float>(FPlatformAtomics::AtomicRead(&VFOV));
}

void FMagicLeapCustomPresent::Reset()
{
	if (IsInGameThread())
	{
		// Wait for all resources to be released
		FlushRenderingCommands();
	}
}

void FMagicLeapCustomPresent::Shutdown()
{
	Reset();
}

template <typename CameraParamsType>
void FMagicLeapCustomPresent::InitCameraParams(CameraParamsType & CameraParams, FTrackingFrame & Frame)
{
	// TODO [Blake] : Need to see if we can use this newer matrix and override the view
	// projection matrix (since they query GetStereoProjectionMatrix on the main thread)
#if WITH_MLSDK
	CameraParams.projection_type = Frame.ProjectionType;
#endif // WITH_MLSDK
	CameraParams.surface_scale = Frame.PixelDensity;
	CameraParams.protected_surface = false;
	GConfig->GetBool(TEXT("/Script/LuminRuntimeSettings.LuminRuntimeSettings"), TEXT("bProtectedContent"), CameraParams.protected_surface, GEngineIni);

	// The near clipping plane is expected in meters despite what is documented in the header.
	CameraParams.near_clip = GNearClippingPlane / Frame.WorldToMetersScale;
	CameraParams.far_clip = Frame.FarClippingPlane / Frame.WorldToMetersScale;
	CameraParams.focus_distance = Frame.FocusDistance / Frame.WorldToMetersScale;

	InitExtraCameraParams(CameraParams, Frame);
}

#if WITH_MLSDK
template <>
void FMagicLeapCustomPresent::InitExtraCameraParams(MLGraphicsFrameParams & CameraParams, FTrackingFrame & Frame)
{}

template <>
void FMagicLeapCustomPresent::InitExtraCameraParams(MLGraphicsFrameParamsEx & CameraParams, FTrackingFrame & Frame)
{
	GConfig->GetBool(TEXT("/Script/LuminRuntimeSettings.LuminRuntimeSettings"), TEXT("bFrameVignette"), CameraParams.vignette, GEngineIni);

	CameraParams.stabilization_depth = Frame.StabilizationDepth / Frame.WorldToMetersScale;
}
#endif // WITH_MLSDK

void FMagicLeapCustomPresent::BeginFrame(FTrackingFrame& Frame)
{
#if WITH_MLSDK
	if (bCustomPresentIsSet && !Plugin->IsRenderingPaused())
	{
		TUnion< MLGraphicsFrameParams, MLGraphicsFrameParamsEx > camera_params;
		if (PlatformAPILevel >= 2)
		{
			camera_params.SetSubtype<MLGraphicsFrameParamsEx>(MLGraphicsFrameParamsEx());
			MLGraphicsFrameParamsExInit(&camera_params.GetSubtype<MLGraphicsFrameParamsEx>());
			InitCameraParams(camera_params.GetSubtype<MLGraphicsFrameParamsEx>(), Frame);
			SET_FLOAT_STAT(STAT_FarClip, camera_params.GetSubtype<MLGraphicsFrameParamsEx>().far_clip);
			SET_FLOAT_STAT(STAT_StabilizationDepth, camera_params.GetSubtype<MLGraphicsFrameParamsEx>().stabilization_depth);
			SET_FLOAT_STAT(STAT_FocusDistance, camera_params.GetSubtype<MLGraphicsFrameParamsEx>().focus_distance);
			SET_FLOAT_STAT(STAT_NearClip, camera_params.GetSubtype<MLGraphicsFrameParamsEx>().near_clip);
		}
		else
		{
			camera_params.SetSubtype<MLGraphicsFrameParams>(MLGraphicsFrameParams());
			MLResult Result = MLGraphicsInitFrameParams(&camera_params.GetSubtype<MLGraphicsFrameParams>());
			if (Result != MLResult_Ok)
			{
				UE_LOG(LogMagicLeap, Error, TEXT("MLGraphicsInitFrameParams failed with status %d"), Result);
			}
			InitCameraParams(camera_params.GetSubtype<MLGraphicsFrameParams>(), Frame);
			SET_FLOAT_STAT(STAT_FarClip, camera_params.GetSubtype<MLGraphicsFrameParams>().far_clip);
			SET_FLOAT_STAT(STAT_FocusDistance, camera_params.GetSubtype<MLGraphicsFrameParams>().focus_distance);
			SET_FLOAT_STAT(STAT_NearClip, camera_params.GetSubtype<MLGraphicsFrameParams>().near_clip);
		}		

		MLResult Result = MLResult_Ok;
		if (PlatformAPILevel >= 2)
		{
			Result = MLGraphicsBeginFrameEx(Plugin->GraphicsClient, &camera_params.GetSubtype<MLGraphicsFrameParamsEx>(), &Frame.FrameInfo);
		}
		else
		{
			Result = MLGraphicsBeginFrame(Plugin->GraphicsClient, &camera_params.GetSubtype<MLGraphicsFrameParams>(), &Frame.FrameInfo.handle, &Frame.FrameInfo.virtual_camera_info_array);
		}
		Frame.bBeginFrameSucceeded = (Result == MLResult_Ok);
		if (!Frame.bBeginFrameSucceeded)
		{
			if (Result != MLResult_Timeout)
			{
				UE_LOG(LogMagicLeap, Error, TEXT("MLGraphicsBeginFrame failed with status %s"), UTF8_TO_TCHAR(MLGetResultString(Result)));
			}
			// TODO: See if this is only needed for ZI.
			Frame.FrameInfo.handle = ML_INVALID_HANDLE;
			MagicLeap::ResetVirtualCameraInfoArray(Frame.FrameInfo.virtual_camera_info_array);
		}

		const MLGraphicsVirtualCameraInfo& VCamInfo = Frame.FrameInfo.virtual_camera_info_array.virtual_cameras[0];
		FPlatformAtomics::AtomicStore(&HFOV, static_cast<int64>(FMath::RadiansToDegrees((VCamInfo.left_half_angle + VCamInfo.right_half_angle)*2.0f)));
		FPlatformAtomics::AtomicStore(&VFOV, static_cast<int64>(FMath::RadiansToDegrees((VCamInfo.top_half_angle + VCamInfo.bottom_half_angle)*2.0f)));
	}
	else
	{
		Frame.bBeginFrameSucceeded = false;
	}
#endif // WITH_MLSDK
}

void FMagicLeapCustomPresent::NotifyFirstRender()
{
#if WITH_MLSDK
	if (bNotifyLifecycleOfFirstPresent)
	{
		MLResult Result = MLLifecycleSetReadyIndication();
		bNotifyLifecycleOfFirstPresent = (Result != MLResult_Ok);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeap, Error, TEXT("MLLifecycleSetReadyIndication failed with error %d."), Result);
		}
		else
		{
			// [temporary] used for KPI tracking.
			UE_LOG(LogMagicLeap, Display, TEXT("Presenting first render from app."));
		}
	}
#endif // WITH_MLSDK
}

void FMagicLeapCustomPresent::RenderToTextureSlice_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, FRHITexture* DstTexture, uint32 ArraySlice, const FVector4& UVandSize)
{
#if WITH_MLSDK
	FRHIRenderPassInfo RPInfo(DstTexture, ERenderTargetActions::DontLoad_Store, nullptr, 0, ArraySlice);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("MagicLeap_RenderToMLSurface"));
	{
		const MLGraphicsVirtualCameraInfoArray& vp_array = Plugin->GetCurrentFrame().FrameInfo.virtual_camera_info_array;
		const uint32 vp_width = static_cast<uint32>(vp_array.viewport.w);
		const uint32 vp_height = static_cast<uint32>(vp_array.viewport.h);

		RHICmdList.SetViewport(0, 0, 0, vp_width, vp_height, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		const auto FeatureLevel = GMaxRHIFeatureLevel;
		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
		TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), SrcTexture);

		Plugin->GetRendererModule()->DrawRectangle(
			RHICmdList,
			0, 0,
			vp_width, vp_height,
			UVandSize.X, UVandSize.Y, 	// U, V
			UVandSize.Z, UVandSize.W,	// SizeU, SizeV
			FIntPoint(vp_width, vp_height),
			FIntPoint(1, 1),
			*VertexShader,
			EDRF_Default);
	}
	RHICmdList.EndRenderPass();
#endif // WITH_MLSDK
}
