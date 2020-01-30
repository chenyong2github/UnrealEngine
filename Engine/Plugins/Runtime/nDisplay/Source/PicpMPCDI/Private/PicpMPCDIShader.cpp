// Copyright Epic Games, Inc. All Rights Reserved.

#include "PicpMPCDIShader.h"

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"

#include "CommonRenderResources.h"
#include "PixelShaderUtils.h"

#include "ShaderParameterUtils.h"

#include "MPCDIData.h"
#include "MPCDIShader.h"

// Select picp shader
enum class EVarPicpMPCDIShaderType : uint8
{
	Default=0,
	DefaultNoBlend,
	Passthrough,
	ForceMultiPassRender,
	Disable,
};
static TAutoConsoleVariable<int32> CVarPicpMPCDIShaderType(
	TEXT("nDisplay.render.picp.shader"),
	(int)EVarPicpMPCDIShaderType::Default,
	TEXT("Select shader for picp:\n")
	TEXT(" 0: Warp shader (used by default)\n")
	TEXT(" 1: Warp shader with disabled blend maps\n")
	TEXT(" 2: Passthrough shader\n")
	TEXT(" 3: Force Multi-Pass render shaders\n")
	TEXT(" 4: Disable all shaders\n"),
	ECVF_RenderThreadSafe
);

#define MPCDIShaderFileName TEXT("/Plugin/nDisplay/Private/MPCDIShaders.usf")
#define MPCDIOverlayShaderFileName TEXT("/Plugin/nDisplay/Private/MPCDIOverlayShaders.usf")

// Implement shaders inside UE4
IMPLEMENT_SHADER_TYPE(, FPicpWarpVS, MPCDIShaderFileName, TEXT("MainVS"), SF_Vertex);

IMPLEMENT_SHADER_TYPE(template<>, FPicpMPCDIPassthroughPS,  MPCDIShaderFileName, TEXT("Passthrough_PS"),  SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FPicpMPCDIWarpAndBlendPS, MPCDIShaderFileName, TEXT("WarpAndBlend_PS"), SF_Pixel);

