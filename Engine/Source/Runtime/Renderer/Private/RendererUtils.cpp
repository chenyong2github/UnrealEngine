// Copyright Epic Games, Inc. All Rights Reserved.

#include "RendererUtils.h"
#include "RenderTargetPool.h"
#include "VisualizeTexture.h"
#include "SceneUtils.h"
#include "ScenePrivateBase.h"
#include "SceneRendering.h"
#include "SceneFilterRendering.h"
#include "CommonRenderResources.h"
#include "ScreenPass.h"

class FRTWriteMaskDecodeCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FRTWriteMaskDecodeCS);

	static const uint32 MaxRenderTargetCount = 4;
	static const uint32 ThreadGroupSizeX = 8;
	static const uint32 ThreadGroupSizeY = 8;

	class FNumRenderTargets : SHADER_PERMUTATION_RANGE_INT("NUM_RENDER_TARGETS", 1, MaxRenderTargetCount);
	using FPermutationDomain = TShaderPermutationDomain<FNumRenderTargets>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ReferenceInput)
		SHADER_PARAMETER_RDG_TEXTURE_SRV_ARRAY(Buffer<uint>, RTWriteMaskInputs, [MaxRenderTargetCount])
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutCombinedRTWriteMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool IsSupported(uint32 NumRenderTargets)
	{
		return NumRenderTargets == 1 || NumRenderTargets == 3;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		const uint32 NumRenderTargets = PermutationVector.Get<FNumRenderTargets>();

		if (!IsSupported(NumRenderTargets))
		{
			return false;
		}

		return RHISupportsRenderTargetWriteMask(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), ThreadGroupSizeY);
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	FRTWriteMaskDecodeCS() = default;
	FRTWriteMaskDecodeCS(const ShaderMetaType::CompiledShaderInitializerType & Initializer)
		: FGlobalShader(Initializer)
	{
		PlatformDataParam.Bind(Initializer.ParameterMap, TEXT("PlatformData"), SPF_Mandatory);
		BindForLegacyShaderParameters<FParameters>(this, Initializer.PermutationId, Initializer.ParameterMap);
	}

	// Shader parameter structs don't have a way to push variable sized data yet. So the we use the old shader parameter API.
	void SetPlatformData(FRHIComputeCommandList& RHICmdList, const void* PlatformDataPtr, uint32 PlatformDataSize)
	{
		RHICmdList.SetShaderParameter(RHICmdList.GetBoundComputeShader(), PlatformDataParam.GetBufferIndex(), PlatformDataParam.GetBaseIndex(), PlatformDataSize, PlatformDataPtr);
	}

private:
	LAYOUT_FIELD(FShaderParameter, PlatformDataParam);
};

IMPLEMENT_GLOBAL_SHADER(FRTWriteMaskDecodeCS, "/Engine/Private/RTWriteMaskDecode.usf", "RTWriteMaskDecodeMain", SF_Compute);

void FRenderTargetWriteMask::Decode(
	FRHICommandListImmediate& RHICmdList,
	FGlobalShaderMap* ShaderMap,
	TArrayView<IPooledRenderTarget* const> InRenderTargets,
	TRefCountPtr<IPooledRenderTarget>& OutRTWriteMask,
	ETextureCreateFlags RTWriteMaskFastVRamConfig,
	const TCHAR* RTWriteMaskDebugName)
{
	FRDGBuilder GraphBuilder(RHICmdList);

	TArray<FRDGTextureRef, SceneRenderingAllocator> InputTextures;
	InputTextures.Reserve(InRenderTargets.Num());
	for (IPooledRenderTarget* RenderTarget : InRenderTargets)
	{
		InputTextures.Add(GraphBuilder.RegisterExternalTexture(RenderTarget));
	}

	FRDGTextureRef OutputTexture = nullptr;
	Decode(GraphBuilder, ShaderMap, InputTextures, OutputTexture, RTWriteMaskFastVRamConfig, RTWriteMaskDebugName);

	GraphBuilder.QueueTextureExtraction(OutputTexture, &OutRTWriteMask);
	GraphBuilder.Execute();
}

