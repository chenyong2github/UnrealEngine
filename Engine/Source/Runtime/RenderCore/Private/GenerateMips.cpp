// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenerateMips.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterUtils.h"
#include "PipelineStateCache.h"
#include "GlobalShader.h"
#include "CommonRenderResources.h"
#include "RHIStaticStates.h"

class FGenerateMipsCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGenerateMipsCS)
	SHADER_USE_PARAMETER_STRUCT(FGenerateMipsCS, FGlobalShader)

	class FGenMipsSRGB : SHADER_PERMUTATION_BOOL("GENMIPS_SRGB");
	class FGenMipsSwizzle : SHADER_PERMUTATION_BOOL("GENMIPS_SWIZZLE");
	using FPermutationDomain = TShaderPermutationDomain<FGenMipsSRGB, FGenMipsSwizzle>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2D, TexelSize)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, MipInSRV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, MipOutUAV)
		SHADER_PARAMETER_SAMPLER(SamplerState, MipSampler)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FShaderPermutationParameters&, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("GENMIPS_COMPUTE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FGenerateMipsCS, "/Engine/Private/ComputeGenerateMips.usf", "MainCS", SF_Compute);

class FGenerateMipsVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGenerateMipsVS);
	SHADER_USE_PARAMETER_STRUCT(FGenerateMipsVS, FGlobalShader);
	using FParameters = FEmptyShaderParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}

	static void ModifyCompilationEnvironment(const FShaderPermutationParameters&, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("GENMIPS_COMPUTE"), 0);
	}
};

IMPLEMENT_GLOBAL_SHADER(FGenerateMipsVS, "/Engine/Private/ComputeGenerateMips.usf", "MainVS", SF_Vertex);

class FGenerateMipsPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGenerateMipsPS);
	SHADER_USE_PARAMETER_STRUCT(FGenerateMipsPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2D, HalfTexelSize)
		SHADER_PARAMETER(float, Level)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, MipInSRV)
		SHADER_PARAMETER_SAMPLER(SamplerState, MipSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}

	static void ModifyCompilationEnvironment(const FShaderPermutationParameters&, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("GENMIPS_COMPUTE"), 0);
	}
};

IMPLEMENT_GLOBAL_SHADER(FGenerateMipsPS, "/Engine/Private/ComputeGenerateMips.usf", "MainPS", SF_Pixel);

void FGenerateMips::ExecuteRaster(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, FRHISamplerState* Sampler)
{
	check(Texture);
	check(Sampler);

	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FGenerateMipsVS> VertexShader(ShaderMap);
	TShaderMapRef<FGenerateMipsPS> PixelShader(ShaderMap);

	const FRDGTextureDesc& TextureDesc = Texture->Desc;

	for (uint32 MipLevel = 1, MipCount = TextureDesc.NumMips; MipLevel < MipCount; ++MipLevel)
	{
		const uint32 InputMipLevel = MipLevel - 1;

		const FIntPoint DestTextureSize(
			FMath::Max(TextureDesc.Extent.X >> MipLevel, 1),
			FMath::Max(TextureDesc.Extent.Y >> MipLevel, 1));

		FGenerateMipsPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGenerateMipsPS::FParameters>();
		PassParameters->HalfTexelSize = FVector2D(0.5f / DestTextureSize.X, 0.5f / DestTextureSize.Y);
		PassParameters->Level = InputMipLevel;
		PassParameters->MipInSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(Texture, InputMipLevel));
		PassParameters->MipSampler = Sampler;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(Texture, ERenderTargetLoadAction::ELoad, MipLevel);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("GenerateMips DestMipLevel=%d", MipLevel),
			PassParameters,
			ERDGPassFlags::Raster,
			[VertexShader, PixelShader, DestTextureSize](FRHICommandList& RHICmdList)
		{
			RHICmdList.SetViewport(0.0f, 0.0f, 0.0f, (float)DestTextureSize.X, (float)DestTextureSize.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGBA, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI();
			GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			RHICmdList.SetStreamSource(0, GScreenRectangleVertexBuffer.VertexBufferRHI, 0);
			RHICmdList.DrawPrimitive(0, 2, 1);
		});
	}
}

