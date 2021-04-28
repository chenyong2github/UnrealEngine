// Copyright Epic Games, Inc. All Rights Reserved.

#include "RendererUtils.h"
#include "RenderTargetPool.h"
#include "VisualizeTexture.h"
#include "SceneUtils.h"
#include "ScenePrivateBase.h"
#include "SceneRendering.h"
#include "SceneFilterRendering.h"
#include "CommonRenderResources.h"

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
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	FRHITexture* SourceTexture,
	TRefCountPtr<IPooledRenderTarget>& RenderTarget,
	TShaderRef<FGaussianBlurPS> PixelShader)
{
	FSceneRenderTargetItem& RenderTargetItem = RenderTarget->GetRenderTargetItem();
	FIntVector RenderTargetSize = RenderTargetItem.TargetableTexture->GetSizeXYZ();
	FIntPoint BufferSize = FIntPoint(RenderTargetSize.X, RenderTargetSize.Y);

	FRHIRenderPassInfo HorizontalBlurRPInfo(RenderTargetItem.TargetableTexture, ERenderTargetActions::Clear_Store);
	RHICmdList.BeginRenderPass(HorizontalBlurRPInfo, TEXT("BlurRenderPass"));
	{
		RHICmdList.SetViewport(0.0f, 0.0f, 0.0f, BufferSize.X, BufferSize.Y, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		TShaderMapRef<FScreenRectangleVS> VertexShader(View.ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		PixelShader->SetParameters(RHICmdList, SourceTexture, FVector4(BufferSize.X, BufferSize.Y, 1.0f / BufferSize.X, 1.0f / BufferSize.Y));

		FIntPoint UV = 0;
		FIntPoint UVSize = BufferSize;
		if (RHINeedsToSwitchVerticalAxis(GShaderPlatformForFeatureLevel[View.FeatureLevel]) && !IsMobileHDR())
		{
			UV.Y = UV.Y + UVSize.Y;
			UVSize.Y = -UVSize.Y;
		}

		DrawRectangle(
			RHICmdList,
			0, 0,
			BufferSize.X, BufferSize.Y,
			UV.X, UV.Y,
			UVSize.X, UVSize.Y,
			BufferSize,
			BufferSize,
			VertexShader,
			EDRF_UseTriangleOptimization);
	}
	RHICmdList.EndRenderPass();
}

void RendererUtils::AddGaussianBlurFilter_InternalCS(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	FRHITexture* SourceTexture,
	TRefCountPtr<IPooledRenderTarget>& RenderTarget,
	TShaderRef<FGaussianBlurCS> ComputeShader,
	FIntPoint TexelsPerThreadGroup)
{
	FSceneRenderTargetItem& RenderTargetItem = RenderTarget->GetRenderTargetItem();
	check(RenderTargetItem.UAV);

	FIntVector RenderTargetSize = RenderTargetItem.TargetableTexture->GetSizeXYZ();
	FIntPoint BufferSize = FIntPoint(RenderTargetSize.X, RenderTargetSize.Y);

	FGaussianBlurCS::FParameters PassParameters;
	PassParameters.SourceTexture = SourceTexture;
	PassParameters.SourceTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters.BufferSizeAndInvSize = FVector4(BufferSize.X, BufferSize.Y, 1.0f / BufferSize.X, 1.0f / BufferSize.Y);
	PassParameters.RWOutputTexture = RenderTargetItem.UAV;

	FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(BufferSize, TexelsPerThreadGroup);
	FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, PassParameters, GroupCount);
}


void RendererUtils::AddGaussianBlurFilter(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	FRHITexture* SourceTexture,
	TRefCountPtr<IPooledRenderTarget>& HorizontalBlurRenderTarget,
	TRefCountPtr<IPooledRenderTarget>& VerticalBlurRenderTarget,
	bool bIsComputePass)
{
	if (bIsComputePass)
	{
		// Horizontal Blur
		TShaderMapRef<FHorizontalBlurCS> HorizontalBlurCS(View.ShaderMap);
		AddGaussianBlurFilter_InternalCS(RHICmdList, View, SourceTexture, HorizontalBlurRenderTarget, HorizontalBlurCS, FHorizontalBlurCS::TexelsPerThreadGroup);

		// Vertical Blur
		TShaderMapRef<FVerticalBlurCS> VerticalBlurCS(View.ShaderMap);
		AddGaussianBlurFilter_InternalCS(RHICmdList, View, HorizontalBlurRenderTarget->GetRenderTargetItem().TargetableTexture, VerticalBlurRenderTarget, VerticalBlurCS, FVerticalBlurCS::TexelsPerThreadGroup);
	}
	else
	{
		// Horizontal Blur
		TShaderMapRef<FHorizontalBlurPS> HorizontalBlurPS(View.ShaderMap);
		AddGaussianBlurFilter_InternalPS(RHICmdList, View, SourceTexture, HorizontalBlurRenderTarget, HorizontalBlurPS);

		// Vertical Blur
		TShaderMapRef<FVerticalBlurPS> VerticalBlurPS(View.ShaderMap);
		AddGaussianBlurFilter_InternalPS(RHICmdList, View, HorizontalBlurRenderTarget->GetRenderTargetItem().TargetableTexture, VerticalBlurRenderTarget, VerticalBlurPS);
	}
}
