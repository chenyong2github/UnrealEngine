// Copyright Epic Games, Inc. All Rights Reserved.

#include "MPCDIShader.h"

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"
#include "RHIStaticStates.h"

#include "RenderResource.h"
#include "CommonRenderResources.h"
#include "PixelShaderUtils.h"

#include "Shader.h"
#include "GlobalShader.h"

#include "ShaderParameters.h"
#include "ShaderParameterUtils.h"
#include "ShaderPermutation.h"
#include "ShaderParameterStruct.h"

#include "MPCDIData.h"
#include "MPCDIRegion.h"
#include "MPCDIWarpTexture.h"

#define ShaderFileName "/Plugin/nDisplay/Private/MPCDIShaders.usf"

namespace MpcdiStrings
{
	static constexpr auto RenderPassName = TEXT("DisplayClusterMPCDIWarpBlend");
	static constexpr auto RenderPassHint = TEXT("DisplayCluster MPCDI Warp&Blend");
};

// Select mpcdi stereo mode
enum class EVarMPCDIShaderType : uint8
{
	Default = 0,
	DisableBlend,
	Passthrough,
	Disable,
};

static TAutoConsoleVariable<int32> CVarMPCDIShaderType(
	TEXT("nDisplay.render.mpcdi.shader"),
	(int)EVarMPCDIShaderType::Default,
	TEXT("Select shader for mpcdi:\n")
	TEXT(" 0: Warp shader (used by default)\n")
	TEXT(" 1: Warp shader with disabled blend maps\n")
	TEXT(" 2: Passthrough shader\n")
	TEXT(" 3: Disable all warp shaders"),
	ECVF_RenderThreadSafe
);


enum class EMpcdiShaderType : uint8
{
	Passthrough,  // Viewport frustum (no warpblend, only frustum math)
	WarpAndBlend, // Pure mpcdi warpblend for viewport
	Invalid,
};

namespace MpcdiShaderPermutation
{
	// Shared permutation for mpcdi warp
	class FMpcdiShaderAlphaMapBlending : SHADER_PERMUTATION_BOOL("ALPHAMAP_BLENDING");
	class FMpcdiShaderBetaMapBlending : SHADER_PERMUTATION_BOOL("BETAMAP_BLENDING");

	class FMpcdiShaderMeshWarp : SHADER_PERMUTATION_BOOL("MESH_WARP");

	using FCommonVSDomain = TShaderPermutationDomain<FMpcdiShaderMeshWarp>;
	using FCommonPSDomain = TShaderPermutationDomain<FMpcdiShaderAlphaMapBlending, FMpcdiShaderBetaMapBlending, FMpcdiShaderMeshWarp>;

	bool ShouldCompileCommonPSPermutation(const FGlobalShaderPermutationParameters& Parameters, const FCommonPSDomain& PermutationVector)
	{
		if (!PermutationVector.Get<FMpcdiShaderAlphaMapBlending>() && PermutationVector.Get<FMpcdiShaderBetaMapBlending>())
		{
			return false;
		}

		return true;
	}

	bool ShouldCompileCommonVSPermutation(const FGlobalShaderPermutationParameters& Parameters, const FCommonVSDomain& PermutationVector)
	{
		return true;
	}
};

BEGIN_SHADER_PARAMETER_STRUCT(FMpcdiVertexShaderParameters, )
	SHADER_PARAMETER(FVector4, DrawRectanglePosScaleBias)
	SHADER_PARAMETER(FVector4, DrawRectangleInvTargetSizeAndTextureSize)
	SHADER_PARAMETER(FVector4, DrawRectangleUVScaleBias)

	SHADER_PARAMETER(FMatrix, MeshToCaveProjectionMatrix)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FMpcdiPixelShaderParameters, )
	SHADER_PARAMETER_TEXTURE(Texture2D, InputTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, WarpMapTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, AlphaMapTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, BetaMapTexture)

	SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, WarpMapSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, AlphaMapSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, BetaMapSampler)

	SHADER_PARAMETER(FMatrix, ViewportTextureProjectionMatrix)

	SHADER_PARAMETER(float, AlphaEmbeddedGamma)
END_SHADER_PARAMETER_STRUCT()

class FMpcdiWarpVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMpcdiWarpVS);
	SHADER_USE_PARAMETER_STRUCT(FMpcdiWarpVS, FGlobalShader);

	using FPermutationDomain = MpcdiShaderPermutation::FCommonVSDomain;
	using FParameters = FMpcdiVertexShaderParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return MpcdiShaderPermutation::ShouldCompileCommonVSPermutation(Parameters, FPermutationDomain(Parameters.PermutationId));
	}
};

