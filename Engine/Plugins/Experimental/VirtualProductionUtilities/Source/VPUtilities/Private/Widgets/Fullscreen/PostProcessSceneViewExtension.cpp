// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcessSceneViewExtension.h"

#include "VPUtilitiesModule.h"

#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialRenderProxy.h"
#include "MaterialDomain.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "MaterialShader.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "CommonRenderResources.h"
#include "PostProcess/DrawRectangle.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "SceneRenderTargetParameters.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"

namespace UE::VirtualProductionUtilities::Private
{
	BEGIN_SHADER_PARAMETER_STRUCT(FVPFullScreenPostProcessMaterialParameters, )
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, PostProcessOutput)
		SHADER_PARAMETER_STRUCT_ARRAY(FScreenPassTextureInput, PostProcessInput, [kPostProcessMaterialInputCountMax])
		SHADER_PARAMETER_STRUCT_ARRAY(FScreenPassTextureInput, PathTracingPostProcessInput, [kPathTracingPostProcessMaterialInputCountMax])
		SHADER_PARAMETER_SAMPLER(SamplerState, PostProcessInput_BilinearSampler)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, EyeAdaptationBuffer)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	class FPostProcessMaterialShader : public FMaterialShader
	{
	public:
		using FParameters = FVPFullScreenPostProcessMaterialParameters;
		SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FPostProcessMaterialShader, FMaterialShader);

		static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
		{
			return Parameters.MaterialParameters.MaterialDomain == MD_PostProcess && !IsMobilePlatform(Parameters.Platform);
		}

		static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL"), 1);
			OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_BEFORE_TONEMAP"), (Parameters.MaterialParameters.BlendableLocation != BL_AfterTonemapping) ? 1 : 0);
		}
	};
	
	/** Shaders to render our post process material */
	class FVPFullScreenPostProcessVS :
		public FPostProcessMaterialShader
	{
	public:
		DECLARE_SHADER_TYPE(FVPFullScreenPostProcessVS, Material);

		static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FPostProcessMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("POST_PROCESS_AR_PASSTHROUGH"), 1);
		}

		FVPFullScreenPostProcessVS() = default;
		FVPFullScreenPostProcessVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FPostProcessMaterialShader(Initializer)
		{}

		void SetParameters(FRHIBatchedShaderParameters& BatchedParameters, const FSceneView& View)
		{
			UE::Renderer::PostProcess::SetDrawRectangleParameters(BatchedParameters, this, View);
			FMaterialShader::SetViewParameters(BatchedParameters, View, View.ViewUniformBuffer);
		}
	};

	IMPLEMENT_SHADER_TYPE(,FVPFullScreenPostProcessVS, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainVS_VideoOverlay"), SF_Vertex);

	class FVPFullScreenPostProcessPS :
		public FPostProcessMaterialShader
	{
	public:
		DECLARE_SHADER_TYPE(FVPFullScreenPostProcessPS, Material);

		static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FPostProcessMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("OUTPUT_MOBILE_HDR"), IsMobileHDR() ? 1 : 0);
		}

		FVPFullScreenPostProcessPS() = default;
		FVPFullScreenPostProcessPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FPostProcessMaterialShader(Initializer)
		{}

		void SetParameters(FRHIBatchedShaderParameters& BatchedParameters, const FSceneView& View, const FMaterialRenderProxy* MaterialProxy)
		{
			const FMaterial& Material = MaterialProxy->GetMaterialWithFallback(View.GetFeatureLevel(), MaterialProxy);
			FMaterialShader::SetViewParameters(BatchedParameters, View, View.ViewUniformBuffer);
			FMaterialShader::SetParameters(BatchedParameters, MaterialProxy, Material, View);
		}
	};

	IMPLEMENT_SHADER_TYPE(,FVPFullScreenPostProcessPS, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainPS_VideoOverlay"), SF_Pixel);
	
	FPostProcessSceneViewExtension::FPostProcessSceneViewExtension(const FAutoRegister& AutoRegister, TAttribute<UMaterialInterface*> PostProcessMaterialGetter)
		: Super(AutoRegister)
		, PostProcessMaterialGetter(MoveTemp(PostProcessMaterialGetter))
	{}

	void FPostProcessSceneViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
	{}

	void FPostProcessSceneViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
	{}

	void FPostProcessSceneViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
	{}

	void FPostProcessSceneViewExtension::PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
	{
		if (VertexBufferRHI == nullptr || !VertexBufferRHI.IsValid())
		{
			// Setup vertex buffer
			TResourceArray<FFilterVertex, VERTEXBUFFER_ALIGNMENT> Vertices;
			Vertices.SetNumUninitialized(4);

			Vertices[0].Position = FVector4f(0.f, 0.f, 0.f, 1.f);
			Vertices[0].UV = FVector2f(0.f, 0.f);

			Vertices[1].Position = FVector4f(1.f, 0.f, 0.f, 1.f);
			Vertices[1].UV = FVector2f(1.f, 0.f);

			Vertices[2].Position = FVector4f(0.f, 1.f, 0.f, 1.f);
			Vertices[2].UV = FVector2f(0.f, 1.f);

			Vertices[3].Position = FVector4f(1.f, 1.f, 0.f, 1.f);
			Vertices[3].UV = FVector2f(1.f, 1.f);

			FRHIResourceCreateInfo CreateInfoVB(TEXT("FPostProcessSceneViewExtension"), &Vertices);
			VertexBufferRHI = RHICreateVertexBuffer(Vertices.GetResourceDataSize(), BUF_Static, CreateInfoVB);
		}

		if (IndexBufferRHI == nullptr || !IndexBufferRHI.IsValid())
		{
			// Setup index buffer
			const uint16 Indices[] = { 0, 1, 2, 2, 1, 3 };

			TResourceArray<uint16, INDEXBUFFER_ALIGNMENT> IndexBuffer;
			const uint32 NumIndices = UE_ARRAY_COUNT(Indices);
			IndexBuffer.AddUninitialized(NumIndices);
			FMemory::Memcpy(IndexBuffer.GetData(), Indices, NumIndices * sizeof(uint16));

			FRHIResourceCreateInfo CreateInfoIB(TEXT("FPostProcessSceneViewExtension"), &IndexBuffer);
			IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint16), IndexBuffer.GetResourceDataSize(), BUF_Static, CreateInfoIB);
		}

		PostProcessMaterial = PostProcessMaterialGetter.Get();
		const bool bIsValidMaterial = PostProcessMaterial && PostProcessMaterial->GetMaterial();
		
		const bool bHasCorrectDomain = bIsValidMaterial && PostProcessMaterial->GetMaterial()->MaterialDomain == EMaterialDomain::MD_PostProcess;
		UE_CLOG(!bHasCorrectDomain, LogVPUtilities, Error, TEXT("Material Domain must be PostProcess!"));
		
		const bool bOutputsAlpha = bIsValidMaterial && PostProcessMaterial->GetMaterial()->BlendableOutputAlpha;
		UE_CLOG(!bOutputsAlpha, LogVPUtilities, Error, TEXT("Material must have \"Output Alpha\" checked!"));
		
		const bool bCanUseMaterial = bHasCorrectDomain && bOutputsAlpha;
		if (!ensure(bCanUseMaterial))
		{
			PostProcessMaterial = nullptr;
		}
	}
	
	void FPostProcessSceneViewExtension::PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
	{}

	void FPostProcessSceneViewExtension::PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder,FSceneViewFamily& InViewFamily)
	{
		if (!PostProcessMaterial || !PostProcessMaterial->IsValidLowLevel() || !VertexBufferRHI.IsValid() || !IndexBufferRHI.IsValid())
		{
			return;
		}

		FRDGTextureRef ViewFamilyTexture = TryCreateViewFamilyTexture(GraphBuilder, InViewFamily);
		if (!ViewFamilyTexture)
		{
			return;
		}

		for (int32 ViewIndex = 0; ViewIndex < InViewFamily.Views.Num(); ++ViewIndex)
		{
			RenderMaterial_RenderThread(GraphBuilder, *InViewFamily.Views[ViewIndex], ViewFamilyTexture);
		}
	}

	bool FPostProcessSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
	{
		return ensure(PostProcessMaterialGetter.IsBound()) && PostProcessMaterialGetter.Get() != nullptr;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FVPFullScreenPostProcessPassParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
	
	void FPostProcessSceneViewExtension::RenderMaterial_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, FRDGTextureRef ViewFamilyTexture)
	{
		if (!VertexBufferRHI.IsValid() || !IndexBufferRHI.IsValid())
		{
			return;
		}

		auto* PassParameters = GraphBuilder.AllocParameters<FVPFullScreenPostProcessPassParameters>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(ViewFamilyTexture, ERenderTargetLoadAction::ELoad);
		check(InView.bIsViewInfo);
		PassParameters->SceneTextures = CreateSceneTextureUniformBuffer(GraphBuilder, InView, ESceneTextureSetupMode::None);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("VPFullScreenPostProcessOverlay"),
			PassParameters,
			ERDGPassFlags::Raster,
			[this, &InView](FRHICommandList& RHICmdList)
		{
			const auto FeatureLevel = InView.GetFeatureLevel();

			const FMaterialRenderProxy* MaterialProxy = PostProcessMaterial->GetRenderProxy();
			const FMaterial& CameraMaterial = MaterialProxy->GetMaterialWithFallback(FeatureLevel, MaterialProxy);
			const FMaterialShaderMap* const MaterialShaderMap = CameraMaterial.GetRenderingThreadShaderMap();

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			// To overlay the widget, we'll do a lerp: Final color = src color * src alpha + dest color * (1-src alpha).
			// Remember: Src = PS output = widget; Dst = current render target contents = scene color
			// Note that "Output Alpha" must be checked in the post process material!!! This makes MATERIAL_OUTPUT_OPACITY_AS_ALPHA = 1 in the PS so the widget opacity is passed as alpha value.
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();	
				
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;

			TShaderRef<FVPFullScreenPostProcessVS> VertexShader = MaterialShaderMap->GetShader<FVPFullScreenPostProcessVS>();
			TShaderRef<FVPFullScreenPostProcessPS> PixelShader = MaterialShaderMap->GetShader<FVPFullScreenPostProcessPS>();
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

			SetShaderParametersLegacyVS(RHICmdList, VertexShader, InView);
			SetShaderParametersLegacyPS(RHICmdList, PixelShader, InView, MaterialProxy);

			RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
			RHICmdList.DrawIndexedPrimitive(
				IndexBufferRHI,
				/*BaseVertexIndex=*/ 0,
				/*MinIndex=*/ 0,
				/*NumVertices=*/ 4,
				/*StartIndex=*/ 0,
				/*NumPrimitives=*/ 2,
				/*NumInstances=*/ 1
			);
		});
	}
}