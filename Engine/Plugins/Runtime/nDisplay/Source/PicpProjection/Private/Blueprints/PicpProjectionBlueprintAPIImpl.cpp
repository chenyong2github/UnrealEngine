// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/PicpProjectionBlueprintAPIImpl.h"

#include "Engine/TextureRenderTarget2D.h"
#include "ComposurePostMoves.h"
#include "UObject/Package.h"
#include "Engine/Texture2D.h"
#include "Engine/RendererSettings.h"

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

void UPicpProjectionAPIImpl::SetupOverlayCaptures(const TArray<struct FPicpCameraBlendingParameters> &CameraCaptures, const TArray<struct FPicpOverlayFrameBlendingPair> &OverlayCaptures)
{
	FPicpProjectionOverlayFrameData OverlayFrameData;
	for (auto& CameraIt : CameraCaptures)
	{
		if (CameraIt.CameraOverlayFrame)
		{
			// Get camera view texture ref
			FTextureRenderTargetResource*     CameraRTTRes = CameraIt.CameraOverlayFrame->GameThread_GetRenderTargetResource();
			FTextureRenderTarget2DResource* CameraRTTRes2D = CameraRTTRes?((FTextureRenderTarget2DResource*)CameraRTTRes):nullptr;
			FRHITexture* CameraRTTRes2DRHI = CameraRTTRes2D ? CameraRTTRes2D->GetTextureRHI() : nullptr;
			if(CameraRTTRes2DRHI)
			{
				// Get camera position, rotation and projection:
				FRotator CameraRotation = CameraIt.CineCamera->K2_GetComponentRotation();
				FVector  CameraLocation = CameraIt.CineCamera->K2_GetComponentLocation();

				float CameraFOV = CameraIt.CineCamera->FieldOfView * CameraIt.FieldOfViewMultiplier;
				float CameraAspectRatio = CameraIt.CineCamera->AspectRatio;

				FComposurePostMoveSettings ComposurePostMoveSettings;
				FMatrix CameraPrj = ComposurePostMoveSettings.GetProjectionMatrix(CameraFOV, CameraAspectRatio);

				// Create inner camera data
				FPicpProjectionOverlayCamera NewCamera(CameraRotation, CameraLocation, CameraPrj, CameraRTTRes2DRHI, CameraIt.RTTViewportId);
				NewCamera.SoftEdge = CameraIt.SoftEdge;

				if (CameraIt.CameraChromakey.ChromakeyOverlayFrame)
				{
					// Render chromakey advanced:
					FTextureRenderTargetResource*     ChromakeyRTTRes = CameraIt.CameraChromakey.ChromakeyOverlayFrame->GameThread_GetRenderTargetResource();
					FTextureRenderTarget2DResource* ChromakeyRTTRes2D = ChromakeyRTTRes ? ((FTextureRenderTarget2DResource*)ChromakeyRTTRes) : nullptr;
					FRHITexture* ChromakeyRTTRes2DRHI = ChromakeyRTTRes2D ? ChromakeyRTTRes2D->GetTextureRHI() : nullptr;
					if (ChromakeyRTTRes2DRHI)
					{
						NewCamera.Chromakey.ChromakeyTexture = ChromakeyRTTRes2DRHI;
						if (NewCamera.Chromakey.IsChromakeyUsed() && CameraIt.CameraChromakey.ChromakeyMarkerTexture)
						{
							// Render chromakey markers:
							FTextureResource*     ChromakeyMarkerRTTRes = CameraIt.CameraChromakey.ChromakeyMarkerTexture->Resource;
							if (ChromakeyMarkerRTTRes)
							{
								NewCamera.Chromakey.ChromakeyMarkerTexture    = ChromakeyMarkerRTTRes ? ChromakeyMarkerRTTRes->GetTexture2DRHI() : nullptr;
								NewCamera.Chromakey.ChromakeyMarkerScale      = CameraIt.CameraChromakey.ChromakeyMarkerScale;

								switch (CameraIt.CameraChromakey.ChromakeyMarkerUVSource)
								{
								case EChromakeyMarkerUVSource::WarpMesh:
									NewCamera.Chromakey.bChromakeyMarkerUseMeshUV = true;
									break;

								default:
									break;
								}
							}
						}
					}
				}

				OverlayFrameData.Cameras.Add(NewCamera);
			}
		}
	}

	// Add blending layers (LightCards)
	for (auto& OverlayIt : OverlayCaptures)
	{
		FTextureRenderTargetResource*     OverlayRTTRes = OverlayIt.SourceFrameCapture->GameThread_GetRenderTargetResource();
		FTextureRenderTarget2DResource* OverlayRTTRes2D = OverlayRTTRes?((FTextureRenderTarget2DResource*)OverlayRTTRes):nullptr;
		FRHITexture2D* OverlayRTTRes2DRHI = OverlayRTTRes2D ? OverlayRTTRes2D->GetTextureRHI() : nullptr;
		if (OverlayRTTRes2DRHI)
		{
			switch (OverlayIt.OverlayBlendMode)
			{
			case ECameraOverlayRenderMode::Over:
				OverlayFrameData.ViewportsOver.Add(OverlayIt.Id, FPicpProjectionOverlayViewport(OverlayRTTRes2DRHI));
				break;

			case ECameraOverlayRenderMode::Under:
				OverlayFrameData.ViewportsUnder.Add(OverlayIt.Id, FPicpProjectionOverlayViewport(OverlayRTTRes2DRHI));
				break;
			}
		}
	}

	IPicpProjection& PicpModule = IPicpProjection::Get();
	PicpModule.SetOverlayFrameData(PicpProjectionStrings::projection::PicpMPCDI, OverlayFrameData);
}

void UPicpProjectionAPIImpl::SetWarpTextureCaptureState(UTextureRenderTarget2D* OutWarpRTT, const FString& ViewportId, const int ViewIdx, bool bCaptureNow)
{
	IPicpProjection::Get().CaptureWarpTexture(OutWarpRTT, ViewportId, ViewIdx, bCaptureNow);
}

void UPicpProjectionAPIImpl::EnableTextureRenderTargetMips(UTextureRenderTarget2D* Texture)
{
	Texture->bAutoGenerateMips = true;
	Texture->UpdateResource();
	Texture->UpdateResourceImmediate(true);
}

void UPicpProjectionAPIImpl::AssignWarpMeshToViewport(const FString& ViewportId, UStaticMeshComponent* MeshComponent, USceneComponent* OriginComponent)
{
	IPicpProjection& PicpModule = IPicpProjection::Get();
	PicpModule.AssignWarpMeshToViewport(ViewportId, MeshComponent, OriginComponent);
}


ETextureRenderTargetFormat UPicpProjectionAPIImpl::GetDefaultBackBufferRenderTargetFormat() const
{
	const TConsoleVariableData<int32>* CVarDefaultBackBufferPixelFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
	EDefaultBackBufferPixelFormat::Type DefaultBackbufferFormat = EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnGameThread());
	
	const int32 ValidIndex = FMath::Clamp((int32)DefaultBackbufferFormat, 0, (int32)EDefaultBackBufferPixelFormat::DBBPF_MAX - 1);
	static ETextureRenderTargetFormat STextureRenderTargetFormat[] = { RTF_RGBA8, RTF_RGBA8, RTF_RGBA16f, RTF_RGBA16f, RTF_RGB10A2 };
	return STextureRenderTargetFormat[ValidIndex];
}

