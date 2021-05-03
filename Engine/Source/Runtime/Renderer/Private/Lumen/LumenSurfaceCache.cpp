// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenScenePrefilter.cpp
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "LumenSceneUtils.h"
#include "PixelShaderUtils.h"

int32 GLumenSceneSurfaceCacheAtlasSize = 4096;
FAutoConsoleVariableRef CVarLumenSceneSurfaceCacheAtlasSize(
	TEXT("r.LumenScene.SurfaceCache.AtlasSize"),
	GLumenSceneSurfaceCacheAtlasSize,
	TEXT("Surface cache card atlas size."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSurfaceCacheCompress = 0;
FAutoConsoleVariableRef CVarLumenSurfaceCacheCompress(
	TEXT("r.LumenScene.SurfaceCache.Compress"),
	GLumenSurfaceCacheCompress,
	TEXT("Whether to use run time compression for surface cache.\n")
	TEXT("0 - Disabled\n")
	TEXT("1 - Compress using UAV aliasing if supported\n")
	TEXT("2 - Compress using CopyTexture (may be very slow on some RHIs)\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

enum class ELumenSurfaceCacheLayer : uint8
{
	Depth,
	Albedo,
	Opacity,
	Normal,
	Emissive,

	MAX
};

struct FLumenSurfaceLayerConfig
{
	const TCHAR* Name;
	EPixelFormat UncompressedFormat;
	EPixelFormat CompressedFormat;
	EPixelFormat CompressedUAVFormat;
	FVector ClearValue;
};

const FLumenSurfaceLayerConfig& GetSurfaceLayerConfig(ELumenSurfaceCacheLayer Layer)
{
	static FLumenSurfaceLayerConfig Configs[(uint32)ELumenSurfaceCacheLayer::MAX] =
	{
		{ TEXT("Depth"),	PF_G16R16,		PF_BC5,			PF_R32G32B32A32_UINT,	FVector(0.0f, 1.0f, 0.0f)		},
		{ TEXT("Albedo"),	PF_R8G8B8A8,	PF_BC7,			PF_R32G32B32A32_UINT,	FVector(0.0f, 1.0f, 1.0f)		},
		{ TEXT("Opacity"),	PF_G8,			PF_BC4,			PF_R32G32_UINT,			FVector(1.0f, 0.0f, 0.0f)		},
		{ TEXT("Normal"),	PF_R32G32_UINT,	PF_R32G32_UINT, PF_R32G32B32A32_UINT,	FVector(0.0f, 0.0f, 0.0f)		},
		{ TEXT("Emissive"), PF_R32G32_UINT,	PF_R32G32_UINT, PF_R32G32B32A32_UINT,	FVector(1000.0f, 1000.0f, 0.0f)	}
	};

	check((uint32)Layer < UE_ARRAY_COUNT(Configs));

	return Configs[(uint32)Layer];
}

class FLumenCardCopyPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardCopyPS);
	SHADER_USE_PARAMETER_STRUCT(FLumenCardCopyPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint4>, RWAtlasBlock4)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint2>, RWAtlasBlock2)
		SHADER_PARAMETER(FVector2D, OneOverSourceAtlasSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceAtlas)
	END_SHADER_PARAMETER_STRUCT()

	class FSurfaceCacheLayer : SHADER_PERMUTATION_ENUM_CLASS("SURFACE_LAYER", ELumenSurfaceCacheLayer);
	class FCompress : SHADER_PERMUTATION_BOOL("COMPRESS");
	using FPermutationDomain = TShaderPermutationDomain<FSurfaceCacheLayer, FCompress>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardCopyPS, "/Engine/Private/Lumen/LumenSurfaceCache.usf", "LumenCardCopyPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardCopyParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardCopyPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FCopyTextureParameters, )
	RDG_TEXTURE_ACCESS(InputTexture, ERHIAccess::CopySrc)
	RDG_TEXTURE_ACCESS(OutputTexture, ERHIAccess::CopyDest)
END_SHADER_PARAMETER_STRUCT()

// Copy captured cards into surface cache. Possibly with compression. Has three paths:
// - Compress from capture atlas to surface cache (for platforms supporting GRHISupportsUAVFormatAliasing or when compression is disabled)
// - Compress from capture atlas into a temporary atlas and copy results into surface cache
// - Straight copy into uncompressed atlas
void FDeferredShadingSceneRenderer::UpdateLumenSurfaceCacheAtlas(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const TArray<FCardPageRenderData, SceneRenderingAllocator>& CardPagesToRender,
	FRDGBufferSRVRef CardCaptureRectBufferSRV,
	const FCardCaptureAtlas& CardCaptureAtlas)
{
	LLM_SCOPE_BYTAG(Lumen);
	RDG_EVENT_SCOPE(GraphBuilder, "CopyCardsToSurfaceCache");

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	FRDGTextureRef DepthAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.DepthAtlas);
	FRDGTextureRef AlbedoAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.AlbedoAtlas);
	FRDGTextureRef OpacityAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.OpacityAtlas);
	FRDGTextureRef NormalAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.NormalAtlas);
	FRDGTextureRef EmissiveAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.EmissiveAtlas);

	// Create rect buffer
	FRDGBufferRef SurfaceCacheRectBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateUploadDesc(sizeof(FUintVector4), FMath::RoundUpToPowerOfTwo(CardPagesToRender.Num())), TEXT("Lumen.SurfaceCacheRects"));
	FRDGBufferSRVRef SurfaceCacheRectBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(SurfaceCacheRectBuffer, PF_R32G32B32A32_UINT));
	{
		TArray<FUintVector4, SceneRenderingAllocator> SurfaceCacheRectArray;
		SurfaceCacheRectArray.Reserve(CardPagesToRender.Num());
		for (const FCardPageRenderData& CardPageRenderData : CardPagesToRender)
		{
			FUintVector4 Rect;
			Rect.X = FMath::Max(CardPageRenderData.SurfaceCacheAtlasRect.Min.X, 0);
			Rect.Y = FMath::Max(CardPageRenderData.SurfaceCacheAtlasRect.Min.Y, 0);
			Rect.Z = FMath::Max(CardPageRenderData.SurfaceCacheAtlasRect.Max.X, 0);
			Rect.W = FMath::Max(CardPageRenderData.SurfaceCacheAtlasRect.Max.Y, 0);
			SurfaceCacheRectArray.Add(Rect);
		}

		FPixelShaderUtils::UploadRectBuffer(GraphBuilder, SurfaceCacheRectArray, SurfaceCacheRectBuffer);
	}

	const FIntPoint PhysicalAtlasSize = LumenSceneData.GetPhysicalAtlasSize();
	const ESurfaceCacheCompression PhysicalAtlasCompression = LumenSceneData.GetPhysicalAtlasCompression();
	const FIntPoint CardCaptureAtlasSize = LumenSceneData.GetCardCaptureAtlasSize();

	struct FPassConfig
	{
		FRDGTextureRef CardCaptureAtlas = nullptr;
		FRDGTextureRef SurfaceCacheAtlas = nullptr;
		FRDGTextureRef TempAtlas = nullptr;
		ELumenSurfaceCacheLayer Layer = ELumenSurfaceCacheLayer::MAX;
	};

	FPassConfig PassConfigs[(uint32)ELumenSurfaceCacheLayer::MAX] =
	{
		{ CardCaptureAtlas.DepthStencil,	DepthAtlas,		nullptr, ELumenSurfaceCacheLayer::Depth },
		{ CardCaptureAtlas.Albedo,			AlbedoAtlas,	nullptr, ELumenSurfaceCacheLayer::Albedo },
		{ CardCaptureAtlas.Albedo,			OpacityAtlas,	nullptr, ELumenSurfaceCacheLayer::Opacity },
		{ CardCaptureAtlas.Normal,			NormalAtlas,	nullptr, ELumenSurfaceCacheLayer::Normal },
		{ CardCaptureAtlas.Emissive,		EmissiveAtlas,	nullptr, ELumenSurfaceCacheLayer::Emissive },
	};

	if (PhysicalAtlasCompression == ESurfaceCacheCompression::UAVAliasing)
	{
		// Compress to surface cache directly
		for (FPassConfig& Pass : PassConfigs)
		{
			const FLumenSurfaceLayerConfig& LayerConfig = GetSurfaceLayerConfig(Pass.Layer);
			const FIntPoint CompressedCardCaptureAtlasSize = FIntPoint::DivideAndRoundUp(CardCaptureAtlasSize, 4);
			const FIntPoint CompressedPhysicalAtlasSize = FIntPoint::DivideAndRoundUp(PhysicalAtlasSize, 4);
			const FRDGTextureUAVDesc CompressedSurfaceUAVDesc(Pass.SurfaceCacheAtlas, 0, LayerConfig.CompressedUAVFormat);

			FLumenCardCopyParameters* PassParameters = GraphBuilder.AllocParameters<FLumenCardCopyParameters>();
			PassParameters->PS.View = View.ViewUniformBuffer;
			PassParameters->PS.RWAtlasBlock4 = LayerConfig.CompressedUAVFormat == PF_R32G32B32A32_UINT ? GraphBuilder.CreateUAV(CompressedSurfaceUAVDesc) : nullptr;
			PassParameters->PS.RWAtlasBlock2 = LayerConfig.CompressedUAVFormat == PF_R32G32_UINT ? GraphBuilder.CreateUAV(CompressedSurfaceUAVDesc) : nullptr;
			PassParameters->PS.SourceAtlas = Pass.CardCaptureAtlas;
			PassParameters->PS.OneOverSourceAtlasSize = FVector2D(1.0f, 1.0f) / FVector2D(CardCaptureAtlasSize);

			FLumenCardCopyPS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenCardCopyPS::FSurfaceCacheLayer>(Pass.Layer);
			PermutationVector.Set<FLumenCardCopyPS::FCompress>(true);
			auto PixelShader = View.ShaderMap->GetShader<FLumenCardCopyPS>(PermutationVector);

			FPixelShaderUtils::AddRasterizeToRectsPass<FLumenCardCopyPS>(GraphBuilder,
				View.ShaderMap,
				RDG_EVENT_NAME("CompressToSurfaceCache %s", LayerConfig.Name),
				PixelShader,
				PassParameters,
				CompressedPhysicalAtlasSize,
				SurfaceCacheRectBufferSRV,
				CardPagesToRender.Num(),
				/*BlendState*/ nullptr,
				/*RasterizerState*/ nullptr,
				/*DepthStencilState*/ nullptr,
				/*StencilRef*/ 0,
				/*TextureSize*/ CompressedCardCaptureAtlasSize,
				/*RectUVBufferSRV*/ CardCaptureRectBufferSRV,
				/*DownsampleFactor*/ 4);
		}
	}
	else if (PhysicalAtlasCompression == ESurfaceCacheCompression::CopyTextureRegion)
	{
		// Compress through a temp surface
		const FIntPoint TempAtlasSize = FIntPoint::DivideAndRoundUp(CardCaptureAtlasSize, 4);

		// TempAtlas is required on platforms without UAV aliasing (GRHISupportsUAVFormatAliasing), where we can't directly compress into the final surface cache
		for (FPassConfig& Pass : PassConfigs)
		{
			const FLumenSurfaceLayerConfig& LayerConfig = GetSurfaceLayerConfig(Pass.Layer);

			Pass.TempAtlas = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(
					TempAtlasSize,
					LayerConfig.CompressedUAVFormat,
					FClearValueBinding::None,
					TexCreate_UAV | TexCreate_ShaderResource | TexCreate_NoFastClear),
				TEXT("Lumen.TempCaptureAtlas"));
		}

		// Compress into temporary atlas
		for (FPassConfig& Pass : PassConfigs)
		{
			const FLumenSurfaceLayerConfig& LayerConfig = GetSurfaceLayerConfig(Pass.Layer);

			FLumenCardCopyParameters* PassParameters = GraphBuilder.AllocParameters<FLumenCardCopyParameters>();

			PassParameters->PS.View = View.ViewUniformBuffer;
			PassParameters->PS.RWAtlasBlock4 = LayerConfig.CompressedUAVFormat == PF_R32G32B32A32_UINT ? GraphBuilder.CreateUAV(Pass.TempAtlas) : nullptr;
			PassParameters->PS.RWAtlasBlock2 = LayerConfig.CompressedUAVFormat == PF_R32G32_UINT ? GraphBuilder.CreateUAV(Pass.TempAtlas) : nullptr;
			PassParameters->PS.SourceAtlas = Pass.CardCaptureAtlas;
			PassParameters->PS.OneOverSourceAtlasSize = FVector2D(1.0f, 1.0f) / FVector2D(CardCaptureAtlasSize);

			FLumenCardCopyPS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenCardCopyPS::FSurfaceCacheLayer>(Pass.Layer);
			PermutationVector.Set<FLumenCardCopyPS::FCompress>(true);
			auto PixelShader = View.ShaderMap->GetShader<FLumenCardCopyPS>(PermutationVector);

			FPixelShaderUtils::AddRasterizeToRectsPass<FLumenCardCopyPS>(GraphBuilder,
				View.ShaderMap,
				RDG_EVENT_NAME("CompressToTemp %s", LayerConfig.Name),
				PixelShader,
				PassParameters,
				TempAtlasSize,
				CardCaptureRectBufferSRV,
				CardPagesToRender.Num(),
				/*BlendState*/ nullptr,
				/*RasterizerState*/ nullptr,
				/*DepthStencilState*/ nullptr,
				/*StencilRef*/ 0,
				/*TextureSize*/ TempAtlasSize,
				/*RectUVBufferSRV*/ nullptr,
				/*DownsampleFactor*/ 4);
		}

		// Copy from temporary atlas to surface cache
		for (FPassConfig& Pass : PassConfigs)
		{
			const FLumenSurfaceLayerConfig& LayerConfig = GetSurfaceLayerConfig(Pass.Layer);

			FCopyTextureParameters* Parameters = GraphBuilder.AllocParameters<FCopyTextureParameters>();
			Parameters->InputTexture = Pass.TempAtlas;
			Parameters->OutputTexture = Pass.SurfaceCacheAtlas;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("CopyTempToSurfaceCache %s", LayerConfig.Name),
				Parameters,
				ERDGPassFlags::Copy,
				[&CardPagesToRender, InputTexture = Pass.TempAtlas, OutputTexture = Pass.SurfaceCacheAtlas](FRHICommandList& RHICmdList)
				{
					for (int32 PageIndex = 0; PageIndex < CardPagesToRender.Num(); ++PageIndex)
					{
						const FCardPageRenderData& Page = CardPagesToRender[PageIndex];

						FRHICopyTextureInfo CopyInfo;
						CopyInfo.Size.X = Page.CardCaptureAtlasRect.Width() / 4;
						CopyInfo.Size.Y = Page.CardCaptureAtlasRect.Height() / 4;
						CopyInfo.Size.Z = 1;
						CopyInfo.SourcePosition.X = Page.CardCaptureAtlasRect.Min.X / 4;
						CopyInfo.SourcePosition.Y = Page.CardCaptureAtlasRect.Min.Y / 4;
						CopyInfo.SourcePosition.Z = 0;
						CopyInfo.DestPosition.X = Page.SurfaceCacheAtlasRect.Min.X;
						CopyInfo.DestPosition.Y = Page.SurfaceCacheAtlasRect.Min.Y;
						CopyInfo.DestPosition.Z = 0;

						RHICmdList.CopyTexture(InputTexture->GetRHI(), OutputTexture->GetRHI(), CopyInfo);
					}
				});
		}
	}
	else
	{
		// Copy uncompressed to surface cache
		for (FPassConfig& Pass : PassConfigs)
		{
			const FLumenSurfaceLayerConfig& LayerConfig = GetSurfaceLayerConfig(Pass.Layer);

			FLumenCardCopyParameters* PassParameters = GraphBuilder.AllocParameters<FLumenCardCopyParameters>();

			PassParameters->RenderTargets[0] = FRenderTargetBinding(Pass.SurfaceCacheAtlas, ERenderTargetLoadAction::ENoAction, 0);
			PassParameters->PS.View = View.ViewUniformBuffer;
			PassParameters->PS.SourceAtlas = Pass.CardCaptureAtlas;
			PassParameters->PS.OneOverSourceAtlasSize = FVector2D(1.0f, 1.0f) / FVector2D(CardCaptureAtlasSize);

			FLumenCardCopyPS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenCardCopyPS::FSurfaceCacheLayer>(Pass.Layer);
			PermutationVector.Set<FLumenCardCopyPS::FCompress>(false);
			auto PixelShader = View.ShaderMap->GetShader<FLumenCardCopyPS>(PermutationVector);

			FPixelShaderUtils::AddRasterizeToRectsPass<FLumenCardCopyPS>(GraphBuilder,
				View.ShaderMap,
				RDG_EVENT_NAME("CopyToSurfaceCache %s", LayerConfig.Name),
				PixelShader,
				PassParameters,
				PhysicalAtlasSize,
				SurfaceCacheRectBufferSRV,
				CardPagesToRender.Num(),
				/*BlendState*/ nullptr,
				/*RasterizerState*/ nullptr,
				/*DepthStencilState*/ nullptr,
				/*StencilRef*/ 0,
				/*TextureSize*/ CardCaptureAtlasSize,
				/*RectUVBufferSRV*/ CardCaptureRectBufferSRV);
		}
	}

	LumenSceneData.DepthAtlas = GraphBuilder.ConvertToExternalTexture(DepthAtlas);
	LumenSceneData.AlbedoAtlas = GraphBuilder.ConvertToExternalTexture(AlbedoAtlas);
	LumenSceneData.OpacityAtlas = GraphBuilder.ConvertToExternalTexture(OpacityAtlas);
	LumenSceneData.NormalAtlas = GraphBuilder.ConvertToExternalTexture(NormalAtlas);
	LumenSceneData.EmissiveAtlas = GraphBuilder.ConvertToExternalTexture(EmissiveAtlas);
}

class FClearCompressedAtlasCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearCompressedAtlasCS)
	SHADER_USE_PARAMETER_STRUCT(FClearCompressedAtlasCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint4>, RWAtlasBlock4)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint2>, RWAtlasBlock2)
		SHADER_PARAMETER(FVector, ClearValue)
		SHADER_PARAMETER(FIntPoint, OutputAtlasSize)
	END_SHADER_PARAMETER_STRUCT()

	class FSurfaceCacheLayer : SHADER_PERMUTATION_ENUM_CLASS("SURFACE_LAYER", ELumenSurfaceCacheLayer);

	using FPermutationDomain = TShaderPermutationDomain<FSurfaceCacheLayer>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static int32 GetGroupSize()
	{
		return 8;
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearCompressedAtlasCS, "/Engine/Private/Lumen/LumenSurfaceCache.usf", "ClearCompressedAtlasCS", SF_Compute);

void ClearAtlas(FRDGBuilder& GraphBuilder, TRefCountPtr<IPooledRenderTarget>& Atlas)
{
	FRDGTextureRef AtlasTexture = GraphBuilder.RegisterExternalTexture(Atlas);
	AddClearRenderTargetPass(GraphBuilder, AtlasTexture);
	Atlas = GraphBuilder.ConvertToExternalTexture(AtlasTexture);
}

