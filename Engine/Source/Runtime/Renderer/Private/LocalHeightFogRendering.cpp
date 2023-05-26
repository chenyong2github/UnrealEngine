// Copyright Epic Games, Inc. All Rights Reserved.

#include "LocalHeightFogRendering.h"
#include "ScenePrivate.h"
#include "RendererUtils.h"
#include "ScreenPass.h"
#include "LocalHeightFogSceneProxy.h"


// The runtime ON/OFF toggle
static TAutoConsoleVariable<int32> CVarLocalHeightFog(
	TEXT("r.LocalHeightFog"), 1,
	TEXT("LocalHeightFog components are rendered when this is not 0, otherwise ignored.\n"),
	ECVF_RenderThreadSafe);

bool ShouldRenderLocalHeightFog(const FScene* Scene, const FSceneViewFamily& Family)
{
	const FEngineShowFlags EngineShowFlags = Family.EngineShowFlags;
	if (Scene && Scene->HasAnyLocalHeightFog() && EngineShowFlags.Fog && !Family.UseDebugViewPS())
	{
		return CVarLocalHeightFog.GetValueOnRenderThread() > 0;
	}
	return false;
}

DECLARE_GPU_STAT(LocalHeightFogVolumes);


/*=============================================================================
	FScene functions
=============================================================================*/



void FScene::AddLocalHeightFog(class FLocalHeightFogSceneProxy* FogProxy)
{
	check(FogProxy);
	FScene* Scene = this;

	ENQUEUE_RENDER_COMMAND(FAddLocalHeightFogCommand)(
		[Scene, FogProxy](FRHICommandListImmediate& RHICmdList)
		{
			check(!Scene->LocalHeightFogs.Contains(FogProxy));
			Scene->LocalHeightFogs.Push(FogProxy);
		} );
}

void FScene::RemoveLocalHeightFog(class FLocalHeightFogSceneProxy* FogProxy)
{
	check(FogProxy);
	FScene* Scene = this;

	ENQUEUE_RENDER_COMMAND(FRemoveLocalHeightFogCommand)(
		[Scene, FogProxy](FRHICommandListImmediate& RHICmdList)
		{
			Scene->LocalHeightFogs.RemoveSingle(FogProxy);
		} );
}

bool FScene::HasAnyLocalHeightFog() const
{ 
	return LocalHeightFogs.Num() > 0;
}


/*=============================================================================
	Local height fog rendering functions
=============================================================================*/

class FLocalHeightFogGPUInstanceData
{
public:
	FMatrix44f Transform;
	FMatrix44f InvTransform;

	FMatrix44f InvTranformNoScale;
	FMatrix44f TransformScaleOnly;

	float Density;
	float HeightFalloff;
	float HeightOffset;
	float RadialAttenuation;

	FVector3f Albedo;
	float PhaseG;
	FVector3f Emissive;
	uint32 FogMode;
};

class FLocalHeightFogVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLocalHeightFogVS);
	SHADER_USE_PARAMETER_STRUCT(FLocalHeightFogVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FLocalHeightFogGPUInstanceData>, LocalHeightFogInstances)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLocalHeightFogVS, "/Engine/Private/LocalHeightFog.usf", "LocalHeightFogSplatVS", SF_Vertex);

class FLocalHeightFogPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLocalHeightFogPS);
	SHADER_USE_PARAMETER_STRUCT(FLocalHeightFogPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FLocalHeightFogGPUInstanceData>, LocalHeightFogInstances)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLocalHeightFogPS, "/Engine/Private/LocalHeightFog.usf", "LocalHeightFogSplatPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLocalHeightFogPassParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FLocalHeightFogVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLocalHeightFogPS::FParameters, PS)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void RenderLocalHeightFog(
	const FScene* Scene,
	TArray<FViewInfo>& Views,
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	FRDGTextureRef LightShaftOcclusionTexture)
{
	uint32 LocalHeightFogInstanceCount = Scene->LocalHeightFogs.Num();
	uint32 LocalHeightFogInstanceCountFinal = 0;
	if (LocalHeightFogInstanceCount > 0)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, LocalHeightFogVolumes);
		
		// No culling as of today
		const uint32 AllLocalHeightFogInstanceBytes = sizeof(FLocalHeightFogGPUInstanceData) * LocalHeightFogInstanceCount;
		FLocalHeightFogGPUInstanceData* LocalHeightFogGPUInstanceData = (FLocalHeightFogGPUInstanceData*) GraphBuilder.Alloc(AllLocalHeightFogInstanceBytes, 16);
		FLocalHeightFogGPUInstanceData* LocalHeightFogGPUInstanceDataIt = LocalHeightFogGPUInstanceData;
		for (FLocalHeightFogSceneProxy* LHF : Scene->LocalHeightFogs)
		{
			if (LHF->FogDensity <= 0.0f)
			{
				continue; // this volume will never be visible
			}

			FTransform TransformScaleOnly;
			TransformScaleOnly.SetScale3D(LHF->FogTransform.GetScale3D());

			LocalHeightFogGPUInstanceDataIt->Transform					= FMatrix44f(LHF->FogTransform.ToMatrixWithScale());
			LocalHeightFogGPUInstanceDataIt->InvTransform				= LocalHeightFogGPUInstanceDataIt->Transform.Inverse();
			LocalHeightFogGPUInstanceDataIt->InvTranformNoScale			= FMatrix44f(LHF->FogTransform.ToMatrixNoScale()).Inverse();
			LocalHeightFogGPUInstanceDataIt->TransformScaleOnly			= FMatrix44f(TransformScaleOnly.ToMatrixWithScale());

			LocalHeightFogGPUInstanceDataIt->Density = LHF->FogDensity;
			LocalHeightFogGPUInstanceDataIt->HeightFalloff = LHF->FogHeightFalloff * 0.01f;	// This scale is used to have artist author reasonable range.
			LocalHeightFogGPUInstanceDataIt->HeightOffset = LHF->FogHeightOffset;
			LocalHeightFogGPUInstanceDataIt->RadialAttenuation = LHF->FogRadialAttenuation;

			LocalHeightFogGPUInstanceDataIt->FogMode = LHF->FogMode;

			LocalHeightFogGPUInstanceDataIt->Albedo = FVector3f(LHF->FogAlbedo);
			LocalHeightFogGPUInstanceDataIt->PhaseG = LHF->FogPhaseG;
			LocalHeightFogGPUInstanceDataIt->Emissive = FVector3f(LHF->FogEmissive);

			LocalHeightFogGPUInstanceDataIt++;
			LocalHeightFogInstanceCountFinal++;
		}

		if (LocalHeightFogInstanceCountFinal > 0)
		{
			const uint32 AllLocalHeightFogInstanceBytesFinal = sizeof(FLocalHeightFogGPUInstanceData) * LocalHeightFogInstanceCountFinal;
			FRDGBufferRef LocalHeightFogGPUInstanceDataBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("LocalHeightFogGPUInstanceDataBuffer"),
				sizeof(FLocalHeightFogGPUInstanceData), LocalHeightFogInstanceCountFinal, LocalHeightFogGPUInstanceData, AllLocalHeightFogInstanceBytesFinal, ERDGInitialDataFlags::NoCopy);

			FRDGBufferSRVRef LocalHeightFogGPUInstanceDataBufferSRV = GraphBuilder.CreateSRV(LocalHeightFogGPUInstanceDataBuffer);

			FRDGTextureRef SceneColorTexture = SceneTextures.Color.Resolve;

			for (FViewInfo& View : Views)
			{
				FLocalHeightFogPassParameters* PassParameters = GraphBuilder.AllocParameters<FLocalHeightFogPassParameters>();

				PassParameters->VS.View = GetShaderBinding(View.ViewUniformBuffer);
				PassParameters->VS.LocalHeightFogInstances = LocalHeightFogGPUInstanceDataBufferSRV;

				PassParameters->PS.View = GetShaderBinding(View.ViewUniformBuffer);
				PassParameters->PS.LocalHeightFogInstances = LocalHeightFogGPUInstanceDataBufferSRV;

				PassParameters->SceneTextures = SceneTextures.UniformBuffer;
				PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ENoAction);

				FLocalHeightFogVS::FPermutationDomain VSPermutationVector;
				auto VertexShader = View.ShaderMap->GetShader< FLocalHeightFogVS >(VSPermutationVector);

				FLocalHeightFogPS::FPermutationDomain PsPermutationVector;
				auto PixelShader = View.ShaderMap->GetShader< FLocalHeightFogPS >(PsPermutationVector);

				const FIntRect ViewRect = View.ViewRect;

				ClearUnusedGraphResources(VertexShader, &PassParameters->VS);
				ClearUnusedGraphResources(PixelShader, &PassParameters->PS);

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("RenderLocalHeightFog %u inst.", LocalHeightFogInstanceCountFinal),
					PassParameters,
					ERDGPassFlags::Raster,
					[VertexShader, PixelShader, PassParameters, LocalHeightFogInstanceCountFinal, ViewRect](FRHICommandList& RHICmdList)
				{
						FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

					RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

					// Render back faces only since camera may intersect
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI();
					GraphicsPSOInit.PrimitiveType = PT_TriangleList;

					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

					SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
					SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

					RHICmdList.SetStreamSource(0, GetUnitCubeVertexBuffer(), 0);

					RHICmdList.DrawIndexedPrimitive(GetUnitCubeIndexBuffer()
						, 0									//BaseVertexIndex
						, 0									//FirstInstance
						, 8									//uint32 NumVertices
						, 0									//uint32 StartIndex
						, UE_ARRAY_COUNT(GCubeIndices) / 3	//uint32 NumPrimitives
						, LocalHeightFogInstanceCountFinal	//uint32 NumInstances
					);
				});
			}
		}
	}
}