IMPLEMENT_SHADER_TYPE(template<>, FPicpMPCDIWarpAndBlendBgPS,            MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendOverlay_PS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FPicpMPCDIWarpAndBlendBgOverlayPS,     MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendOverlay_PS"), SF_Pixel);

IMPLEMENT_SHADER_TYPE(template<>, FPicpMPCDIWarpAndBlendCameraPS,        MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendOverlay_PS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FPicpMPCDIWarpAndBlendCameraOverlayPS, MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendOverlay_PS"), SF_Pixel);

IMPLEMENT_SHADER_TYPE(template<>, FPicpMPCDIWarpAndBlendOverlayPS,       MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendViewportOverlay_PS"), SF_Pixel);

IMPLEMENT_SHADER_TYPE(template<>, FPicpWarpAndBlendOneCameraFullPS,      MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendOneCameraFull_PS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FPicpWarpAndBlendOneCameraBackPS,      MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendOneCameraFull_PS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FPicpWarpAndBlendOneCameraFrontPS,     MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendOneCameraFull_PS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FPicpWarpAndBlendOneCameraNonePS,      MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendOneCameraFull_PS"), SF_Pixel);

// No Blend case:
IMPLEMENT_SHADER_TYPE(template<>, FPicpMPCDIWarpPS,          MPCDIShaderFileName, TEXT("WarpAndBlend_PS"), SF_Pixel);

IMPLEMENT_SHADER_TYPE(template<>, FPicpMPCDIWarpBgPS,        MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendOverlay_PS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FPicpMPCDIWarpBgOverlayPS, MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendOverlay_PS"), SF_Pixel);

IMPLEMENT_SHADER_TYPE(template<>, FPicpMPCDIWarpCameraPS,    MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendOverlay_PS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FPicpMPCDIWarpCameraOverlayPS, MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendOverlay_PS"), SF_Pixel);

IMPLEMENT_SHADER_TYPE(template<>, FPicpMPCDIWarpOverlayPS,   MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendViewportOverlay_PS"), SF_Pixel);

IMPLEMENT_SHADER_TYPE(template<>, FPicpWarpOneCameraFullPS,  MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendOneCameraFull_PS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FPicpWarpOneCameraBackPS,  MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendOneCameraFull_PS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FPicpWarpOneCameraFrontPS, MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendOneCameraFull_PS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FPicpWarpOneCameraNonePS,  MPCDIOverlayShaderFileName, TEXT("PicpMPCDIWarpAndBlendOneCameraFull_PS"), SF_Pixel);



static void GetUniformBufferParameters(IMPCDI::FTextureWarpData& WarpData, FVector4& OutPosScaleBias, FVector4& OutUVScaleBias, FVector4& OutInvTargetSizeAndTextureSize)
{
	FIntPoint WarpDataSrcSize = WarpData.SrcTexture->GetSizeXY();
	FIntPoint WarpDataDstSize = WarpData.DstTexture->GetSizeXY();

	float U = WarpData.SrcRect.Min.X / (float)WarpDataSrcSize.X;
	float V = WarpData.SrcRect.Min.Y / (float)WarpDataSrcSize.Y;
	float USize = WarpData.SrcRect.Width() / (float)WarpDataSrcSize.X;
	float VSize = WarpData.SrcRect.Height() / (float)WarpDataSrcSize.Y;

	OutUVScaleBias = FVector4(USize, VSize, U, V);

	OutPosScaleBias = FVector4(1, 1, 0, 0);
	OutInvTargetSizeAndTextureSize = FVector4(1, 1, 1, 1);
}

static void GetStereoMatrix(IMPCDI::FTextureWarpData& WarpData, FMatrix& OutStereoMatrix)
{
	FIntPoint WarpDataSrcSize = WarpData.SrcTexture->GetSizeXY();
	FIntPoint WarpDataDstSize = WarpData.DstTexture->GetSizeXY();

	OutStereoMatrix = FMatrix::Identity;
	OutStereoMatrix.M[0][0] = float(WarpData.SrcRect.Width()) / float(WarpDataSrcSize.X);
	OutStereoMatrix.M[1][1] = float(WarpData.SrcRect.Height()) / float(WarpDataSrcSize.Y);
	OutStereoMatrix.M[3][0] = float(WarpData.SrcRect.Min.X) / float(WarpDataSrcSize.X);
	OutStereoMatrix.M[3][1] = float(WarpData.SrcRect.Min.Y) / float(WarpDataSrcSize.Y);
}


static void GetViewportRect(const IMPCDI::FTextureWarpData& TextureWarpData, const MPCDI::FMPCDIRegion& MPCDIRegion, FIntRect& OutViewportRect)
{
	int vpPosX = TextureWarpData.DstRect.Min.X;
	int vpPosY = TextureWarpData.DstRect.Min.Y;
	int vpSizeX = TextureWarpData.DstRect.Width();
	int vpSizeY = TextureWarpData.DstRect.Height();

	OutViewportRect.Min = FIntPoint(vpPosX, vpPosY);
	OutViewportRect.Max = FIntPoint(vpPosX + vpSizeX, vpPosY + vpSizeY);
}

static EPicpShaderType GetPixelShaderType(FMPCDIData *MPCDIData)
{
	if (MPCDIData && MPCDIData->IsValid())
	{
		switch (MPCDIData->GetProfileType())
		{
		case IMPCDI::EMPCDIProfileType::mpcdi_A3D:
			return EPicpShaderType::WarpAndBlend;

		case IMPCDI::EMPCDIProfileType::Invalid:
		case IMPCDI::EMPCDIProfileType::mpcdi_SL:
		case IMPCDI::EMPCDIProfileType::mpcdi_2D:
		case IMPCDI::EMPCDIProfileType::mpcdi_3D:
			return EPicpShaderType::Passthrough;
		};
	}

	return EPicpShaderType::Invalid;
}


enum class EColorOPMode :uint8
{
	Default,
	AddAlpha,
	AddInvAlpha,
};


template<EPicpShaderType PixelShaderType>
static bool CompleteWarpTempl(
	FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& TextureWarpData, const IMPCDI::FShaderInputData &ShaderInputData, MPCDI::FMPCDIRegion& MPCDIRegion, FMPCDIData* MPCDIData,
	const FPicpProjectionOverlayViewportData* PicpProjectionOverlayViewportData,	int CameraIndex, EColorOPMode ColorOp
)
{

	FMatrix StereoMatrix;
	GetStereoMatrix(TextureWarpData, StereoMatrix);


	FPicpMpcdiWarpDrawRectangleParameters VSData;
	GetUniformBufferParameters(TextureWarpData, VSData.DrawRectanglePosScaleBias, VSData.DrawRectangleUVScaleBias, VSData.DrawRectangleInvTargetSizeAndTextureSize);

	{
		//Clear viewport before render
		FIntRect MPCDIViewportRect;
		GetViewportRect(TextureWarpData, MPCDIRegion, MPCDIViewportRect);

		RHICmdList.SetViewport(MPCDIViewportRect.Min.X, MPCDIViewportRect.Min.Y, 0.0f, MPCDIViewportRect.Max.X, MPCDIViewportRect.Max.Y, 1.0f);
	}


	// Set the graphic pipeline state.
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FPicpWarpVS>                       VertexShader(ShaderMap);
	TShaderMapRef<TPicpWarpBasePS<PixelShaderType> > PixelShader(ShaderMap);


	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Never>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;// GetVertexDeclarationFVector4();
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	   
	switch (ColorOp)
	{
	case EColorOPMode::Default:     GraphicsPSOInit.BlendState = TStaticBlendState <>::GetRHI(); break;
	case EColorOPMode::AddAlpha:    GraphicsPSOInit.BlendState = TStaticBlendState <CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI(); break;
	case EColorOPMode::AddInvAlpha: GraphicsPSOInit.BlendState = TStaticBlendState <CW_RGBA, BO_Add, BF_InverseSourceAlpha, BF_SourceAlpha, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI(); break;
	}

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	if (PicpProjectionOverlayViewportData != nullptr && CameraIndex >= 0 && PicpProjectionOverlayViewportData->Cameras.Num() > CameraIndex)
	{
		FPicpProjectionOverlayCamera Camera = PicpProjectionOverlayViewportData->Cameras[CameraIndex];

		static const FMatrix Game2Render(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));
		static const FMatrix Render2Game =
			Game2Render.Inverse();

		FRotationTranslationMatrix CameraTranslationMatrix(Camera.ViewRot, Camera.ViewLoc);

		FMatrix WorldToCamera = CameraTranslationMatrix.Inverse();

		FMatrix UVMatrix = WorldToCamera * Game2Render * Camera.Prj;

		Camera.RuntimeCameraProjection = UVMatrix * MPCDIData->GetTextureMatrix() *MPCDIRegion.GetRegionMatrix();

		PixelShader->SetPicpParameters(RHICmdList, PixelShader->GetPixelShader(), &Camera);
	}

	switch (PixelShaderType)
	{
	case EPicpShaderType::WarpAndBlendAddOverlay:
	case EPicpShaderType::WarpAddOverlay:
		if (PicpProjectionOverlayViewportData != nullptr)
		{
			PixelShader->SetPicpParameters(RHICmdList, PixelShader->GetPixelShader(), &PicpProjectionOverlayViewportData->ViewportOver, nullptr);
		}
		break;
	case EPicpShaderType::WarpAndBlendAddCameraOverlay:
	case EPicpShaderType::WarpAddCameraOverlay:
		if(PicpProjectionOverlayViewportData!=nullptr)
		{
			PixelShader->SetPicpParameters(RHICmdList, PixelShader->GetPixelShader(), &PicpProjectionOverlayViewportData->ViewportOver, nullptr);
		}
	case EPicpShaderType::WarpAndBlendBgOverlay:
	case EPicpShaderType::WarpBgOverlay:
		if (PicpProjectionOverlayViewportData != nullptr)
		{
			PixelShader->SetPicpParameters(RHICmdList, PixelShader->GetPixelShader(), &PicpProjectionOverlayViewportData->ViewportUnder, nullptr);
		}
		break;
	case EPicpShaderType::WarpAndBlendOneCameraFull:
	case EPicpShaderType::WarpAndBlendOneCameraBack:
	case EPicpShaderType::WarpAndBlendOneCameraFront:

	case EPicpShaderType::WarpOneCameraFull:
	case EPicpShaderType::WarpOneCameraBack:
	case EPicpShaderType::WarpOneCameraFront:
		if (PicpProjectionOverlayViewportData != nullptr)
		{
			PixelShader->SetPicpParameters(RHICmdList, PixelShader->GetPixelShader(), &PicpProjectionOverlayViewportData->ViewportOver, &PicpProjectionOverlayViewportData->ViewportUnder);
		}
		break;
	default:
		// Do nothing
		break;
	}
	
	VertexShader->SetParameters(RHICmdList, VertexShader->GetVertexShader(), VSData);

	PixelShader->SetParameters(RHICmdList, PixelShader->GetPixelShader(), TextureWarpData.SrcTexture);
	PixelShader->SetParameters(RHICmdList, PixelShader->GetPixelShader(), MPCDIRegion);
	PixelShader->SetParameters(RHICmdList, PixelShader->GetPixelShader(), ShaderInputData.UVMatrix, StereoMatrix);

	{
		// Render quad
		FPixelShaderUtils::DrawFullscreenQuad(RHICmdList, 1);
	}

	return true;
}


bool FPicpMPCDIShader::ApplyWarpBlend(FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& TextureWarpData, IMPCDI::FShaderInputData& ShaderInputData, FMPCDIData* MPCDIData, FPicpProjectionOverlayViewportData* ViewportOverlayData)
{
	check(IsInRenderingThread());

	MPCDI::FMPCDIRegion* MPCDIRegion = nullptr;
	if (MPCDIData && MPCDIData->IsValid())
	{
		MPCDIRegion = MPCDIData->GetRegion(ShaderInputData.RegionLocator);
	}

	if (MPCDIRegion == nullptr)
	{
		//Handle error
		return false;
	}

	// Map ProfileType to shader type to use
	EPicpShaderType PixelShaderType = GetPixelShaderType(MPCDIData);
	if (EPicpShaderType::Invalid == PixelShaderType)
	{
		//@todo: handle error
		return false;
	}

	bool bIsBlendDisabled = !MPCDIRegion->AlphaMap.IsInitialized();
	bool bIsRenderSinglePass = ViewportOverlayData->Cameras.Num()==1;

	const EVarPicpMPCDIShaderType ShaderType = (EVarPicpMPCDIShaderType)CVarPicpMPCDIShaderType.GetValueOnAnyThread();

	switch (ShaderType) {
	case EVarPicpMPCDIShaderType::DefaultNoBlend:
		bIsBlendDisabled = true;
		break;
	case EVarPicpMPCDIShaderType::Passthrough:
		PixelShaderType = EPicpShaderType::Passthrough;
		break;
	case EVarPicpMPCDIShaderType::ForceMultiPassRender:
		bIsRenderSinglePass = false;
		break;
	case EVarPicpMPCDIShaderType::Disable:
		return false;
		break;
	};

	{
		// Do single warp render pass
		bool bIsRenderSuccess = false;
		FRHIRenderPassInfo RPInfo(TextureWarpData.DstTexture, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("DisplayClusterMPCDIWarpBlendShader"));
		{
			switch (PixelShaderType)
			{
			case EPicpShaderType::Passthrough:
				ShaderInputData.UVMatrix = FMatrix::Identity;				
				bIsRenderSuccess = CompleteWarpTempl<EPicpShaderType::Passthrough>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, nullptr, 0, EColorOPMode::Default);
				break;

			case EPicpShaderType::WarpAndBlend:
				ShaderInputData.UVMatrix = ShaderInputData.Frustum.UVMatrix*MPCDIData->GetTextureMatrix()*MPCDIRegion->GetRegionMatrix();

				// Use overlay render tricks
				if (ViewportOverlayData)
				{
					// First pass render bg
					if (bIsRenderSinglePass)
					{
						//One pass solution:
						if (ViewportOverlayData->iViewportUnderUsed())
						{
							if (ViewportOverlayData->iViewportOverUsed())
							{
								if (bIsBlendDisabled)
								{
									bIsRenderSuccess = CompleteWarpTempl<EPicpShaderType::WarpOneCameraFull>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, ViewportOverlayData, 0, EColorOPMode::Default);
								}
								else
								{
									bIsRenderSuccess = CompleteWarpTempl<EPicpShaderType::WarpAndBlendOneCameraFull>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, ViewportOverlayData, 0, EColorOPMode::Default);
								}
							}
							else
							{
								if (bIsBlendDisabled)
								{
									bIsRenderSuccess = CompleteWarpTempl<EPicpShaderType::WarpOneCameraBack>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, ViewportOverlayData, 0, EColorOPMode::Default);
								}
								else
								{
									bIsRenderSuccess = CompleteWarpTempl<EPicpShaderType::WarpAndBlendOneCameraBack>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, ViewportOverlayData, 0, EColorOPMode::Default);
								}
							}
						}
						else
						{
							if (ViewportOverlayData->iViewportOverUsed())
							{
								if (bIsBlendDisabled)
								{
									bIsRenderSuccess = CompleteWarpTempl<EPicpShaderType::WarpOneCameraFront>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, ViewportOverlayData, 0, EColorOPMode::Default);
								}
								else
								{
									bIsRenderSuccess = CompleteWarpTempl<EPicpShaderType::WarpAndBlendOneCameraFront>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, ViewportOverlayData, 0, EColorOPMode::Default);
								}
							}
							else
							{
								if (bIsBlendDisabled)
								{
									bIsRenderSuccess = CompleteWarpTempl<EPicpShaderType::WarpOneCameraNone>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, ViewportOverlayData, 0, EColorOPMode::Default);
								}
								else
								{
									bIsRenderSuccess = CompleteWarpTempl<EPicpShaderType::WarpAndBlendOneCameraNone>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, ViewportOverlayData, 0, EColorOPMode::Default);
								}
							}
						}						
					}
					else
					{
						//Complex multipass solution						
						if (ViewportOverlayData->iViewportUnderUsed())
						{
							if (bIsBlendDisabled)
							{
								bIsRenderSuccess = CompleteWarpTempl <EPicpShaderType::WarpBgOverlay>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, ViewportOverlayData, -1, EColorOPMode::Default);
							}
							else
							{
								bIsRenderSuccess = CompleteWarpTempl <EPicpShaderType::WarpAndBlendBgOverlay>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, ViewportOverlayData, -1, EColorOPMode::Default);
							}
						}
						else
						{
							if (bIsBlendDisabled)
							{
								bIsRenderSuccess = CompleteWarpTempl<EPicpShaderType::WarpBg>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, ViewportOverlayData, -1, EColorOPMode::Default);
							}
							else
							{
								bIsRenderSuccess = CompleteWarpTempl<EPicpShaderType::WarpAndBlendBg>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, ViewportOverlayData, -1, EColorOPMode::Default);
							}
						}

						//Second pass (now all cams on second pass, @todo add first cam on single pass with bg
						//@todo: Possible to sort camera override order
						if (ViewportOverlayData->Cameras.Num() > 0)
						{
							int CameraIndex = 0;
							int RenderPass = 0;
							for (auto& It : ViewportOverlayData->Cameras)
							{
								if (bIsBlendDisabled)
								{
									bIsRenderSuccess = CompleteWarpTempl<EPicpShaderType::WarpAddCamera>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, ViewportOverlayData, CameraIndex, EColorOPMode::AddAlpha);
								}
								else
								{
									bIsRenderSuccess = CompleteWarpTempl<EPicpShaderType::WarpAndBlendAddCamera>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, ViewportOverlayData, CameraIndex, EColorOPMode::AddAlpha);
								}

								RenderPass++;
								CameraIndex++; //! fixme
							}
						}

						if (ViewportOverlayData->iViewportOverUsed())
						{
							if (bIsBlendDisabled)
							{
								bIsRenderSuccess = CompleteWarpTempl<EPicpShaderType::WarpAddOverlay>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, ViewportOverlayData, -1, EColorOPMode::AddInvAlpha);
							}
							else
							{
								bIsRenderSuccess = CompleteWarpTempl<EPicpShaderType::WarpAndBlendAddOverlay>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, ViewportOverlayData, -1, EColorOPMode::AddInvAlpha);
							}
						}
					}
				}
				// Use default render
				else
				{
					if (bIsBlendDisabled)
					{
						bIsRenderSuccess = CompleteWarpTempl<EPicpShaderType::Warp>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, nullptr, 0, EColorOPMode::Default);
					}
					else
					{
						bIsRenderSuccess = CompleteWarpTempl<EPicpShaderType::WarpAndBlend>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, MPCDIData, nullptr, 0, EColorOPMode::Default);
					}
				}
			};
		}

		RHICmdList.EndRenderPass();

		if (bIsRenderSuccess)
		{
			return true;
		}
	}

	// Render pass failed, handle error
	return false;
}