// Clear entire Lumen surface cache to debug default values
// Surface cache can be compressed
void FDeferredShadingSceneRenderer::ClearLumenSurfaceCacheAtlas(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View)
{
	RDG_EVENT_SCOPE(GraphBuilder, "ClearLumenSurfaceCache");

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	FRDGTextureRef DepthAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.DepthAtlas);
	FRDGTextureRef AlbedoAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.AlbedoAtlas);
	FRDGTextureRef OpacityAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.OpacityAtlas);
	FRDGTextureRef NormalAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.NormalAtlas);
	FRDGTextureRef EmissiveAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.EmissiveAtlas);

	struct FPassConfig
	{
		FRDGTextureRef SurfaceCacheAtlas = nullptr;
		FRDGTextureRef TempAtlas = nullptr;
		ELumenSurfaceCacheLayer Layer = ELumenSurfaceCacheLayer::MAX;
	};

	FPassConfig PassConfigs[(uint32)ELumenSurfaceCacheLayer::MAX] =
	{
		{ DepthAtlas,		nullptr, ELumenSurfaceCacheLayer::Depth },
		{ AlbedoAtlas,		nullptr, ELumenSurfaceCacheLayer::Albedo },
		{ OpacityAtlas,		nullptr, ELumenSurfaceCacheLayer::Opacity },
		{ NormalAtlas,		nullptr, ELumenSurfaceCacheLayer::Normal },
		{ EmissiveAtlas,	nullptr, ELumenSurfaceCacheLayer::Emissive },
	};

	const FIntPoint PhysicalAtlasSize = LumenSceneData.GetPhysicalAtlasSize();
	const ESurfaceCacheCompression PhysicalAtlasCompression = LumenSceneData.GetPhysicalAtlasCompression();

	if (PhysicalAtlasCompression == ESurfaceCacheCompression::UAVAliasing)
	{
		// Clear compressed surface cache directly
		for (FPassConfig& Pass : PassConfigs)
		{
			const FLumenSurfaceLayerConfig& LayerConfig = GetSurfaceLayerConfig(Pass.Layer);

			const FRDGTextureUAVDesc CompressedSurfaceUAVDesc(Pass.SurfaceCacheAtlas, 0, LayerConfig.CompressedUAVFormat);

			FClearCompressedAtlasCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearCompressedAtlasCS::FParameters>();
			PassParameters->RWAtlasBlock4 = LayerConfig.CompressedUAVFormat == PF_R32G32B32A32_UINT ? GraphBuilder.CreateUAV(CompressedSurfaceUAVDesc) : nullptr;
			PassParameters->RWAtlasBlock2 = LayerConfig.CompressedUAVFormat == PF_R32G32_UINT ? GraphBuilder.CreateUAV(CompressedSurfaceUAVDesc) : nullptr;
			PassParameters->ClearValue = LayerConfig.ClearValue;
			PassParameters->OutputAtlasSize = PhysicalAtlasSize;

			FClearCompressedAtlasCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FClearCompressedAtlasCS::FSurfaceCacheLayer>(Pass.Layer);
			auto ComputeShader = View.ShaderMap->GetShader<FClearCompressedAtlasCS>(PermutationVector);

			FIntPoint GroupSize(FIntPoint::DivideAndRoundUp(PhysicalAtlasSize, FClearCompressedAtlasCS::GetGroupSize()));

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ClearCompressedAtlas %s", LayerConfig.Name),
				ComputeShader,
				PassParameters,
				FIntVector(GroupSize.X, GroupSize.Y, 1));
		}
	}
	else if (PhysicalAtlasCompression == ESurfaceCacheCompression::CopyTextureRegion)
	{
		// Temporary atlas is required on platforms without UAV aliasing (GRHISupportsUAVFormatAliasing), where we can't directly compress into the final surface cache

		const FIntPoint TempAtlasSize = FIntPoint::DivideAndRoundUp(LumenSceneData.GetCardCaptureAtlasSize(), 4);

		for (FPassConfig& Pass : PassConfigs)
		{
			const FLumenSurfaceLayerConfig& LayerConfig = GetSurfaceLayerConfig(Pass.Layer);

			const EPixelFormat TempFormat = LayerConfig.CompressedUAVFormat;

			Pass.TempAtlas = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(
					TempAtlasSize,
					LayerConfig.CompressedUAVFormat,
					FClearValueBinding::None,
					TexCreate_UAV | TexCreate_ShaderResource | TexCreate_NoFastClear),
				TEXT("Lumen.TempCaptureAtlas"));
		}

		// Clear temporary atlas
		for (FPassConfig& Pass : PassConfigs)
		{
			const FLumenSurfaceLayerConfig& LayerConfig = GetSurfaceLayerConfig(Pass.Layer);

			FClearCompressedAtlasCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearCompressedAtlasCS::FParameters>();
			PassParameters->RWAtlasBlock4 = LayerConfig.CompressedUAVFormat == PF_R32G32B32A32_UINT ? GraphBuilder.CreateUAV(Pass.TempAtlas) : nullptr;
			PassParameters->RWAtlasBlock2 = LayerConfig.CompressedUAVFormat == PF_R32G32_UINT ? GraphBuilder.CreateUAV(Pass.TempAtlas) : nullptr;
			PassParameters->ClearValue = LayerConfig.ClearValue;
			PassParameters->OutputAtlasSize = TempAtlasSize;

			FClearCompressedAtlasCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FClearCompressedAtlasCS::FSurfaceCacheLayer>(Pass.Layer);
			auto ComputeShader = View.ShaderMap->GetShader<FClearCompressedAtlasCS>(PermutationVector);

			FIntPoint GroupSize(FIntPoint::DivideAndRoundUp(TempAtlasSize, FClearCompressedAtlasCS::GetGroupSize()));

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ClearCompressedAtlas %s", LayerConfig.Name),
				ComputeShader,
				PassParameters,
				FIntVector(GroupSize.X, GroupSize.Y, 1));
		}

		// Copy from temporary atlas into surface cache
		for (FPassConfig& Pass : PassConfigs)
		{
			const FLumenSurfaceLayerConfig& LayerConfig = GetSurfaceLayerConfig(Pass.Layer);

			FCopyTextureParameters* Parameters = GraphBuilder.AllocParameters<FCopyTextureParameters>();
			Parameters->InputTexture = Pass.TempAtlas;
			Parameters->OutputTexture = Pass.SurfaceCacheAtlas;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("CopyToSurfaceCache %s", LayerConfig.Name),
				Parameters,
				ERDGPassFlags::Copy,
				[InputTexture = Pass.TempAtlas, PhysicalAtlasSize, TempAtlasSize, OutputTexture = Pass.SurfaceCacheAtlas](FRHICommandList& RHICmdList)
				{
					const int32 NumTilesX = FMath::DivideAndRoundDown(PhysicalAtlasSize.X / 4, TempAtlasSize.X);
					const int32 NumTilesY = FMath::DivideAndRoundDown(PhysicalAtlasSize.Y / 4, TempAtlasSize.Y);

					for (int32 TileY = 0; TileY < NumTilesY; ++TileY)
					{
						for (int32 TileX = 0; TileX < NumTilesX; ++TileX)
						{
							FRHICopyTextureInfo CopyInfo;
							CopyInfo.Size.X = TempAtlasSize.X;
							CopyInfo.Size.Y = TempAtlasSize.Y;
							CopyInfo.Size.Z = 1;
							CopyInfo.SourcePosition.X = 0;
							CopyInfo.SourcePosition.Y = 0;
							CopyInfo.SourcePosition.Z = 0;
							CopyInfo.DestPosition.X = TileX * TempAtlasSize.X * 4;
							CopyInfo.DestPosition.Y = TileY * TempAtlasSize.Y * 4;
							CopyInfo.DestPosition.Z = 0;

							RHICmdList.CopyTexture(InputTexture->GetRHI(), OutputTexture->GetRHI(), CopyInfo);
						}
					}
				});
		}
	}
	else
	{
		// Simple clear of an uncompressed surface cache
		for (FPassConfig& Pass : PassConfigs)
		{
			const FLumenSurfaceLayerConfig& LayerConfig = GetSurfaceLayerConfig(Pass.Layer);

			AddClearRenderTargetPass(GraphBuilder, Pass.SurfaceCacheAtlas, LayerConfig.ClearValue);
		}
	}

	LumenSceneData.DepthAtlas = GraphBuilder.ConvertToExternalTexture(DepthAtlas);
	LumenSceneData.AlbedoAtlas = GraphBuilder.ConvertToExternalTexture(AlbedoAtlas);
	LumenSceneData.OpacityAtlas = GraphBuilder.ConvertToExternalTexture(OpacityAtlas);
	LumenSceneData.NormalAtlas = GraphBuilder.ConvertToExternalTexture(NormalAtlas);
	LumenSceneData.EmissiveAtlas = GraphBuilder.ConvertToExternalTexture(EmissiveAtlas);


	ClearAtlas(GraphBuilder, LumenSceneData.FinalLightingAtlas);
	ClearAtlas(GraphBuilder, LumenSceneData.RadiosityAtlas);

	if (Lumen::UseIrradianceAtlas(View))
	{
		ClearAtlas(GraphBuilder, LumenSceneData.IrradianceAtlas);
	}

	if (Lumen::UseIndirectIrradianceAtlas(View))
	{
		ClearAtlas(GraphBuilder, LumenSceneData.IndirectIrradianceAtlas);
	}
}