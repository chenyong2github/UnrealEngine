// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GoogleARCorePassthroughCameraRenderer.h"
#include "ScreenRendering.h"
#include "RendererInterface.h"
#include "EngineModule.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "SceneUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessMaterial.h"
#include "PostProcessParameters.h"
#include "MaterialShader.h"
#include "MaterialShaderType.h"
#include "ExternalTexture.h"
#include "GoogleARCorePassthroughCameraExternalTextureGuid.h"
#include "GoogleARCoreAndroidHelper.h"
#include "CommonRenderResources.h"

FGoogleARCorePassthroughCameraRenderer::FGoogleARCorePassthroughCameraRenderer()
	: OverlayQuadUVs{ 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f }
	, bInitialized(false)
	, VideoTexture(nullptr)
	, bMaterialInitialized(false)
	, DefaultOverlayMaterial(nullptr)
	, OverrideOverlayMaterial(nullptr)
	, RenderingOverlayMaterial(nullptr)
{
}

void FGoogleARCorePassthroughCameraRenderer::SetDefaultCameraOverlayMaterial(UMaterialInterface* InDefaultCameraOverlayMaterial)
{
	DefaultOverlayMaterial = InDefaultCameraOverlayMaterial;
}

void FGoogleARCorePassthroughCameraRenderer::InitializeOverlayMaterial()
{
	if (RenderingOverlayMaterial != nullptr)
		return;

	SetDefaultCameraOverlayMaterial(GetDefault<UGoogleARCoreCameraOverlayMaterialLoader>()->DefaultCameraOverlayMaterial);
	ResetOverlayMaterialToDefault();
}

void FGoogleARCorePassthroughCameraRenderer::SetOverlayMaterialInstance(UMaterialInterface* NewMaterialInstance)
{
	if (NewMaterialInstance != nullptr)
	{
		OverrideOverlayMaterial = NewMaterialInstance;

		ENQUEUE_RENDER_COMMAND(UseOverrideOverlayMaterial)(
			[VideoOverlayRendererRHIPtr = this](FRHICommandListImmediate& RHICmdList)
	        {
				VideoOverlayRendererRHIPtr->RenderingOverlayMaterial = VideoOverlayRendererRHIPtr->OverrideOverlayMaterial;
			});
	}
}

void FGoogleARCorePassthroughCameraRenderer::ResetOverlayMaterialToDefault()
{
	ENQUEUE_RENDER_COMMAND(UseDefaultOverlayMaterial)(
		[VideoOverlayRendererRHIPtr = this](FRHICommandListImmediate& RHICmdList)
        {
            VideoOverlayRendererRHIPtr->RenderingOverlayMaterial = VideoOverlayRendererRHIPtr->DefaultOverlayMaterial;
        }
    );
}

void FGoogleARCorePassthroughCameraRenderer::InitializeRenderer_RenderThread(FTextureRHIRef ExternalTexture)
{
	if (bInitialized)
		return;

	// Initialize Index buffer;
	const uint16 Indices[] = { 0, 1, 2, 2, 1, 3};

	TResourceArray<uint16, INDEXBUFFER_ALIGNMENT> IndexBuffer;
	uint32 NumIndices = UE_ARRAY_COUNT(Indices);
	IndexBuffer.AddUninitialized(NumIndices);
	FMemory::Memcpy(IndexBuffer.GetData(), Indices, NumIndices * sizeof(uint16));

	// Create index buffer. Fill buffer with initial data upon creation
	FRHIResourceCreateInfo CreateInfo(&IndexBuffer);
	OverlayIndexBufferRHI = RHICreateIndexBuffer(sizeof(uint16), IndexBuffer.GetResourceDataSize(), BUF_Static, CreateInfo);

	VideoTexture = ExternalTexture;

	FSamplerStateInitializerRHI SamplerStateInitializer(SF_Point, AM_Clamp, AM_Clamp, AM_Clamp);
	FSamplerStateRHIRef SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);

	// VideoTexture can be NULL for Vulkan
	if (VideoTexture)
	{
		FExternalTextureRegistry::Get().RegisterExternalTexture(GoogleARCorePassthroughCameraExternalTextureGuid, VideoTexture, SamplerStateRHI);
	}

	//Make sure AR camera pass through materials are updated properly
	FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();

	bInitialized = true;
}