IMPLEMENT_GLOBAL_SHADER(FMpcdiWarpVS, ShaderFileName, "WarpVS", SF_Vertex);

class FMpcdiWarpPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMpcdiWarpPS);
	SHADER_USE_PARAMETER_STRUCT(FMpcdiWarpPS, FGlobalShader);

	using FPermutationDomain = MpcdiShaderPermutation::FCommonPSDomain;
	using FParameters = FMpcdiPixelShaderParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return MpcdiShaderPermutation::ShouldCompileCommonPSPermutation(Parameters, FPermutationDomain(Parameters.PermutationId));
	}
};

IMPLEMENT_GLOBAL_SHADER(FMpcdiWarpPS, ShaderFileName, "WarpPS", SF_Pixel);

class FMpcdiPassthroughPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMpcdiPassthroughPS);
	SHADER_USE_PARAMETER_STRUCT(FMpcdiPassthroughPS, FGlobalShader);

	using FParameters = FMpcdiPixelShaderParameters;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};

IMPLEMENT_GLOBAL_SHADER(FMpcdiPassthroughPS, ShaderFileName, "Passthrough_PS", SF_Pixel);


struct FMpcdiRenderPassData {
	MpcdiShaderPermutation::FCommonVSDomain VSPermutationVector;
	FMpcdiVertexShaderParameters            VSParameters;

	MpcdiShaderPermutation::FCommonPSDomain PSPermutationVector;
	FMpcdiPixelShaderParameters             PSParameters;
};


class FMpcdiPassRenderer
{
public:

	IMPCDI::FTextureWarpData &TextureWarpData;
	IMPCDI::FShaderInputData &ShaderInputData;

	const  FMPCDIRegion     *MPCDIRegion = nullptr;
	const    FMPCDIData     *MPCDIData   = nullptr;

	EMpcdiShaderType PixelShaderType;

	bool bIsBlendDisabled = false;

private:
	FMatrix GetStereoMatrix() const
	{
		FIntPoint WarpDataSrcSize = TextureWarpData.SrcTexture->GetSizeXY();
		FIntPoint WarpDataDstSize = TextureWarpData.DstTexture->GetSizeXY();

		FMatrix StereoMatrix = FMatrix::Identity;
		StereoMatrix.M[0][0] = float(TextureWarpData.SrcRect.Width()) / float(WarpDataSrcSize.X);
		StereoMatrix.M[1][1] = float(TextureWarpData.SrcRect.Height()) / float(WarpDataSrcSize.Y);
		StereoMatrix.M[3][0] = float(TextureWarpData.SrcRect.Min.X) / float(WarpDataSrcSize.X);
		StereoMatrix.M[3][1] = float(TextureWarpData.SrcRect.Min.Y) / float(WarpDataSrcSize.Y);

		return StereoMatrix;
	}

	FIntRect GetViewportRect() const
	{
		int vpPosX = TextureWarpData.DstRect.Min.X;
		int vpPosY = TextureWarpData.DstRect.Min.Y;
		int vpSizeX = TextureWarpData.DstRect.Width();
		int vpSizeY = TextureWarpData.DstRect.Height();

		return FIntRect(FIntPoint(vpPosX, vpPosY), FIntPoint(vpPosX + vpSizeX, vpPosY + vpSizeY));
	}

	EMpcdiShaderType GetPixelShaderType()
	{
		if (MPCDIData && MPCDIData->IsValid())
		{
			switch (MPCDIData->GetProfileType())
			{
				case IMPCDI::EMPCDIProfileType::mpcdi_2D:
				case IMPCDI::EMPCDIProfileType::mpcdi_A3D:
					return EMpcdiShaderType::WarpAndBlend;
#if 0
				case IMPCDI::EMPCDIProfileType::mpcdi_SL:
				case IMPCDI::EMPCDIProfileType::mpcdi_3D:
#endif
				default:
					return EMpcdiShaderType::Passthrough;
			};
		}

		return EMpcdiShaderType::Invalid;
	}

public:
	FMpcdiPassRenderer(IMPCDI::FTextureWarpData& InTextureWarpData, IMPCDI::FShaderInputData& InShaderInputData, FMPCDIData* InMPCDIData)
		: TextureWarpData(InTextureWarpData)
		, ShaderInputData(InShaderInputData)
		, MPCDIData(InMPCDIData)
	{ }

