// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterShadersPostprocess_OutputRemap.h"

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"

#include "CommonRenderResources.h"
#include "PixelShaderUtils.h"

#include "ShaderParameterUtils.h"

#include "HAL/IConsoleManager.h"

#include "RenderResource.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "ShaderParameterStruct.h"

#include "ClearQuad.h"

#include "Render/Containers/DisplayClusterRender_MeshComponent.h"
#include "Render/Containers/DisplayClusterRender_MeshComponentProxy.h"
#include "Render/Containers/DisplayClusterRender_MeshComponentProxyData.h"
#include "Render/Containers/DisplayClusterRender_MeshGeometry.h"

#define OutputRemapShaderFileName TEXT("/Plugin/nDisplay/Private/OutputRemapShaders.usf")

// Select output remap shader
enum class EVarOutputRemapShaderType : uint8
{
	Default,
	Passthrough,
	Disable,
};

static TAutoConsoleVariable<int32> CVarOutputRemapShaderType(
	TEXT("nDisplay.render.output_remap.shader"),
	(int)EVarOutputRemapShaderType::Default,
	TEXT("Select shader for output remap:\n")	
	TEXT(" 0: default remap shader\n")
	TEXT(" 1: pass throught shader, test rect mesh\n")
	TEXT(" 2: Disable remap shaders\n")
	,ECVF_RenderThreadSafe
);

// Enable/Disable ClearTexture for OutputRemap RTT
static TAutoConsoleVariable<int32> CVarClearOutputRemapFrameTextureEnabled(
	TEXT("nDisplay.render.output_remap.ClearTextureEnabled"),
	1,
	TEXT("Enables OutputRemap RTT clearing before postprocessing.\n")
	TEXT("0 : disabled\n")
	TEXT("1 : enabled\n")
	,
	ECVF_RenderThreadSafe
);


BEGIN_SHADER_PARAMETER_STRUCT(FOutputRemapVertexShaderParameters, )
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FOutputRemapPixelShaderParameters, )
	SHADER_PARAMETER_TEXTURE(Texture2D,    PostprocessInput0)
	SHADER_PARAMETER_SAMPLER(SamplerState, PostprocessInput0Sampler)
END_SHADER_PARAMETER_STRUCT()

class FOutputRemapPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FOutputRemapPS);
	SHADER_USE_PARAMETER_STRUCT(FOutputRemapPS, FGlobalShader);

	using FParameters = FOutputRemapPixelShaderParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{ return true; }
};

IMPLEMENT_SHADER_TYPE(, FOutputRemapPS, OutputRemapShaderFileName, TEXT("OutputRemap_PS"), SF_Pixel);

class FOutputRemapVS : public FGlobalShader
	{
public:
	DECLARE_GLOBAL_SHADER(FOutputRemapVS);
	SHADER_USE_PARAMETER_STRUCT(FOutputRemapVS, FGlobalShader);

	using FParameters = FOutputRemapVertexShaderParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};

IMPLEMENT_SHADER_TYPE(, FOutputRemapVS, OutputRemapShaderFileName, TEXT("OutputRemap_VS"), SF_Vertex);

DECLARE_GPU_STAT_NAMED(nDisplay_PostProcess_OutputRemap, TEXT("nDisplay PostProcess::OutputRemap"));

bool FDisplayClusterShadersPostprocess_OutputRemap::RenderPostprocess_OutputRemap(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InSourceTexture, FRHITexture2D* InRenderTargetableDestTexture, const FDisplayClusterRender_MeshComponentProxy& InMeshProxy)
{
	check(IsInRenderingThread());
	if (InSourceTexture == nullptr || InRenderTargetableDestTexture == nullptr)
	{
		return false;
	}

	FDisplayClusterRender_MeshComponentProxy const* MeshProxy = &InMeshProxy;

	const EVarOutputRemapShaderType ShaderType = (EVarOutputRemapShaderType)CVarOutputRemapShaderType.GetValueOnAnyThread();
	switch (ShaderType)
	{
		case EVarOutputRemapShaderType::Passthrough:
		{
			// Use simple 1:1 test mesh for shader forwarding
			static FDisplayClusterRender_MeshComponent TestMesh_Passthrough;
			if (TestMesh_Passthrough.GetProxy())
			{
				if (!TestMesh_Passthrough.GetProxy()->IsValid_RenderThread())
				{
					FDisplayClusterRender_MeshGeometry PassthroughMeshGeometry(EDisplayClusterRender_MeshGeometryCreateType::Passthrough);
					FDisplayClusterRender_MeshComponentProxyData ProxyData(FDisplayClusterRender_MeshComponentProxyDataFunc::OutputRemapScreenSpace, PassthroughMeshGeometry);
					TestMesh_Passthrough.GetProxy()->UpdateRHI_RenderThread(RHICmdList, &ProxyData);
				}

				MeshProxy = TestMesh_Passthrough.GetProxy();
			}
			break;
		}

		case EVarOutputRemapShaderType::Default:
			break;

		case EVarOutputRemapShaderType::Disable:
		default:
			return false;
	};

	bool bResult = false;

	SCOPED_GPU_STAT(RHICmdList, nDisplay_PostProcess_OutputRemap);
	SCOPED_DRAW_EVENT(RHICmdList, nDisplay_PostProcess_OutputRemap);

	const ERHIFeatureLevel::Type RenderFeatureLevel = GMaxRHIFeatureLevel;
	const auto GlobalShaderMap = GetGlobalShaderMap(RenderFeatureLevel);

	const FIntPoint TargetSizeXY = InRenderTargetableDestTexture->GetSizeXY();

	TShaderMapRef<FOutputRemapVS> VertexShader(GlobalShaderMap);
	TShaderMapRef<FOutputRemapPS> PixelShader(GlobalShaderMap);

	FRHIRenderPassInfo RPInfo(InRenderTargetableDestTexture, ERenderTargetActions::DontLoad_Store);
	TransitionRenderPassTargets(RHICmdList, RPInfo);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("nDisplay_OutputRemap"));

	const bool bClearOutputRemapFrameTextureEnabled = CVarClearOutputRemapFrameTextureEnabled.GetValueOnRenderThread() != 0;
	if (bClearOutputRemapFrameTextureEnabled)
	{
		// Do clear
		const FIntPoint Size = InRenderTargetableDestTexture->GetSizeXY();
		RHICmdList.SetViewport(0, 0, 0.0f, Size.X, Size.Y, 1.0f);
		DrawClearQuad(RHICmdList, FLinearColor::Black);
	}
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		if (MeshProxy->BeginRender_RenderThread(RHICmdList, GraphicsPSOInit))
		{
			GraphicsPSOInit.BlendState = TStaticBlendState <>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			FPixelShaderUtils::DrawFullscreenQuad(RHICmdList, 1);

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			FOutputRemapPixelShaderParameters PSParameters;
			{
				PSParameters.PostprocessInput0 = InSourceTexture;
				PSParameters.PostprocessInput0Sampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			}
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PSParameters);

			MeshProxy->FinishRender_RenderThread(RHICmdList);
			bResult = true;
		}
	}
	RHICmdList.EndRenderPass();

	return bResult;
}