void FGoogleARCorePassthroughCameraRenderer::UpdateOverlayUVCoordinate_RenderThread(TArray<FVector2D>& InOverlayUVs, FSceneView& InView)
{
	check(InOverlayUVs.Num() == 4);
	
	bool bFlipCameraImageVertically = RHINeedsToSwitchVerticalAxis(InView.GetShaderPlatform()) && !IsMobileHDR();

	if (bFlipCameraImageVertically)
	{
		FVector2D Tmp = InOverlayUVs[0];
		InOverlayUVs[0] = InOverlayUVs[1];
		InOverlayUVs[1] = Tmp;
		
		Tmp = InOverlayUVs[2];
		InOverlayUVs[2] = InOverlayUVs[3];
		InOverlayUVs[3] = Tmp;
	}

	if (OverlayVertexBufferRHI.IsValid())
	{
		OverlayVertexBufferRHI.SafeRelease();
	}

	TResourceArray<FFilterVertex, VERTEXBUFFER_ALIGNMENT> Vertices;
	Vertices.SetNumUninitialized(4);

	// Unreal uses reversed z. 0 is the farthest.
	Vertices[0].Position = FVector4(0, 0, 0, 1);
	Vertices[0].UV = InOverlayUVs[0];

	Vertices[1].Position = FVector4(0, 1, 0, 1);
	Vertices[1].UV = InOverlayUVs[1];

	Vertices[2].Position = FVector4(1, 0, 0, 1);
	Vertices[2].UV = InOverlayUVs[2];

	Vertices[3].Position = FVector4(1, 1, 0, 1);
	Vertices[3].UV = InOverlayUVs[3];

	// Create vertex buffer. Fill buffer with initial data upon creation
	FRHIResourceCreateInfo CreateInfo(&Vertices);
	OverlayVertexBufferRHI = RHICreateVertexBuffer(Vertices.GetResourceDataSize(), BUF_Static, CreateInfo);
}

class FPostProcessMaterialShader : public FMaterialShader
{
public:
	using FParameters = FPostProcessMaterialParameters;
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FPostProcessMaterialShader, FMaterialShader);

	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
	{
		return Parameters.Material->GetMaterialDomain() == MD_PostProcess && IsMobilePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL"), 1);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_MOBILE"), 1);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_BEFORE_TONEMAP"), (Parameters.Material->GetBlendableLocation() != BL_AfterTonemapping) ? 1 : 0);
	}
};

// We use something similar to the PostProcessMaterial to render the color camera overlay.
class FGoogleARCoreCameraOverlayVS : public FPostProcessMaterialShader
{
public:
	DECLARE_MATERIAL_SHADER(FGoogleARCoreCameraOverlayVS);

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FPostProcessMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_AR_PASSTHROUGH"), 1);
	}

	FGoogleARCoreCameraOverlayVS() = default;
	FGoogleARCoreCameraOverlayVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FPostProcessMaterialShader(Initializer)
	{}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView View)
	{
		FRHIVertexShader* ShaderRHI = GetVertexShader();
		FMaterialShader::SetViewParameters(RHICmdList, ShaderRHI, View, View.ViewUniformBuffer);
	}
};

IMPLEMENT_MATERIAL_SHADER(FGoogleARCoreCameraOverlayVS, "/Engine/Private/PostProcessMaterialShaders.usf", "MainVS", SF_Vertex);