	bool GetViewportParameters(FMpcdiRenderPassData& RenderPassData)
	{
		// Input viewport texture
		RenderPassData.PSParameters.InputTexture = TextureWarpData.SrcTexture;
		RenderPassData.PSParameters.InputSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		// Vertex shader viewport rect
		FIntPoint WarpDataSrcSize = TextureWarpData.SrcTexture->GetSizeXY();
		FIntPoint WarpDataDstSize = TextureWarpData.DstTexture->GetSizeXY();

		float U = TextureWarpData.SrcRect.Min.X / (float)WarpDataSrcSize.X;
		float V = TextureWarpData.SrcRect.Min.Y / (float)WarpDataSrcSize.Y;
		float USize = TextureWarpData.SrcRect.Width() / (float)WarpDataSrcSize.X;
		float VSize = TextureWarpData.SrcRect.Height() / (float)WarpDataSrcSize.Y;

		RenderPassData.VSParameters.DrawRectanglePosScaleBias = FVector4(1, 1, 0, 0);
		RenderPassData.VSParameters.DrawRectangleInvTargetSizeAndTextureSize = FVector4(1, 1, 1, 1);
		RenderPassData.VSParameters.DrawRectangleUVScaleBias = FVector4(USize, VSize, U, V);
		return true;
	}

	bool GetWarpMapParameters(FMpcdiRenderPassData& RenderPassData)
	{
		RenderPassData.PSParameters.ViewportTextureProjectionMatrix = ShaderInputData.UVMatrix*GetStereoMatrix();;

		if (MPCDIRegion->WarpData)
		{
			switch (MPCDIRegion->WarpData->GetWarpGeometryType())
			{
				case EWarpGeometryType::UE_StaticMesh:
				{
					// Use mesh inseat of warp texture
					RenderPassData.PSPermutationVector.Set<MpcdiShaderPermutation::FMpcdiShaderMeshWarp>(true);
					RenderPassData.VSPermutationVector.Set<MpcdiShaderPermutation::FMpcdiShaderMeshWarp>(true);

					RenderPassData.VSParameters.MeshToCaveProjectionMatrix = ShaderInputData.Frustum.MeshToCaveMatrix;
					break;
				}

				case EWarpGeometryType::PFM_Texture:
				{
					FMPCDIWarpTexture* WarpMap = static_cast<FMPCDIWarpTexture*>(MPCDIRegion->WarpData);
					RenderPassData.PSParameters.WarpMapTexture = WarpMap->TextureRHI;
					RenderPassData.PSParameters.WarpMapSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
					break;
				}
			}
		}

		return true;
	}

	bool GetWarpBlendParameters(FMpcdiRenderPassData& RenderPassData)
	{
		if (MPCDIRegion->AlphaMap.IsValid())
		{
			RenderPassData.PSParameters.AlphaMapTexture = MPCDIRegion->AlphaMap.TextureRHI;
			RenderPassData.PSParameters.AlphaMapSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			RenderPassData.PSParameters.AlphaEmbeddedGamma = MPCDIRegion->AlphaMap.GetEmbeddedGamma();

			RenderPassData.PSPermutationVector.Set<MpcdiShaderPermutation::FMpcdiShaderAlphaMapBlending>(true);
		}

		if (MPCDIRegion->BetaMap.IsValid())
		{
			RenderPassData.PSParameters.BetaMapTexture = MPCDIRegion->BetaMap.TextureRHI;
			RenderPassData.PSParameters.BetaMapSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			RenderPassData.PSPermutationVector.Set<MpcdiShaderPermutation::FMpcdiShaderBetaMapBlending>(true);
		}

		return true;
	}
	
	void InitRenderPass(FMpcdiRenderPassData& RenderPassData)
	{
		// Forward input viewport
		GetViewportParameters(RenderPassData);

		// Configure mutable pixel shader:
		if (MPCDIRegion)
		{
			GetWarpMapParameters(RenderPassData);

			if (!bIsBlendDisabled)
			{
				GetWarpBlendParameters(RenderPassData);
			}
		}
	}

