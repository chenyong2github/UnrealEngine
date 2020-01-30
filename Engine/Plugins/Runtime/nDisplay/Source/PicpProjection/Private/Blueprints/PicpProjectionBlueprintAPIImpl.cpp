// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/PicpProjectionBlueprintAPIImpl.h"

#include "Engine/TextureRenderTarget2D.h"
#include "ComposurePostMoves.h"
#include "UObject/Package.h"

#include "IPicpProjection.h"
#include "IPicpMPCDI.h"
#include "PicpProjectionStrings.h"
#include "Overlay/PicpProjectionOverlayRender.h"



int UPicpProjectionAPIImpl::GetViewportCount()
{
	IPicpProjection& PicpModule = IPicpProjection::Get();
	return PicpModule.GetPolicyCount("picp_mpcdi");
}

void UPicpProjectionAPIImpl::AddProjectionDataListener(TScriptInterface<IPicpProjectionFrustumDataListener> Listener)
{
	IPicpProjection& PicpModule = IPicpProjection::Get();
	PicpModule.AddProjectionDataListener(Listener);
} 

void UPicpProjectionAPIImpl::RemoveProjectionDataListener(TScriptInterface<IPicpProjectionFrustumDataListener> Listener)
{
	IPicpProjection& PicpModule = IPicpProjection::Get();
	PicpModule.RemoveProjectionDataListener(Listener);
}

void UPicpProjectionAPIImpl::CleanProjectionDataListener(TScriptInterface<IPicpProjectionFrustumDataListener> Listener)
{
	IPicpProjection& PicpModule = IPicpProjection::Get();
	PicpModule.CleanProjectionDataListeners();
}

void UPicpProjectionAPIImpl::BlendCameraFrameCaptures(UTexture* SrcFrame, UTextureRenderTarget2D* DstFrame, UTextureRenderTarget2D* Result)
{	
	IPicpMPCDI::Get().ApplyCompose(SrcFrame, DstFrame, Result);
}

void UPicpProjectionAPIImpl::ApplyBlurPostProcess(UTextureRenderTarget2D* InOutRenderTarget, UTextureRenderTarget2D* TemporaryRenderTarget, int KernelRadius, float KernelScale, EPicpBlurPostProcessShaderType BlurType)
{
	IPicpMPCDI::Get().ApplyBlur(InOutRenderTarget, TemporaryRenderTarget, KernelRadius, KernelScale, BlurType);
}

void UPicpProjectionAPIImpl::SetupOverlayCaptures(const TArray<struct FPicpOverlayFrameBlendingParameters>& captures)
{
	FPicpProjectionOverlayFrameData overlayFrameData;

	for (auto& capture : captures)
	{		
		FRotator CameraRotation = capture.CineCamera->K2_GetComponentRotation();
		FVector  CameraLocation = capture.CineCamera->K2_GetComponentLocation();

		float fov = capture.CineCamera->FieldOfView * capture.FieldOfViewMultiplier;

		FComposurePostMoveSettings settings;
		float aspectRatio = capture.CineCamera->AspectRatio;

		FMatrix Prj = settings.GetProjectionMatrix(fov, aspectRatio);

		FTextureRenderTargetResource* overlayRTTRes = capture.CameraOverlayFrame->GameThread_GetRenderTargetResource();
		FTextureRenderTarget2DResource* overlayRTTRes2D = (FTextureRenderTarget2DResource*)overlayRTTRes;

		FRHITexture2D* CameraTextureRef = overlayRTTRes2D->GetTextureRHI();

		// add inner camera
		FPicpProjectionOverlayCamera NewCamera(CameraRotation, CameraLocation, Prj, CameraTextureRef, capture.RTTViewportId);
		NewCamera.SoftEdge = capture.SoftEdge;
		overlayFrameData.Cameras.Add(NewCamera);
		

		// add blending layers (lights?)
		for (auto& layer : capture.OverlayBlendFrames)
		{
			FTextureRenderTargetResource* res = layer.SourceFrameCapture->GameThread_GetRenderTargetResource();

			FTextureRenderTarget2DResource* rtt2d = (FTextureRenderTarget2DResource*)res;
			FRHITexture2D* paramRef = rtt2d->GetTextureRHI();

			if (layer.OverlayBlendMode == ECameraOverlayRenderMode::Over)
			{
				overlayFrameData.ViewportsOver.Add(layer.Id, FPicpProjectionOverlayViewport(paramRef));
			}
			else if (layer.OverlayBlendMode == ECameraOverlayRenderMode::Under)
			{
				overlayFrameData.ViewportsUnder.Add(layer.Id, FPicpProjectionOverlayViewport(paramRef));
			}
		}
	}

	IPicpProjection& PicpModule = IPicpProjection::Get();
	PicpModule.SetOverlayFrameData(PicpProjectionStrings::projection::PicpMPCDI, overlayFrameData);
}

void UPicpProjectionAPIImpl::SetWarpTextureCaptureState(UTextureRenderTarget2D* dstTexture, const FString& ViewportId, const int ViewIdx, bool bCaptureNow)
{
	IPicpProjection::Get().CaptureWarpTexture(dstTexture, ViewportId, ViewIdx, bCaptureNow);
}

void UPicpProjectionAPIImpl::EnableTextureRenderTargetMips(UTextureRenderTarget2D* Texture)
{
	Texture->bAutoGenerateMips = true;
	Texture->UpdateResource();
	Texture->UpdateResourceImmediate(true);
}