class FGoogleARCoreCameraOverlayPS : public FPostProcessMaterialShader
{
public:
	DECLARE_MATERIAL_SHADER(FGoogleARCoreCameraOverlayPS);

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FPostProcessMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("OUTPUT_GAMMA_SPACE"), IsMobileHDR() ? 0 : 1);
	}

	FGoogleARCoreCameraOverlayPS() = default;
	FGoogleARCoreCameraOverlayPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FPostProcessMaterialShader(Initializer)
	{}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView View, const FMaterialRenderProxy* Material)
	{
		FRHIPixelShader* ShaderRHI = GetPixelShader();
		FMaterialShader::SetParameters(RHICmdList, ShaderRHI, Material, *Material->GetMaterial(View.GetFeatureLevel()), View, View.ViewUniformBuffer, ESceneTextureSetupMode::None);
	}
};

IMPLEMENT_MATERIAL_SHADER(FGoogleARCoreCameraOverlayPS, "/Engine/Private/PostProcessMaterialShaders.usf", "MainPS", SF_Pixel);

void FGoogleARCorePassthroughCameraRenderer::RenderVideoOverlay_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView)
{
#if PLATFORM_ANDROID
	if (FAndroidMisc::ShouldUseVulkan() && IsMobileHDR() && !RHICmdList.IsInsideRenderPass())
	{
		// We must NOT call DrawIndexedPrimitive below if not in a render pass on Vulkan, it's very likely to crash!
		UE_LOG(LogTemp, Warning, TEXT("FGoogleARCorePassthroughCameraRenderer::RenderVideoOverlay_RenderThread: skipped due to not called within a render pass on Vulkan!"));
		return;
	}
	
	if (RenderingOverlayMaterial == nullptr || !RenderingOverlayMaterial->IsValidLowLevel())
	{
		return;
	}

	const auto FeatureLevel = InView.GetFeatureLevel();
	IRendererModule& RendererModule = GetRendererModule();

	if (FeatureLevel <= ERHIFeatureLevel::ES3_1)
	{
		const FMaterial* CameraMaterial = RenderingOverlayMaterial->GetRenderProxy()->GetMaterial(FeatureLevel);
		const FMaterialShaderMap* MaterialShaderMap = CameraMaterial->GetRenderingThreadShaderMap();

		FGoogleARCoreCameraOverlayPS* PixelShader = MaterialShaderMap->GetShader<FGoogleARCoreCameraOverlayPS>();
		FGoogleARCoreCameraOverlayVS* VertexShader = MaterialShaderMap->GetShader<FGoogleARCoreCameraOverlayVS>();

		FGraphicsPipelineStateInitializer GraphicsPSOInit;

		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader);
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		VertexShader->SetParameters(RHICmdList, InView);
		PixelShader->SetParameters(RHICmdList, InView, RenderingOverlayMaterial->GetRenderProxy());

		FIntPoint ViewSize = InView.UnscaledViewRect.Size();

		FDrawRectangleParameters Parameters;
		Parameters.PosScaleBias = FVector4(ViewSize.X, ViewSize.Y, 0, 0);
		Parameters.UVScaleBias = FVector4(1.0f, 1.0f, 0.0f, 0.0f);

		Parameters.InvTargetSizeAndTextureSize = FVector4(
			1.0f / ViewSize.X, 1.0f / ViewSize.Y,
			1.0f, 1.0f);

		SetUniformBufferParameterImmediate(RHICmdList, VertexShader->GetVertexShader(), VertexShader->GetUniformBufferParameter<FDrawRectangleParameters>(), Parameters);

		if (OverlayVertexBufferRHI.IsValid() && OverlayIndexBufferRHI.IsValid())
		{
			RHICmdList.SetStreamSource(0, OverlayVertexBufferRHI, 0);
			RHICmdList.DrawIndexedPrimitive(
				OverlayIndexBufferRHI,
				/*BaseVertexIndex=*/ 0,
				/*MinIndex=*/ 0,
				/*NumVertices=*/ 4,
				/*StartIndex=*/ 0,
				/*NumPrimitives=*/ 2,
				/*NumInstances=*/ 1
			);
		}
	}
#endif
}