void FRenderTargetWriteMask::Decode(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	TArrayView<FRDGTextureRef const> RenderTargets,
	FRDGTextureRef& OutRTWriteMask,
	ETextureCreateFlags RTWriteMaskFastVRamConfig,
	const TCHAR* RTWriteMaskDebugName)
{
	const uint32 NumRenderTargets = RenderTargets.Num();

	check(RHISupportsRenderTargetWriteMask(GMaxRHIShaderPlatform));
	checkf(FRTWriteMaskDecodeCS::IsSupported(NumRenderTargets), TEXT("FRenderTargetWriteMask::Decode does not currently support decoding %d render targets."), RenderTargets.Num());

	FRDGTextureRef Texture0 = RenderTargets[0];

	const FIntPoint RTWriteMaskDims(
		FMath::DivideAndRoundUp(Texture0->Desc.Extent.X, (int32)FRTWriteMaskDecodeCS::ThreadGroupSizeX),
		FMath::DivideAndRoundUp(Texture0->Desc.Extent.Y, (int32)FRTWriteMaskDecodeCS::ThreadGroupSizeY));

	// Allocate the Mask from the render target pool.
	const FRDGTextureDesc MaskDesc = FRDGTextureDesc::Create2D(
		RTWriteMaskDims,
		NumRenderTargets <= 2 ? PF_R8_UINT : PF_R16_UINT,
		FClearValueBinding::None,
		RTWriteMaskFastVRamConfig | TexCreate_UAV | TexCreate_RenderTargetable | TexCreate_ShaderResource);

	OutRTWriteMask = GraphBuilder.CreateTexture(MaskDesc, RTWriteMaskDebugName);

	auto* PassParameters = GraphBuilder.AllocParameters<FRTWriteMaskDecodeCS::FParameters>();
	PassParameters->ReferenceInput = Texture0;
	PassParameters->OutCombinedRTWriteMask = GraphBuilder.CreateUAV(OutRTWriteMask);

	for (uint32 Index = 0; Index < NumRenderTargets; ++Index)
	{
		PassParameters->RTWriteMaskInputs[Index] = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMetaData(RenderTargets[Index], ERDGTextureMetaDataAccess::CMask));
	}

	FRTWriteMaskDecodeCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FRTWriteMaskDecodeCS::FNumRenderTargets>(NumRenderTargets);
	TShaderMapRef<FRTWriteMaskDecodeCS> DecodeCS(ShaderMap, PermutationVector);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("DecodeWriteMask[%d]", NumRenderTargets),
		PassParameters,
		ERDGPassFlags::Compute,
		[DecodeCS, PassParameters, ShaderMap, RTWriteMaskDims](FRHIComputeCommandList& RHICmdList)
	{
		FRHITexture* Texture0RHI = PassParameters->ReferenceInput->GetRHI();

		// Retrieve the platform specific data that the decode shader needs.
		void* PlatformDataPtr = nullptr;
		uint32 PlatformDataSize = 0;
		Texture0RHI->GetWriteMaskProperties(PlatformDataPtr, PlatformDataSize);
		check(PlatformDataSize > 0);

		if (PlatformDataPtr == nullptr)
		{
			// If the returned pointer was null, the platform RHI wants us to allocate the memory instead.
			PlatformDataPtr = alloca(PlatformDataSize);
			Texture0RHI->GetWriteMaskProperties(PlatformDataPtr, PlatformDataSize);
		}

		RHICmdList.SetComputeShader(DecodeCS.GetComputeShader());
		SetShaderParameters(RHICmdList, DecodeCS, DecodeCS.GetComputeShader(), *PassParameters);
		DecodeCS->SetPlatformData(RHICmdList, PlatformDataPtr, PlatformDataSize);

		RHICmdList.DispatchComputeShader(
			FMath::DivideAndRoundUp((uint32)RTWriteMaskDims.X, FRTWriteMaskDecodeCS::ThreadGroupSizeX),
			FMath::DivideAndRoundUp((uint32)RTWriteMaskDims.Y, FRTWriteMaskDecodeCS::ThreadGroupSizeY),
			1);
	});
}

using namespace RendererUtils;

IMPLEMENT_GLOBAL_SHADER(FScreenRectangleVS, "/Engine/Private/RenderGraphUtilities.usf", "ScreenRectangleVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FHorizontalBlurPS, "/Engine/Private/RenderGraphUtilities.usf", "HorizontalBlurPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FVerticalBlurPS, "/Engine/Private/RenderGraphUtilities.usf", "VerticalBlurPS", SF_Pixel);