	bool RenderPassthrough(FRHICommandListImmediate& RHICmdList, FMpcdiRenderPassData& RenderPassData)
	{
		if (!MPCDIRegion)
		{
			return false;
		}

		// Get mutable shaders:
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FMpcdiWarpVS> VertexShader(ShaderMap, RenderPassData.VSPermutationVector);
		TShaderMapRef<FMpcdiPassthroughPS> PixelShader(ShaderMap);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		{
			// Set the graphic pipeline state.
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

			MPCDIRegion->WarpData->BeginRender(RHICmdList, GraphicsPSOInit);

			// Setup graphics pipeline
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Never>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			GraphicsPSOInit.BlendState = TStaticBlendState <>::GetRHI();
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		}

		// Setup shaders data
		SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), RenderPassData.VSParameters);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), RenderPassData.PSParameters);

		MPCDIRegion->WarpData->FinishRender(RHICmdList);

		return true;
	}

	bool RenderCurentPass(FRHICommandListImmediate& RHICmdList, FMpcdiRenderPassData& RenderPassData)
	{
		if (!MPCDIRegion)
		{
			return false;
		}

		// Get mutable shaders:
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FMpcdiWarpVS> VertexShader(ShaderMap, RenderPassData.VSPermutationVector);
		TShaderMapRef<FMpcdiWarpPS> PixelShader(ShaderMap, RenderPassData.PSPermutationVector);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		{
			// Set the graphic pipeline state.
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

			MPCDIRegion->WarpData->BeginRender(RHICmdList, GraphicsPSOInit);

			// Setup graphics pipeline
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Never>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			// First pass always override old viewport image
			GraphicsPSOInit.BlendState = TStaticBlendState <>::GetRHI();

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		}

		// Setup shaders data
		SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), RenderPassData.VSParameters);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), RenderPassData.PSParameters);

		MPCDIRegion->WarpData->FinishRender(RHICmdList);

		return true;
	}

	bool Initialize()
	{
		if (MPCDIData && MPCDIData->IsValid())
		{
			MPCDIRegion = MPCDIData->GetRegion(ShaderInputData.RegionLocator);
		}

		if (MPCDIRegion == nullptr)
		{
			return false;
		}

		// Map ProfileType to shader type to use
		PixelShaderType = GetPixelShaderType();
		if (EMpcdiShaderType::Invalid == PixelShaderType)
		{
			//@todo: Handle error
			return false;
		}

		bIsBlendDisabled = !MPCDIRegion->AlphaMap.IsInitialized();

		const EVarMPCDIShaderType ShaderType = (EVarMPCDIShaderType)CVarMPCDIShaderType.GetValueOnAnyThread();

		switch (ShaderType)
		{
		case EVarMPCDIShaderType::DisableBlend:
			bIsBlendDisabled = true;
			break;

		case EVarMPCDIShaderType::Passthrough:
			PixelShaderType = EMpcdiShaderType::Passthrough;
			break;

		case EVarMPCDIShaderType::Disable:
			return false;
			break;
		};

		return true;
	}

	bool Render(FRHICommandListImmediate& RHICmdList)
	{
		// Setup viewport before render
		FIntRect MPCDIViewportRect = GetViewportRect();
		RHICmdList.SetViewport(MPCDIViewportRect.Min.X, MPCDIViewportRect.Min.Y, 0.0f, MPCDIViewportRect.Max.X, MPCDIViewportRect.Max.Y, 1.0f);

		// Render
		switch (PixelShaderType)
		{
			case EMpcdiShaderType::Passthrough:
			{
				ShaderInputData.UVMatrix = FMatrix::Identity;
				FMpcdiRenderPassData RenderPassData;
				InitRenderPass(RenderPassData);
				RenderPassthrough(RHICmdList, RenderPassData);

				break;
			}

			case EMpcdiShaderType::WarpAndBlend:
			{
				ShaderInputData.UVMatrix = ShaderInputData.Frustum.UVMatrix*MPCDIData->GetTextureMatrix()*MPCDIRegion->GetRegionMatrix();
				FMpcdiRenderPassData RenderPassData;
				InitRenderPass(RenderPassData);
				RenderCurentPass(RHICmdList, RenderPassData);

				break;
			}

			default:
				return false;
		}

		return true;
	}
};

DECLARE_GPU_STAT_NAMED(DisplayClusterMpcdiWarpBlend, MpcdiStrings::RenderPassHint);

bool FMPCDIShader::ApplyWarpBlend(FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& InTextureWarpData, IMPCDI::FShaderInputData& InShaderInputData, FMPCDIData* InMPCDIData)
{
	check(IsInRenderingThread());

	FMpcdiPassRenderer MpcdiPassRenderer(InTextureWarpData, InShaderInputData, InMPCDIData);

	if (!MpcdiPassRenderer.Initialize())
	{
		return false;
	}

	SCOPED_GPU_STAT(RHICmdList, DisplayClusterMpcdiWarpBlend);
	SCOPED_DRAW_EVENTF(RHICmdList, DisplayClusterMpcdiWarpBlend, MpcdiStrings::RenderPassName);

	// Do single-pass warp&blend render
	FRHIRenderPassInfo RPInfo(InTextureWarpData.DstTexture, ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, MpcdiStrings::RenderPassName);
	bool bIsRenderSuccess = MpcdiPassRenderer.Render(RHICmdList);
	RHICmdList.EndRenderPass();

	return bIsRenderSuccess;
};
