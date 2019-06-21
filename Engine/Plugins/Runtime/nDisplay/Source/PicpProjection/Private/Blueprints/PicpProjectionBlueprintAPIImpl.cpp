// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Blueprints/PicpProjectionBlueprintAPIImpl.h"

#include "Engine/TextureRenderTarget2D.h"

#include "UObject/Package.h"
#include "IPicpProjection.h"

#include "Overlay/PicpProjectionOverlayRender.h"
#include "IPicpMPCDI.h"
#include "ComposurePostMoves.h"


int UPicpProjectionAPIImpl::GetViewportCount()
{
	IPicpProjection& PicpModule = IPicpProjection::Get();
	return PicpModule.GetPolicyCount("picp_mpcdi");
}

void UPicpProjectionAPIImpl::SetProjectionDataListener(TScriptInterface<IPicpProjectionFrustumDataListener> Listener)
{
	IPicpProjection& PicpModule = IPicpProjection::Get();
	PicpModule.AddProjectionDataListener(Listener);
} 


void UPicpProjectionAPIImpl::BlendCameraFrameCaptures(UTexture* SrcFrame, UTextureRenderTarget2D* DstFrame, UTextureRenderTarget2D* Result)
{	
	IPicpMPCDI::Get().ApplyCompose(SrcFrame, DstFrame, Result);
}

void UPicpProjectionAPIImpl::ApplyBlurPostProcess(UTextureRenderTarget2D* InOutRenderTarget, UTextureRenderTarget2D* TemporaryRenderTarget, int KernelRadius, float KernelScale, EPicpBlurPostProcessShaderType BlurType)
{
	IPicpMPCDI::Get().ApplyBlur(InOutRenderTarget, TemporaryRenderTarget, KernelRadius, KernelScale, BlurType);
}

//!fixme send policy name from script
#define TEMP_POLICY_NAME TEXT("Picp_mpcdi")

void UPicpProjectionAPIImpl::SetupOverlayCaptures(const TArray<struct FPicpOverlayFrameBlendingParameters>& captures)
{
	FPicpProjectionOverlayFrameData overlayFrameData;

	for (auto& capture : captures)
	{
		FRotator CameraRotation = capture.CameraOverlayFrameCapture->K2_GetComponentRotation();
		FVector  CameraLocation = capture.CameraOverlayFrameCapture->K2_GetComponentLocation();

		float fov = capture.CameraOverlayFrameCapture->FOVAngle;

		FComposurePostMoveSettings settings;
		float width = capture.CameraOverlayFrameCapture->TextureTarget->GetSurfaceWidth();
		float height = capture.CameraOverlayFrameCapture->TextureTarget->GetSurfaceHeight();

		float aspectRatio = width / height;

		FMatrix Prj = settings.GetProjectionMatrix(fov, aspectRatio);

		FTextureRenderTargetResource* overlayRTTRes = capture.CameraOverlayFrame->GameThread_GetRenderTargetResource();
		FTextureRenderTarget2DResource* overlayRTTRes2D = (FTextureRenderTarget2DResource*)overlayRTTRes;

		FRHITexture2D* CameraTextureRef = overlayRTTRes2D->GetTextureRHI();
		FPicpProjectionOverlayCamera* overlayCamera = new FPicpProjectionOverlayCamera(CameraRotation, CameraLocation, Prj, CameraTextureRef);

		// add inner camera
		overlayFrameData.Cameras.Add(overlayCamera);
		overlayCamera->SoftEdge = capture.SoftEdge;

		// add blending layers (lights?)
		for (auto& layer : capture.OverlayBlendFrames)
		{
			FTextureRenderTargetResource* res = layer.SourceFrameCapture->GameThread_GetRenderTargetResource();

			FTextureRenderTarget2DResource* rtt2d = (FTextureRenderTarget2DResource*)res;
			FRHITexture2D* paramRef = rtt2d->GetTextureRHI();
			FPicpProjectionOverlayViewport* overlayViewport = new FPicpProjectionOverlayViewport(paramRef);

			if (layer.OverlayBlendMode == ECameraOverlayRenderMode::Over)
			{
				overlayFrameData.ViewportsOver.Add(layer.Id, overlayViewport);
			}
			else if (layer.OverlayBlendMode == ECameraOverlayRenderMode::Under)
			{
				overlayFrameData.ViewportsUnder.Add(layer.Id, overlayViewport);
			}
		}
	}

	IPicpProjection& PicpModule = IPicpProjection::Get();
	PicpModule.SetOverlayFrameData(TEMP_POLICY_NAME, overlayFrameData);
}


void UPicpProjectionAPIImpl::SetWarpTextureCaptureState(UTextureRenderTarget2D* dstTexture, const FString& ViewportId, const int ViewIdx, bool bCaptureNow)
{
	IPicpProjection::Get().CaptureWarpTexture(dstTexture, ViewportId, ViewIdx, bCaptureNow);
}