void FGenerateMips::ExecuteCompute(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, FRHISamplerState* Sampler)
{
	check(Texture);
	check(Sampler);

	const FRDGTextureDesc& TextureDesc = Texture->Desc;

	// Select compute shader variant (normal vs. sRGB etc.)
	bool bMipsSRGB = EnumHasAnyFlags(TextureDesc.Flags, TexCreate_SRGB);
#if PLATFORM_ANDROID
	if (IsVulkanPlatform(GMaxRHIShaderPlatform))
	{
		// Vulkan Android seems to skip sRGB->Lin conversion when sampling texture in compute
		bMipsSRGB = false;
	}
#endif
	const bool bMipsSwizzle = false; 

	FGenerateMipsCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FGenerateMipsCS::FGenMipsSRGB>(bMipsSRGB);
	PermutationVector.Set<FGenerateMipsCS::FGenMipsSwizzle>(bMipsSwizzle);
	TShaderMapRef<FGenerateMipsCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

	// Loop through each level of the mips that require creation and add a dispatch pass per level.
	for (uint32 MipLevel = 1, MipCount = TextureDesc.NumMips; MipLevel < MipCount; ++MipLevel)
	{
		const FIntPoint DestTextureSize(
			FMath::Max(TextureDesc.Extent.X >> MipLevel, 1),
			FMath::Max(TextureDesc.Extent.Y >> MipLevel, 1));

		FGenerateMipsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGenerateMipsCS::FParameters>();
		PassParameters->TexelSize  = FVector2D(1.0f / DestTextureSize.X, 1.0f / DestTextureSize.Y);
		PassParameters->MipInSRV   = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(Texture, MipLevel - 1));
		PassParameters->MipOutUAV  = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Texture, MipLevel));
		PassParameters->MipSampler = Sampler;

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("GenerateMips DestMipLevel=%d", MipLevel),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(DestTextureSize, FComputeShaderUtils::kGolden2DGroupSize));
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FCopyDestParameters, )
	RDG_TEXTURE_ACCESS(Texture, ERHIAccess::CopyDest)
END_SHADER_PARAMETER_STRUCT()

void FGenerateMips::Execute(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, FGenerateMipsParams Params, EGenerateMipsPass Pass)
{
	if (Texture->Desc.NumMips > 1)
	{
		if (RHIRequiresComputeGenerateMips())
		{
			FSamplerStateInitializerRHI SamplerInit(Params.Filter, Params.AddressU, Params.AddressV, Params.AddressW);
			Execute(GraphBuilder, Texture, RHICreateSamplerState(SamplerInit), Pass);
		}
		else
		{
			FCopyDestParameters* PassParameters = GraphBuilder.AllocParameters<FCopyDestParameters>();
			PassParameters->Texture = Texture;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("GenerateMipsTexture"),
				PassParameters,
				ERDGPassFlags::Copy,
				[Texture](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.GenerateMips(Texture->GetRHI());
			});
		}
	}
}

void FGenerateMips::Execute(FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, TSharedPtr<FGenerateMipsStruct>& ExternalMipsStructCache, FGenerateMipsParams Params, bool bAllowRenderBasedGeneration)
{
	TRefCountPtr<IPooledRenderTarget> PooledRenderTarget = CreateRenderTarget(Texture, TEXT("MipGeneration"));

	FRDGBuilder GraphBuilder(RHICmdList);
	Execute(GraphBuilder, GraphBuilder.RegisterExternalTexture(PooledRenderTarget), Params, bAllowRenderBasedGeneration ? EGenerateMipsPass::Raster : EGenerateMipsPass::Compute);
	GraphBuilder.Execute();
}

void FGenerateMips::Execute(FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, FGenerateMipsParams Params, bool bAllowRenderBasedGeneration)
{
	TRefCountPtr<IPooledRenderTarget> PooledRenderTarget = CreateRenderTarget(Texture, TEXT("MipGeneration"));

	FRDGBuilder GraphBuilder(RHICmdList);
	Execute(GraphBuilder, GraphBuilder.RegisterExternalTexture(PooledRenderTarget), Params, bAllowRenderBasedGeneration ? EGenerateMipsPass::Raster : EGenerateMipsPass::Compute);
	GraphBuilder.Execute();
}