const FIntPoint FHorizontalBlurCS::TexelsPerThreadGroup(ThreadGroupSizeX, ThreadGroupSizeY);
const FIntPoint FVerticalBlurCS::TexelsPerThreadGroup(ThreadGroupSizeX, ThreadGroupSizeY);
IMPLEMENT_GLOBAL_SHADER(FHorizontalBlurCS, "/Engine/Private/RenderGraphUtilities.usf", "HorizontalBlurCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVerticalBlurCS, "/Engine/Private/RenderGraphUtilities.usf", "VerticalBlurCS", SF_Compute);

void RendererUtils::AddGaussianBlurFilter_InternalPS(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureSRVRef InTexture,
	FRDGTextureRef OutTexture,
	TShaderRef<FGaussianBlurPS> PixelShader)
{
	FScreenPassRenderTarget ScreenPassRenderTarget(OutTexture, ERenderTargetLoadAction::EClear);

	FIntVector TextureSize = OutTexture->Desc.GetSize();
	FIntPoint BufferSize = FIntPoint(TextureSize.X, TextureSize.Y);

	FGaussianBlurPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGaussianBlurPS::FParameters>();
	PassParameters->SourceTexture = InTexture;
	PassParameters->SourceTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->BufferSizeAndInvSize = FVector4(BufferSize.X, BufferSize.Y, 1.0f / BufferSize.X, 1.0f / BufferSize.Y);
	PassParameters->RenderTargets[0] = ScreenPassRenderTarget.GetRenderTargetBinding();

	TShaderMapRef<FScreenRectangleVS> VertexShader(View.ShaderMap);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("GaussianBlurFilter %dx%d (PS)", BufferSize.X, BufferSize.Y),
		PassParameters,
		ERDGPassFlags::Raster,
		[VertexShader, PixelShader, PassParameters, BufferSize](FRHICommandList& RHICmdList)
		{
			RHICmdList.SetViewport(0.0f, 0.0f, 0.0f, BufferSize.X, BufferSize.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

			DrawRectangle(
				RHICmdList,
				0, 0,
				BufferSize.X, BufferSize.Y,
				0, 0,
				BufferSize.X, BufferSize.Y,
				BufferSize,
				BufferSize,
				VertexShader,
				EDRF_UseTriangleOptimization);
		});
}

void RendererUtils::AddGaussianBlurFilter_InternalCS(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureSRVRef InTexture,
	FRDGTextureUAVRef OutTexture,
	TShaderRef<FGaussianBlurCS> ComputeShader,
	FIntPoint TexelsPerThreadGroup)
{
	FIntVector TextureSize = OutTexture->Desc.Texture->Desc.GetSize();
	FIntPoint BufferSize = FIntPoint(TextureSize.X, TextureSize.Y);

	FGaussianBlurCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGaussianBlurCS::FParameters>();
	PassParameters->SourceTexture = InTexture;
	PassParameters->SourceTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->BufferSizeAndInvSize = FVector4(BufferSize.X, BufferSize.Y, 1.0f / BufferSize.X, 1.0f / BufferSize.Y);
	PassParameters->RWOutputTexture = OutTexture;

	FComputeShaderUtils::AddPass(GraphBuilder,
		RDG_EVENT_NAME("GaussianBlurFilter %dx%d (CS)", BufferSize.X, BufferSize.Y),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(BufferSize, TexelsPerThreadGroup));
}

void RendererUtils::AddGaussianBlurFilter(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef SourceTexture,
	FRDGTextureRef HorizontalBlurTexture,
	FRDGTextureRef VerticalBlurTexture,
	bool bUseComputeShader)
{
	FRDGTextureSRVRef SourceTextureSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SourceTexture));
	FRDGTextureSRVRef HorizontalBlurTextureSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(HorizontalBlurTexture));

	if (bUseComputeShader)
	{
		FRDGTextureUAVRef HorizontalBlurTextureUAV = GraphBuilder.CreateUAV(HorizontalBlurTexture);
		FRDGTextureUAVRef VerticalBlurTextureUAV = GraphBuilder.CreateUAV(VerticalBlurTexture);

		// Horizontal Blur
		TShaderMapRef<FHorizontalBlurCS> HorizontalBlurCS(View.ShaderMap);
		AddGaussianBlurFilter_InternalCS(GraphBuilder, View, SourceTextureSRV, HorizontalBlurTextureUAV, HorizontalBlurCS, FHorizontalBlurCS::TexelsPerThreadGroup);

		// Vertical Blur
		TShaderMapRef<FVerticalBlurCS> VerticalBlurCS(View.ShaderMap);
		AddGaussianBlurFilter_InternalCS(GraphBuilder, View, HorizontalBlurTextureSRV, VerticalBlurTextureUAV, VerticalBlurCS, FVerticalBlurCS::TexelsPerThreadGroup);
	}
	else
	{
		// Horizontal Blur
		TShaderMapRef<FHorizontalBlurPS> HorizontalBlurPS(View.ShaderMap);
		AddGaussianBlurFilter_InternalPS(GraphBuilder, View, SourceTextureSRV, HorizontalBlurTexture, HorizontalBlurPS);

		// Vertical Blur
		TShaderMapRef<FVerticalBlurPS> VerticalBlurPS(View.ShaderMap);
		AddGaussianBlurFilter_InternalPS(GraphBuilder, View, HorizontalBlurTextureSRV, VerticalBlurTexture, VerticalBlurPS);
	}
}
