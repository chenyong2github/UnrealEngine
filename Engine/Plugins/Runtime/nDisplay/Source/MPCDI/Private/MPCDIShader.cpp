// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MPCDIShader.h"

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"

#include "CommonRenderResources.h"
#include "PixelShaderUtils.h"

#include "ShaderParameterUtils.h"

#include "HAL/IConsoleManager.h"


// Select mpcdi stereo mode
enum class EVarMPCDIShaderType : uint8
{
	Default,
	DisableBlend,
	Passthrough,
	ShowWarpTexture,
	Disable,
};

static TAutoConsoleVariable<int32> CVarMPCDIShaderType(
	TEXT("nDisplay.render.mpcdi.shader"),
	(int)EVarMPCDIShaderType::Default,
	TEXT("Select shader for mpcdi:\n")
	TEXT("0 : default warp shader\n")
	TEXT("1 : Default, disabled blend\n")
	TEXT("2 : Passthrough shader\n")
	TEXT("3 : ShowWarpTexture shader\n")
	TEXT("4 : disable warp shader"),
	ECVF_RenderThreadSafe
);



#define MPCDIShaderFileName TEXT("/Plugin/nDisplay/Private/MPCDIShaders.usf")

// Implement shaders inside UE4
IMPLEMENT_SHADER_TYPE(, FMpcdiWarpVS, MPCDIShaderFileName, TEXT("MainVS"), SF_Vertex);
IMPLEMENT_SHADER_TYPE(, FMPCDIDirectProjectionVS, MPCDIShaderFileName, TEXT("DirectProjectionVS"), SF_Vertex);

IMPLEMENT_SHADER_TYPE(template<>, FMPCDIPassthroughPS,   MPCDIShaderFileName, TEXT("Passthrough_PS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FMPCDIShowWarpTexture, MPCDIShaderFileName, TEXT("ShowWarpTexture_PS"), SF_Pixel);

IMPLEMENT_SHADER_TYPE(template<>, FMPCDIWarpAndBlendPS, MPCDIShaderFileName, TEXT("WarpAndBlend_PS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FMPCDIWarpPS,         MPCDIShaderFileName, TEXT("WarpAndBlend_PS"), SF_Pixel);

#if 0
//Cubemap support
IMPLEMENT_SHADER_TYPE(template<>, FMPCDIWarpAndBlendCubemapPS, MPCDIShaderFileName, TEXT("WarpAndBlendCube_PS"), SF_Pixel);
#endif
#if 0
// Gpu perfect frustum
IMPLEMENT_SHADER_TYPE(template<>, FMPCDICalcBoundBoxPS, MPCDIShaderFileName, TEXT("BuildProjectedAABB_PS"), SF_Pixel);
#endif



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


static EMPCDIShader GetPixelShaderType(FMPCDIData *MPCDIData)
{
	if (MPCDIData && MPCDIData->IsValid())
	{
		switch (MPCDIData->GetProfileType())
		{
		case IMPCDI::EMPCDIProfileType::mpcdi_SL:
			return EMPCDIShader::Invalid;

		case IMPCDI::EMPCDIProfileType::mpcdi_A3D:
#if 0
			//@todo Unsupported now
			if (MPCDIData->IsCubeMapEnabled())
			{
				// Cubemap support
				return EMPCDIShader::WarpAndBlendCubeMap;
		}
#endif

		case IMPCDI::EMPCDIProfileType::mpcdi_2D:
		case IMPCDI::EMPCDIProfileType::mpcdi_3D:
			return EMPCDIShader::WarpAndBlend;

		case IMPCDI::EMPCDIProfileType::Invalid:
			return EMPCDIShader::Passthrough;

		default: EMPCDIShader::Invalid;
	};
}

	return EMPCDIShader::Invalid;
}



enum class EColorOPMode :uint8
{
	Default,
	AddAlpha,
	AddInvAlpha,
};

template<EMPCDIShader PixelShaderType>
static bool CompleteWarpTempl(FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& TextureWarpData, const IMPCDI::FShaderInputData &ShaderInputData, MPCDI::FMPCDIRegion& MPCDIRegion, EColorOPMode ColorOp)
{
	FMpcdiWarpDrawRectangleParameters VSData;
	GetUniformBufferParameters(TextureWarpData, VSData.DrawRectanglePosScaleBias, VSData.DrawRectangleUVScaleBias, VSData.DrawRectangleInvTargetSizeAndTextureSize);

	{
		//Clear viewport before render
		FIntRect MPCDIViewportRect;
		GetViewportRect(TextureWarpData, MPCDIRegion, MPCDIViewportRect);

		RHICmdList.SetViewport(MPCDIViewportRect.Min.X, MPCDIViewportRect.Min.Y, 0.0f, MPCDIViewportRect.Max.X, MPCDIViewportRect.Max.Y, 1.0f);
		//DrawClearQuad(RHICmdList, FLinearColor::Black);
	}


	// Set the graphic pipeline state.
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Never>::GetRHI();
	
	switch (ColorOp)
	{
	case EColorOPMode::Default:     GraphicsPSOInit.BlendState = TStaticBlendState <>::GetRHI(); break;
	case EColorOPMode::AddAlpha:    GraphicsPSOInit.BlendState = TStaticBlendState <CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI(); break;
	case EColorOPMode::AddInvAlpha: GraphicsPSOInit.BlendState = TStaticBlendState <CW_RGBA, BO_Add, BF_InverseSourceAlpha, BF_SourceAlpha, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI(); break;
	}

	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();

	TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FMpcdiWarpVS>                  VertexShader(ShaderMap);
	TShaderMapRef<TMpcdiWarpBasePS<PixelShaderType> > PixelShader(ShaderMap);

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;// GetVertexDeclarationFVector4();
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;


	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	VertexShader->SetParameters(RHICmdList, VertexShader->GetVertexShader(), VSData);
	PixelShader->SetParameters(RHICmdList, PixelShader->GetPixelShader(), TextureWarpData.SrcTexture, ShaderInputData, MPCDIRegion);

	FPixelShaderUtils::DrawFullscreenQuad(RHICmdList, 1); // Render quad

	return true;
}

bool FMPCDIShader::ApplyWarpBlend(FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& TextureWarpData, IMPCDI::FShaderInputData& ShaderInputData, FMPCDIData* MPCDIData)
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
	EMPCDIShader PixelShaderType = GetPixelShaderType(MPCDIData);
	if (EMPCDIShader::Invalid == PixelShaderType)
	{
		//@todo: handle error
		return false;
	}

	bool bIsBlendDisabled = !MPCDIRegion->AlphaMap.IsInitialized();

	FMatrix StereoMatrix;
	GetStereoMatrix(TextureWarpData, StereoMatrix);



	const EVarMPCDIShaderType ShaderType = (EVarMPCDIShaderType)CVarMPCDIShaderType.GetValueOnAnyThread();
	switch (ShaderType)
	{
	case EVarMPCDIShaderType::DisableBlend:
		bIsBlendDisabled = true; // Disable blend color ops
		break;
	case EVarMPCDIShaderType::Passthrough:
		PixelShaderType = EMPCDIShader::Passthrough;
		break;

	case EVarMPCDIShaderType::ShowWarpTexture:
		PixelShaderType = EMPCDIShader::ShowWarpTexture;
		break;

	case EVarMPCDIShaderType::Disable:
		return false;

	default:
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
			case EMPCDIShader::Passthrough:
				ShaderInputData.UVMatrix = StereoMatrix;
				bIsRenderSuccess = CompleteWarpTempl<EMPCDIShader::Passthrough>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, EColorOPMode::Default);
				break;

			case EMPCDIShader::ShowWarpTexture:
				ShaderInputData.UVMatrix = ShaderInputData.Frustum.UVMatrix*MPCDIData->GetTextureMatrix()*MPCDIRegion->GetRegionMatrix()*StereoMatrix;
				{
					// WarpAndBlend
					if (bIsBlendDisabled)
					{
						bIsRenderSuccess = CompleteWarpTempl<EMPCDIShader::Warp>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, EColorOPMode::Default);
					}
					else
					{
						bIsRenderSuccess = CompleteWarpTempl<EMPCDIShader::WarpAndBlend>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, EColorOPMode::Default);
					}
				}

				// Overlay
				bIsRenderSuccess &= CompleteWarpTempl<EMPCDIShader::ShowWarpTexture>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, EColorOPMode::AddAlpha);
				break;

			case EMPCDIShader::WarpAndBlend:
				ShaderInputData.UVMatrix = ShaderInputData.Frustum.UVMatrix*MPCDIData->GetTextureMatrix()*MPCDIRegion->GetRegionMatrix()*StereoMatrix;
				if (bIsBlendDisabled)
				{
					bIsRenderSuccess = CompleteWarpTempl<EMPCDIShader::Warp>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, EColorOPMode::Default);
				}
				else
				{
					bIsRenderSuccess = CompleteWarpTempl<EMPCDIShader::WarpAndBlend>(RHICmdList, TextureWarpData, ShaderInputData, *MPCDIRegion, EColorOPMode::Default);
				}
				
				break;
#if 0
				//Cubemap support
			case EMPCDIShader::WarpAndBlendCubeMap:
				//! Not implemented
				break;
#endif
#if 0
				// GPU perfect frustum calc
			case EMPCDIShader::BuildProjectedAABB:
				//! Not implemented
				break;
#endif
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
