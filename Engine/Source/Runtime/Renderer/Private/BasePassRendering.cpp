// Copyright Epic Games, Inc. All Rights Reserved.

#include "BasePassRendering.h"
#include "DeferredShadingRenderer.h"
#include "DistortionRendering.h"
#include "DynamicPrimitiveDrawing.h"
#include "ScenePrivate.h"
#include "MeshPassProcessor.inl"
#include "EditorPrimitivesRendering.h"
#include "TranslucentRendering.h"
#include "SingleLayerWaterRendering.h"
#include "Rendering/SkyAtmosphereCommonData.h"
#include "SceneTextureParameters.h"
#include "CompositionLighting/CompositionLighting.h"
#include "SceneViewExtension.h"
#include "VariableRateShadingImageManager.h"
#include "OneColorShader.h"
#include "ClearQuad.h"

// Changing this causes a full shader recompile
static TAutoConsoleVariable<int32> CVarSelectiveBasePassOutputs(
	TEXT("r.SelectiveBasePassOutputs"),
	0,
	TEXT("Enables shaders to only export to relevant rendertargets.\n") \
	TEXT(" 0: Export in all rendertargets.\n") \
	TEXT(" 1: Export only into relevant rendertarget.\n"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

// Changing this causes a full shader recompile
static TAutoConsoleVariable<int32> CVarGlobalClipPlane(
	TEXT("r.AllowGlobalClipPlane"),
	0,
	TEXT("Enables mesh shaders to support a global clip plane, needed for planar reflections, which adds about 15% BasePass GPU cost on PS4."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

// Changing this causes a full shader recompile
static TAutoConsoleVariable<int32> CVarVertexFoggingForOpaque(
	TEXT("r.VertexFoggingForOpaque"),
	1,
	TEXT("Causes opaque materials to use per-vertex fogging, which costs less and integrates properly with MSAA.  Only supported with forward shading."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRHICmdFlushRenderThreadTasksBasePass(
	TEXT("r.RHICmdFlushRenderThreadTasksBasePass"),
	0,
	TEXT("Wait for completion of parallel render thread tasks at the end of the base pass. A more granular version of r.RHICmdFlushRenderThreadTasks. If either r.RHICmdFlushRenderThreadTasks or r.RHICmdFlushRenderThreadTasksBasePass is > 0 we will flush."));

static TAutoConsoleVariable<int32> CVarSupportStationarySkylight(
	TEXT("r.SupportStationarySkylight"),
	1,
	TEXT("Enables Stationary and Dynamic Skylight shader permutations."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarSupportAtmosphericFog(
	TEXT("r.SupportAtmosphericFog"),
	1,
	TEXT("Enables AtmosphericFog shader permutations."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarSupportLowQualityLightmaps(
	TEXT("r.SupportLowQualityLightmaps"),
	1,
	TEXT("Support low quality lightmap shader permutations"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarSupportAllShaderPermutations(
	TEXT("r.SupportAllShaderPermutations"),
	0,
	TEXT("Local user config override to force all shader permutation features on."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarParallelBasePass(
	TEXT("r.ParallelBasePass"),
	1,
	TEXT("Toggles parallel base pass rendering. Parallel rendering must be enabled for this to have an effect."),
	ECVF_RenderThreadSafe);

// Scene color alpha is used during scene captures and planar reflections.  1 indicates background should be shown, 0 indicates foreground is fully present.
static const float kSceneColorClearAlpha = 1.0f;

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FSharedBasePassUniformParameters, "BasePass");
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FOpaqueBasePassUniformParameters, "OpaqueBasePass", SceneTextures);
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FTranslucentBasePassUniformParameters, "TranslucentBasePass", SceneTextures);

// Typedef is necessary because the C preprocessor thinks the comma in the template parameter list is a comma in the macro parameter list.
// BasePass Vertex Shader needs to include hull and domain shaders for tessellation, these only compile for D3D11
#define IMPLEMENT_BASEPASS_VERTEXSHADER_TYPE(LightMapPolicyType,LightMapPolicyName) \
	typedef TBasePassVS< LightMapPolicyType, false > TBasePassVS##LightMapPolicyName ; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassVS##LightMapPolicyName,TEXT("/Engine/Private/BasePassVertexShader.usf"),TEXT("Main"),SF_Vertex); \
	typedef TBasePassHS< LightMapPolicyType, false > TBasePassHS##LightMapPolicyName; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassHS##LightMapPolicyName,TEXT("/Engine/Private/BasePassTessellationShaders.usf"),TEXT("MainHull"),SF_Hull); \
	typedef TBasePassDS< LightMapPolicyType > TBasePassDS##LightMapPolicyName; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassDS##LightMapPolicyName,TEXT("/Engine/Private/BasePassTessellationShaders.usf"),TEXT("MainDomain"),SF_Domain); 

#define IMPLEMENT_BASEPASS_VERTEXSHADER_ONLY_TYPE(LightMapPolicyType,LightMapPolicyName,AtmosphericFogShaderName) \
	typedef TBasePassVS<LightMapPolicyType,true> TBasePassVS##LightMapPolicyName##AtmosphericFogShaderName; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassVS##LightMapPolicyName##AtmosphericFogShaderName,TEXT("/Engine/Private/BasePassVertexShader.usf"),TEXT("Main"),SF_Vertex)	\
	typedef TBasePassHS< LightMapPolicyType, true> TBasePassHS##LightMapPolicyName##AtmosphericFogShaderName; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassHS##LightMapPolicyName##AtmosphericFogShaderName,TEXT("/Engine/Private/BasePassTessellationShaders.usf"),TEXT("MainHull"),SF_Hull);

#define IMPLEMENT_BASEPASS_PIXELSHADER_TYPE(LightMapPolicyType,LightMapPolicyName,bEnableSkyLight,SkyLightName) \
	typedef TBasePassPS<LightMapPolicyType, bEnableSkyLight> TBasePassPS##LightMapPolicyName##SkyLightName; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TBasePassPS##LightMapPolicyName##SkyLightName,TEXT("/Engine/Private/BasePassPixelShader.usf"),TEXT("MainPS"),SF_Pixel);

// Implement a pixel shader type for skylights and one without, and one vertex shader that will be shared between them
#define IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE(LightMapPolicyType,LightMapPolicyName) \
	IMPLEMENT_BASEPASS_VERTEXSHADER_TYPE(LightMapPolicyType,LightMapPolicyName) \
	IMPLEMENT_BASEPASS_VERTEXSHADER_ONLY_TYPE(LightMapPolicyType,LightMapPolicyName,AtmosphericFog) \
	IMPLEMENT_BASEPASS_PIXELSHADER_TYPE(LightMapPolicyType,LightMapPolicyName,true,Skylight) \
	IMPLEMENT_BASEPASS_PIXELSHADER_TYPE(LightMapPolicyType,LightMapPolicyName,false,);

// Implement shader types per lightmap policy
// If renaming or refactoring these, remember to update FMaterialResource::GetRepresentativeInstructionCounts and FPreviewMaterial::ShouldCache().
IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE( FSelfShadowedTranslucencyPolicy, FSelfShadowedTranslucencyPolicy );
IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE( FSelfShadowedCachedPointIndirectLightingPolicy, FSelfShadowedCachedPointIndirectLightingPolicy );
IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE( FSelfShadowedVolumetricLightmapPolicy, FSelfShadowedVolumetricLightmapPolicy );

IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE( TUniformLightMapPolicy<LMP_NO_LIGHTMAP>, FNoLightMapPolicy );
IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE( TUniformLightMapPolicy<LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING>, FPrecomputedVolumetricLightmapLightingPolicy );
IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE( TUniformLightMapPolicy<LMP_CACHED_VOLUME_INDIRECT_LIGHTING>, FCachedVolumeIndirectLightingPolicy );
IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE( TUniformLightMapPolicy<LMP_CACHED_POINT_INDIRECT_LIGHTING>, FCachedPointIndirectLightingPolicy );
IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE( TUniformLightMapPolicy<LMP_SIMPLE_NO_LIGHTMAP>, FSimpleNoLightmapLightingPolicy );
IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE( TUniformLightMapPolicy<LMP_SIMPLE_LIGHTMAP_ONLY_LIGHTING>, FSimpleLightmapOnlyLightingPolicy );
IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE( TUniformLightMapPolicy<LMP_SIMPLE_DIRECTIONAL_LIGHT_LIGHTING>, FSimpleDirectionalLightLightingPolicy );
IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE( TUniformLightMapPolicy<LMP_SIMPLE_STATIONARY_PRECOMPUTED_SHADOW_LIGHTING>, FSimpleStationaryLightPrecomputedShadowsLightingPolicy );
IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE( TUniformLightMapPolicy<LMP_SIMPLE_STATIONARY_SINGLESAMPLE_SHADOW_LIGHTING>, FSimpleStationaryLightSingleSampleShadowsLightingPolicy );
IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE( TUniformLightMapPolicy<LMP_SIMPLE_STATIONARY_VOLUMETRICLIGHTMAP_SHADOW_LIGHTING>, FSimpleStationaryLightVolumetricLightmapShadowsLightingPolicy );
IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE( TUniformLightMapPolicy<LMP_LQ_LIGHTMAP>, TLightMapPolicyLQ );
IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE( TUniformLightMapPolicy<LMP_HQ_LIGHTMAP>, TLightMapPolicyHQ );
IMPLEMENT_BASEPASS_LIGHTMAPPED_SHADER_TYPE( TUniformLightMapPolicy<LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP>, TDistanceFieldShadowsAndLightMapPolicyHQ  );

IMPLEMENT_MATERIAL_SHADER_TYPE(, F128BitRTBasePassPS, TEXT("/Engine/Private/BasePassPixelShader.usf"), TEXT("MainPS"), SF_Pixel);

DEFINE_GPU_DRAWCALL_STAT(Basepass);

DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer ClearGBufferAtMaxZ"), STAT_FDeferredShadingSceneRenderer_ClearGBufferAtMaxZ, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer ViewExtensionPostRenderBasePass"), STAT_FDeferredShadingSceneRenderer_ViewExtensionPostRenderBasePass, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("BasePass"), STAT_CLM_BasePass, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("AfterBasePass"), STAT_CLM_AfterBasePass, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("AnisotropyPass"), STAT_CLM_AnisotropyPass, STATGROUP_CommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("AfterAnisotropyPass"), STAT_CLM_AfterAnisotropyPass, STATGROUP_CommandListMarkers);

DECLARE_CYCLE_STAT(TEXT("BasePass"), STAT_CLP_BasePass, STATGROUP_ParallelCommandListMarkers);

static bool IsBasePassWaitForTasksEnabled()
{
	return CVarRHICmdFlushRenderThreadTasksBasePass.GetValueOnRenderThread() > 0 || CVarRHICmdFlushRenderThreadTasks.GetValueOnRenderThread() > 0;
}

void SetTranslucentRenderState(FMeshPassProcessorRenderState& DrawRenderState, const FMaterial& Material, const EShaderPlatform Platform, ETranslucencyPass::Type InTranslucencyPassType)
{
	if (Material.GetShadingModels().HasShadingModel(MSM_ThinTranslucent))
	{
		// Special case for dual blending, which is not exposed as a parameter in the material editor
		if (Material.IsDualBlendingEnabled(Platform))
		{
			if (InTranslucencyPassType == ETranslucencyPass::TPT_StandardTranslucency || InTranslucencyPassType == ETranslucencyPass::TPT_AllTranslucency)
			{
				// If we are in the transparancy pass (before DoF) we do standard dual blending, and the alpha gets ignored

				// Blend by putting add in target 0 and multiply by background in target 1.
				DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Source1Color, BO_Add, BF_One, BF_Source1Alpha>::GetRHI());
			}
			else if (InTranslucencyPassType == ETranslucencyPass::TPT_TranslucencyAfterDOF)
			{
				// In the separate pass (after DoF), we want let alpha pass through, and then multiply our color modulation in the after DoF Modulation pass.
				// Alpha is BF_Zero for source and BF_One for dest, which leaves alpha unchanged
				DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Source1Color, BO_Add, BF_Zero, BF_One>::GetRHI());
			}
			else if (InTranslucencyPassType == ETranslucencyPass::TPT_TranslucencyAfterDOFModulate)
			{
				// In the separate pass (after DoF) modulate, we want to only darken the target by our multiplication term, and ignore the addition term.
				// For regular dual blending, our function is:
				//     FrameBuffer = MRT0 + MRT1 * FrameBuffer;
				// So we can just remove the MRT0 component and it will modulate as expected.
				// Alpha we will leave unchanged.
				DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_Zero, BF_Source1Color, BO_Add, BF_Zero, BF_One>::GetRHI());
			}
		}
		else
		{
			// If unsupported, use the same as translucent, but with color multiplied by BF_One instead of BF_SourceAlpha.
			// The shader will use the variation that approximates color modulation using alpha
			DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI());
		}
	}
	else
	{
		switch (Material.GetBlendMode())
		{
		default:
		case BLEND_Opaque:
			// Opaque materials are rendered together in the base pass, where the blend state is set at a higher level
			break;
		case BLEND_Masked:
			// Masked materials are rendered together in the base pass, where the blend state is set at a higher level
			break;
		case BLEND_Translucent:
			// Note: alpha channel used by separate translucency, storing how much of the background should be added when doing the final composite
			// The Alpha channel is also used by non-separate translucency when rendering to scene captures, which store the final opacity
			DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI());
			break;
		case BLEND_Additive:
			// Add to the existing scene color
			// Note: alpha channel used by separate translucency, storing how much of the background should be added when doing the final composite
			// The Alpha channel is also used by non-separate translucency when rendering to scene captures, which store the final opacity
			DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI());
			break;
		case BLEND_Modulate:
			// Modulate with the existing scene color, preserve destination alpha.
			DrawRenderState.SetBlendState(TStaticBlendState<CW_RGB, BO_Add, BF_DestColor, BF_Zero>::GetRHI());
			break;
		case BLEND_AlphaComposite:
			// Blend with existing scene color. New color is already pre-multiplied by alpha.
			DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI());
			break;
		case BLEND_AlphaHoldout:
			// Blend by holding out the matte shape of the source alpha
			DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_Zero, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI());
			break;
		};
	}

	const bool bDisableDepthTest = Material.ShouldDisableDepthTest();
	const bool bEnableResponsiveAA = Material.ShouldEnableResponsiveAA();

	if (bEnableResponsiveAA)
	{
		if (bDisableDepthTest)
		{
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<
				false, CF_Always,
				true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
				false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
				STENCIL_TEMPORAL_RESPONSIVE_AA_MASK, STENCIL_TEMPORAL_RESPONSIVE_AA_MASK
			>::GetRHI());
			DrawRenderState.SetStencilRef(STENCIL_TEMPORAL_RESPONSIVE_AA_MASK);
		}
		else
		{
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<
				false, CF_DepthNearOrEqual,
				true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
				false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
				STENCIL_TEMPORAL_RESPONSIVE_AA_MASK, STENCIL_TEMPORAL_RESPONSIVE_AA_MASK
			>::GetRHI());
			DrawRenderState.SetStencilRef(STENCIL_TEMPORAL_RESPONSIVE_AA_MASK);
		}
	}
	else if (bDisableDepthTest)
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
	}
}

FMeshDrawCommandSortKey CalculateTranslucentMeshStaticSortKey(const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, uint16 MeshIdInPrimitive)
{
	uint16 SortKeyPriority = 0;
	float DistanceOffset = 0.0f;

	if (PrimitiveSceneProxy)
	{
		const FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneProxy->GetPrimitiveSceneInfo();
		SortKeyPriority = (uint16)((int32)PrimitiveSceneInfo->Proxy->GetTranslucencySortPriority() - (int32)SHRT_MIN);
		DistanceOffset = PrimitiveSceneInfo->Proxy->GetTranslucencySortDistanceOffset();
	}

	FMeshDrawCommandSortKey SortKey;
	SortKey.Translucent.MeshIdInPrimitive = MeshIdInPrimitive;
	SortKey.Translucent.Priority = SortKeyPriority;
	SortKey.Translucent.Distance = *(uint32*)(&DistanceOffset); // View specific, so will be filled later inside VisibleMeshCommands.

	return SortKey;
}

FMeshDrawCommandSortKey CalculateBasePassMeshStaticSortKey(EDepthDrawingMode EarlyZPassMode, EBlendMode BlendMode, const FMeshMaterialShader* VertexShader, const FMeshMaterialShader* PixelShader)
{
	FMeshDrawCommandSortKey SortKey;
	SortKey.BasePass.VertexShaderHash = PointerHash(VertexShader) & 0xFFFF;
	SortKey.BasePass.PixelShaderHash = PointerHash(PixelShader);
	if (EarlyZPassMode != DDM_None)
	{
		SortKey.BasePass.Masked = BlendMode == EBlendMode::BLEND_Masked ? 0 : 1;
	}
	else
	{
		SortKey.BasePass.Masked = BlendMode == EBlendMode::BLEND_Masked ? 1 : 0;
	}

	return SortKey;
}

void SetDepthStencilStateForBasePass(
	const FSceneView* ViewIfDynamicMeshCommand,
	FMeshPassProcessorRenderState& DrawRenderState,
	ERHIFeatureLevel::Type FeatureLevel,
	const FMeshBatch& Mesh,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterial& MaterialResource,
	bool bEnableReceiveDecalOutput)
{
	const bool bMaskedInEarlyPass = (MaterialResource.IsMasked() || Mesh.bDitheredLODTransition) && MaskedInEarlyPass(GShaderPlatformForFeatureLevel[FeatureLevel]);

	if (bEnableReceiveDecalOutput)
	{
		// Set stencil value for this draw call
		// This is effectively extending the GBuffer using the stencil bits
		const uint8 StencilValue = GET_STENCIL_BIT_MASK(RECEIVE_DECAL, PrimitiveSceneProxy ? !!PrimitiveSceneProxy->ReceivesDecals() : 0x00)
			| STENCIL_LIGHTING_CHANNELS_MASK(PrimitiveSceneProxy ? PrimitiveSceneProxy->GetLightingChannelStencilValue() : 0x00);

		if (bMaskedInEarlyPass)
		{
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<
				false, CF_Equal,
				true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
				false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
				0xFF, GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1) | STENCIL_LIGHTING_CHANNELS_MASK(0x7)
			>::GetRHI());
			DrawRenderState.SetStencilRef(StencilValue);
		}
		else if (DrawRenderState.GetDepthStencilAccess() & FExclusiveDepthStencil::DepthWrite)
		{
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<
				true, CF_GreaterEqual,
				true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
				false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
				0xFF, GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1) | STENCIL_LIGHTING_CHANNELS_MASK(0x7)
			>::GetRHI());
			DrawRenderState.SetStencilRef(StencilValue);
		}
		else
		{
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<
				false, CF_GreaterEqual,
				true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
				false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
				0xFF, GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1) | STENCIL_LIGHTING_CHANNELS_MASK(0x7)
			>::GetRHI());
			DrawRenderState.SetStencilRef(StencilValue);
		}
	}
	else if (bMaskedInEarlyPass)
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Equal>::GetRHI());
	}

	if (ViewIfDynamicMeshCommand && StaticMeshId >= 0 && Mesh.bDitheredLODTransition)
	{
		checkSlow(ViewIfDynamicMeshCommand->bIsViewInfo);
		const FViewInfo* ViewInfo = static_cast<const FViewInfo*>(ViewIfDynamicMeshCommand);

		if (ViewInfo->bAllowStencilDither)
		{
			if (ViewInfo->StaticMeshFadeOutDitheredLODMap[StaticMeshId] || ViewInfo->StaticMeshFadeInDitheredLODMap[StaticMeshId])
			{
				const uint32 RestoreStencilRef = DrawRenderState.GetStencilRef();
				DrawRenderState.SetDepthStencilState(
					TStaticDepthStencilState<
					false, CF_Equal,
					true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
					false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
					0xFF, GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1) | STENCIL_LIGHTING_CHANNELS_MASK(0x7)
					>::GetRHI());
				DrawRenderState.SetStencilRef(RestoreStencilRef);
			}
		}
	}
}

void SetupBasePassState(FExclusiveDepthStencil::Type BasePassDepthStencilAccess, const bool bShaderComplexity, FMeshPassProcessorRenderState& DrawRenderState)
{
	DrawRenderState.SetDepthStencilAccess(BasePassDepthStencilAccess);

	if (bShaderComplexity)
	{
		// Additive blending when shader complexity viewmode is enabled.
		DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One>::GetRHI());
		// Disable depth writes as we have a full depth prepass.
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
	}
	else
	{
		// Opaque blending for all G buffer targets, depth tests and writes.
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.BasePassOutputsVelocityDebug"));
		if (CVar && CVar->GetValueOnRenderThread() == 2)
		{
			DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RGBA, CW_RGBA, CW_RGBA, CW_RGBA, CW_RGBA, CW_RGBA, CW_NONE>::GetRHI());
		}
		else
		{
			DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RGBA, CW_RGBA, CW_RGBA, CW_RGBA>::GetRHI());
		}

		if (DrawRenderState.GetDepthStencilAccess() & FExclusiveDepthStencil::DepthWrite)
		{
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
		}
		else
		{
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
		}
	}
}

/**
 * Get shader templates allowing to redirect between compatible shaders.
 */

template <ELightMapPolicyType Policy>
bool GetUniformBasePassShaders(
	const FMaterial& Material,
	FVertexFactoryType* VertexFactoryType,
	ERHIFeatureLevel::Type FeatureLevel,
	bool bEnableAtmosphericFog,
	bool bEnableSkyLight,
	bool bUse128bitRT,
	TShaderRef<FBaseHS>& HullShader,
	TShaderRef<FBaseDS>& DomainShader,
	TShaderRef<TBasePassVertexShaderPolicyParamType<FUniformLightMapPolicy>>& VertexShader,
	TShaderRef<TBasePassPixelShaderPolicyParamType<FUniformLightMapPolicy>>& PixelShader
)
{
	const EMaterialTessellationMode MaterialTessellationMode = Material.GetTessellationMode();

	const bool bNeedsHSDS = RHISupportsTessellation(GShaderPlatformForFeatureLevel[FeatureLevel])
		&& VertexFactoryType->SupportsTessellationShaders()
		&& MaterialTessellationMode != MTM_NoTessellation;

	FMaterialShaderTypes ShaderTypes;
	if (bNeedsHSDS)
	{
		//DomainShader = Material.GetShader<TBasePassDS<TUniformLightMapPolicy<Policy> > >(VertexFactoryType, 0, false);
		ShaderTypes.AddShaderType<TBasePassDS<TUniformLightMapPolicy<Policy>>>();

		// Metal requires matching permutations, but no other platform should worry about this complication.
		if (bEnableAtmosphericFog && DomainShader.IsValid() && IsMetalPlatform(EShaderPlatform(DomainShader->GetTarget().Platform)))
		{
			ShaderTypes.AddShaderType<TBasePassHS<TUniformLightMapPolicy<Policy>, true>>();
		}
		else
		{
			ShaderTypes.AddShaderType<TBasePassHS<TUniformLightMapPolicy<Policy>, false>>();
		}
	}

	if (bEnableAtmosphericFog)
	{
		ShaderTypes.AddShaderType<TBasePassVS<TUniformLightMapPolicy<Policy>, true>>();
	}
	else
	{
		ShaderTypes.AddShaderType<TBasePassVS<TUniformLightMapPolicy<Policy>, false>>();
	}

	if (bEnableSkyLight)
	{
		ShaderTypes.AddShaderType<TBasePassPS<TUniformLightMapPolicy<Policy>, true>>();
	}
	else
	{
		if (bUse128bitRT && (Policy == LMP_NO_LIGHTMAP))
		{
			ShaderTypes.AddShaderType<F128BitRTBasePassPS>();
		}
		else
		{
			ShaderTypes.AddShaderType<TBasePassPS<TUniformLightMapPolicy<Policy>, false>>();
		}
	}

	FMaterialShaders Shaders;
	if (!Material.TryGetShaders(ShaderTypes, VertexFactoryType, Shaders))
	{
		return false;
	}

	Shaders.TryGetVertexShader(VertexShader);
	Shaders.TryGetPixelShader(PixelShader);
	Shaders.TryGetHullShader(HullShader);
	Shaders.TryGetDomainShader(DomainShader);
	return true;
}

template <>
bool GetBasePassShaders<FUniformLightMapPolicy>(
	const FMaterial& Material, 
	FVertexFactoryType* VertexFactoryType, 
	FUniformLightMapPolicy LightMapPolicy, 
	ERHIFeatureLevel::Type FeatureLevel,
	bool bEnableAtmosphericFog,
	bool bEnableSkyLight,
	bool bUse128bitRT,
	TShaderRef<FBaseHS>& HullShader,
	TShaderRef<FBaseDS>& DomainShader,
	TShaderRef<TBasePassVertexShaderPolicyParamType<FUniformLightMapPolicy>>& VertexShader,
	TShaderRef<TBasePassPixelShaderPolicyParamType<FUniformLightMapPolicy>>& PixelShader
	)
{
	switch (LightMapPolicy.GetIndirectPolicy())
	{
	case LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING:
		return GetUniformBasePassShaders<LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING>(Material, VertexFactoryType, FeatureLevel, bEnableAtmosphericFog, bEnableSkyLight, bUse128bitRT, HullShader, DomainShader, VertexShader, PixelShader);
	case LMP_CACHED_VOLUME_INDIRECT_LIGHTING:
		return GetUniformBasePassShaders<LMP_CACHED_VOLUME_INDIRECT_LIGHTING>(Material, VertexFactoryType, FeatureLevel, bEnableAtmosphericFog, bEnableSkyLight, bUse128bitRT, HullShader, DomainShader, VertexShader, PixelShader);
	case LMP_CACHED_POINT_INDIRECT_LIGHTING:
		return GetUniformBasePassShaders<LMP_CACHED_POINT_INDIRECT_LIGHTING>(Material, VertexFactoryType, FeatureLevel, bEnableAtmosphericFog, bEnableSkyLight, bUse128bitRT, HullShader, DomainShader, VertexShader, PixelShader);
	case LMP_SIMPLE_DIRECTIONAL_LIGHT_LIGHTING:
		return GetUniformBasePassShaders<LMP_SIMPLE_DIRECTIONAL_LIGHT_LIGHTING>(Material, VertexFactoryType, FeatureLevel, bEnableAtmosphericFog, bEnableSkyLight, bUse128bitRT, HullShader, DomainShader, VertexShader, PixelShader);
	case LMP_SIMPLE_NO_LIGHTMAP:
		return GetUniformBasePassShaders<LMP_SIMPLE_NO_LIGHTMAP>(Material, VertexFactoryType, FeatureLevel, bEnableAtmosphericFog, bEnableSkyLight, bUse128bitRT, HullShader, DomainShader, VertexShader, PixelShader);
	case LMP_SIMPLE_LIGHTMAP_ONLY_LIGHTING:
		return GetUniformBasePassShaders<LMP_SIMPLE_LIGHTMAP_ONLY_LIGHTING>(Material, VertexFactoryType, FeatureLevel, bEnableAtmosphericFog, bEnableSkyLight, bUse128bitRT, HullShader, DomainShader, VertexShader, PixelShader);
	case LMP_SIMPLE_STATIONARY_PRECOMPUTED_SHADOW_LIGHTING:
		return GetUniformBasePassShaders<LMP_SIMPLE_STATIONARY_PRECOMPUTED_SHADOW_LIGHTING>(Material, VertexFactoryType, FeatureLevel, bEnableAtmosphericFog, bEnableSkyLight, bUse128bitRT, HullShader, DomainShader, VertexShader, PixelShader);
	case LMP_SIMPLE_STATIONARY_SINGLESAMPLE_SHADOW_LIGHTING:
		return GetUniformBasePassShaders<LMP_SIMPLE_STATIONARY_SINGLESAMPLE_SHADOW_LIGHTING>(Material, VertexFactoryType, FeatureLevel, bEnableAtmosphericFog, bEnableSkyLight, bUse128bitRT, HullShader, DomainShader, VertexShader, PixelShader);
	case LMP_SIMPLE_STATIONARY_VOLUMETRICLIGHTMAP_SHADOW_LIGHTING:
		return GetUniformBasePassShaders<LMP_SIMPLE_STATIONARY_VOLUMETRICLIGHTMAP_SHADOW_LIGHTING>(Material, VertexFactoryType, FeatureLevel, bEnableAtmosphericFog, bEnableSkyLight, bUse128bitRT, HullShader, DomainShader, VertexShader, PixelShader);
	case LMP_LQ_LIGHTMAP:
		return GetUniformBasePassShaders<LMP_LQ_LIGHTMAP>(Material, VertexFactoryType, FeatureLevel, bEnableAtmosphericFog, bEnableSkyLight, bUse128bitRT, HullShader, DomainShader, VertexShader, PixelShader);
	case LMP_HQ_LIGHTMAP:
		return GetUniformBasePassShaders<LMP_HQ_LIGHTMAP>(Material, VertexFactoryType, FeatureLevel, bEnableAtmosphericFog, bEnableSkyLight, bUse128bitRT, HullShader, DomainShader, VertexShader, PixelShader);
	case LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP:
		return GetUniformBasePassShaders<LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP>(Material, VertexFactoryType, FeatureLevel, bEnableAtmosphericFog, bEnableSkyLight, bUse128bitRT, HullShader, DomainShader, VertexShader, PixelShader);
	case LMP_NO_LIGHTMAP:
		return GetUniformBasePassShaders<LMP_NO_LIGHTMAP>(Material, VertexFactoryType, FeatureLevel, bEnableAtmosphericFog, bEnableSkyLight, bUse128bitRT, HullShader, DomainShader, VertexShader, PixelShader);
	default:
		check(false);
		return false;
	}
}

extern void SetupFogUniformParameters(FRDGBuilder* GraphBuilder, const FViewInfo& View, FFogUniformParameters& OutParameters);

void SetupSharedBasePassParameters(
	FRDGBuilder* GraphBuilder,
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	FSharedBasePassUniformParameters& SharedParameters)
{
	SharedParameters.Forward = View.ForwardLightingResources->ForwardLightData;

	SetupFogUniformParameters(GraphBuilder, View, SharedParameters.Fog);

	if (View.IsInstancedStereoPass())
	{
		const FSceneView& RightEye = *View.Family->Views[1];
		SharedParameters.ForwardISR = RightEye.ForwardLightingResources->ForwardLightData;
		SetupFogUniformParameters(GraphBuilder, (FViewInfo&)RightEye, SharedParameters.FogISR);
	}
	else
	{
		SharedParameters.ForwardISR = View.ForwardLightingResources->ForwardLightData;
		SharedParameters.FogISR = SharedParameters.Fog;
	}

	const FScene* Scene = View.Family->Scene ? View.Family->Scene->GetRenderScene() : nullptr;
	const FPlanarReflectionSceneProxy* ReflectionSceneProxy = Scene ? Scene->GetForwardPassGlobalPlanarReflection() : nullptr;

	SetupReflectionUniformParameters(View, SharedParameters.Reflection);
	SetupPlanarReflectionUniformParameters(View, ReflectionSceneProxy, SharedParameters.PlanarReflection);

	SharedParameters.SSProfilesTexture = GBlackTexture->TextureRHI;

	if (const IPooledRenderTarget* PooledRT = GetSubsufaceProfileTexture_RT(RHICmdList))
	{
		SharedParameters.SSProfilesTexture = PooledRT->GetShaderResourceRHI();
	}
}

void SetupSharedOpaqueBasePassParameters(
	FRDGBuilder* GraphBuilder,
	FRHICommandListImmediate& RHICmdList,
	const FSceneRenderTargets& SceneRenderTargets,
	const FViewInfo& View,
	FRDGTextureRef ForwardScreenSpaceShadowMask,
	const FSceneWithoutWaterTextures* SceneWithoutWaterTextures,
	const int32 ViewIndex,
	FOpaqueBasePassUniformParameters& BasePassParameters)
{
	const auto GetRDG = [&](const TRefCountPtr<IPooledRenderTarget>& PooledRenderTarget, ERDGTextureFlags Flags = ERDGTextureFlags::None)
	{
		return RegisterExternalOrPassthroughTexture(GraphBuilder, PooledRenderTarget, Flags);
	};

	SetupSharedBasePassParameters(GraphBuilder, RHICmdList, View, BasePassParameters.Shared);

	FRDGTextureRef WhiteDummy = GetRDG(GSystemTextures.WhiteDummy);
	FRDGTextureRef BlackDummy = GetRDG(GSystemTextures.BlackDummy);

	// Forward shading
	{
		if (ForwardScreenSpaceShadowMask)
		{
			BasePassParameters.UseForwardScreenSpaceShadowMask = 1;
			BasePassParameters.ForwardScreenSpaceShadowMaskTexture = ForwardScreenSpaceShadowMask;
		}
		else
		{
			BasePassParameters.UseForwardScreenSpaceShadowMask = 0;
			BasePassParameters.ForwardScreenSpaceShadowMaskTexture = WhiteDummy;
		}

		IPooledRenderTarget* IndirectOcclusion = SceneRenderTargets.ScreenSpaceAO;

		if (!SceneRenderTargets.bScreenSpaceAOIsValid)
		{
			IndirectOcclusion = GSystemTextures.WhiteDummy;
		}

		BasePassParameters.IndirectOcclusionTexture = GetRDG(IndirectOcclusion);

		FRDGTextureRef ResolvedSceneDepthTextureValue = WhiteDummy;

		if (SceneRenderTargets.GetMSAACount() > 1)
		{
			ResolvedSceneDepthTextureValue = GetRDG(SceneRenderTargets.SceneDepthZ);
		}

		BasePassParameters.ResolvedSceneDepthTexture = ResolvedSceneDepthTextureValue;
	}

	// DBuffer Decals
	{
		const bool bIsDBufferEnabled = IsUsingDBuffers(View.GetShaderPlatform());
		IPooledRenderTarget* DBufferA = bIsDBufferEnabled && SceneRenderTargets.DBufferA ? SceneRenderTargets.DBufferA : GSystemTextures.BlackAlphaOneDummy;
		IPooledRenderTarget* DBufferB = bIsDBufferEnabled && SceneRenderTargets.DBufferB ? SceneRenderTargets.DBufferB : GSystemTextures.DefaultNormal8Bit;
		IPooledRenderTarget* DBufferC = bIsDBufferEnabled && SceneRenderTargets.DBufferC ? SceneRenderTargets.DBufferC : GSystemTextures.BlackAlphaOneDummy;

		ERDGTextureFlags Flags = ERDGTextureFlags::None;
		if ((RHISupportsRenderTargetWriteMask(GMaxRHIShaderPlatform) || FDataDrivenShaderPlatformInfo::GetSupportsPerPixelDBufferMask(View.GetShaderPlatform())) && SceneRenderTargets.DBufferMask)
		{
			BasePassParameters.DBufferRenderMask = GetRDG(SceneRenderTargets.DBufferMask);
			Flags = ERDGTextureFlags::MaintainCompression;
		}
		else
		{
			BasePassParameters.DBufferRenderMask = WhiteDummy;
		}

		BasePassParameters.DBufferATexture = GetRDG(DBufferA, Flags);
		BasePassParameters.DBufferBTexture = GetRDG(DBufferB, Flags);
		BasePassParameters.DBufferCTexture = GetRDG(DBufferC, Flags);
		BasePassParameters.DBufferATextureSampler = TStaticSamplerState<>::GetRHI();
		BasePassParameters.DBufferBTextureSampler = TStaticSamplerState<>::GetRHI();
		BasePassParameters.DBufferCTextureSampler = TStaticSamplerState<>::GetRHI();
	}

	// Single Layer Water
	BasePassParameters.SceneWithoutSingleLayerWaterMinMaxUV = FVector4(0.0f, 0.0f, 1.0f, 1.0f);
	BasePassParameters.SceneColorWithoutSingleLayerWaterTexture = BlackDummy;
	BasePassParameters.SceneDepthWithoutSingleLayerWaterTexture = BlackDummy;
	BasePassParameters.SceneColorWithoutSingleLayerWaterSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	BasePassParameters.SceneDepthWithoutSingleLayerWaterSampler = TStaticSamplerState<SF_Point>::GetRHI();

	if (SceneWithoutWaterTextures)
	{
		BasePassParameters.SceneWithoutSingleLayerWaterMinMaxUV = SceneWithoutWaterTextures->Views[ViewIndex].MinMaxUV;
		BasePassParameters.SceneColorWithoutSingleLayerWaterTexture = SceneWithoutWaterTextures->ColorTexture;
		BasePassParameters.SceneDepthWithoutSingleLayerWaterTexture = SceneWithoutWaterTextures->DepthTexture;
	}

	BasePassParameters.PreIntegratedGFTexture = GSystemTextures.PreintegratedGF->GetShaderResourceRHI();
	BasePassParameters.PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	SetupDistortionParams(BasePassParameters.DistortionParams, View);

	// Misc
	if (View.HasValidEyeAdaptationTexture())
	{
		BasePassParameters.EyeAdaptationTexture = GetRDG(View.GetEyeAdaptationTexture(), ERDGTextureFlags::MultiFrame);
	}
	else
	{
		BasePassParameters.EyeAdaptationTexture = WhiteDummy;
	}
}

TRDGUniformBufferRef<FOpaqueBasePassUniformParameters> CreateOpaqueBasePassUniformBuffer(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef ForwardScreenSpaceShadowMask,
	const FSceneWithoutWaterTextures* SceneWithoutWaterTextures,
	const int32 ViewIndex)
{
	FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	FOpaqueBasePassUniformParameters* BasePassParameters = GraphBuilder.AllocParameters<FOpaqueBasePassUniformParameters>();
	SetupSharedOpaqueBasePassParameters(&GraphBuilder, GraphBuilder.RHICmdList, SceneRenderTargets, View, ForwardScreenSpaceShadowMask, SceneWithoutWaterTextures, ViewIndex, *BasePassParameters);
	return GraphBuilder.CreateUniformBuffer(BasePassParameters);
}

TUniformBufferRef<FOpaqueBasePassUniformParameters> CreateOpaqueBasePassUniformBuffer(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	IPooledRenderTarget* ForwardScreenSpaceShadowMask)
{
	FRDGTextureRef ForwardScreenSpaceShadowMaskRDG = FRDGTexture::GetPassthrough(ForwardScreenSpaceShadowMask);

	FOpaqueBasePassUniformParameters BasePassParameters;
	SetupSharedOpaqueBasePassParameters(nullptr, RHICmdList, FSceneRenderTargets::Get(RHICmdList), View, ForwardScreenSpaceShadowMaskRDG, nullptr, 0, BasePassParameters);
	return TUniformBufferRef<FOpaqueBasePassUniformParameters>::CreateUniformBufferImmediate(BasePassParameters, UniformBuffer_SingleFrame);
}

static void ClearGBufferAtMaxZ(
	FRDGBuilder& GraphBuilder,
	TArrayView<const FViewInfo> Views,
	const FRenderTargetBindingSlots& BasePassRenderTargets,
	FLinearColor ClearColor0)
{
	check(Views.Num() > 0);

	SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_ClearGBufferAtMaxZ);
	RDG_EVENT_SCOPE(GraphBuilder, "ClearGBufferAtMaxZ");

	const uint32 ActiveTargetCount = BasePassRenderTargets.GetActiveCount();
	FGlobalShaderMap* ShaderMap = Views[0].ShaderMap;

	TShaderMapRef<TOneColorVS<true> > VertexShader(ShaderMap);
	TOneColorPixelShaderMRT::FPermutationDomain PermutationVector;
	PermutationVector.Set<TOneColorPixelShaderMRT::TOneColorPixelShaderNumOutputs>(ActiveTargetCount);
	TShaderMapRef<TOneColorPixelShaderMRT>PixelShader(ShaderMap, PermutationVector);

	auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
	PassParameters->RenderTargets = BasePassRenderTargets;

	// Clear each viewport by drawing background color at MaxZ depth
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];
		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
		RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

		GraphBuilder.AddPass(
			{},
			PassParameters,
			ERDGPassFlags::Raster,
			[&View, VertexShader, PixelShader, ActiveTargetCount, ClearColor0](FRHICommandListImmediate& RHICmdList)
		{
			const FLinearColor ClearColors[MaxSimultaneousRenderTargets] =
			{
				ClearColor0,
				FLinearColor(0.5f,0.5f,0.5f,0),
				FLinearColor(0,0,0,1),
				FLinearColor(0,0,0,0),
				FLinearColor(0,1,1,1),
				FLinearColor(1,1,1,1),
				FLinearColor::Transparent,
				FLinearColor::Transparent
			};

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			// Opaque rendering, depth test but no depth writes
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			VertexShader->SetDepthParameter(RHICmdList, float(ERHIZBuffer::FarPlane));

			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1);
			PixelShader->SetColors(RHICmdList, ClearColors, ActiveTargetCount);
			RHICmdList.SetStreamSource(0, GClearVertexBuffer.VertexBufferRHI, 0);
			RHICmdList.DrawPrimitive(0, 2, 1);
		});
	}
}

void FDeferredShadingSceneRenderer::RenderBasePass(
	FRDGBuilder& GraphBuilder,
	FExclusiveDepthStencil::Type BasePassDepthStencilAccess,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef SceneDepthTexture,
	ERenderTargetLoadAction SceneDepthLoadAction,
	FRDGTextureRef ForwardShadowMaskTexture)
{
	const bool bEnableParallelBasePasses = GRHICommandList.UseParallelAlgorithms() && CVarParallelBasePass.GetValueOnRenderThread();

	static const auto ClearMethodCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.ClearSceneMethod"));
	bool bRequiresRHIClear = true;
	bool bRequiresFarZQuadClear = false;

	if (ClearMethodCVar)
	{
		int32 ClearMethod = ClearMethodCVar->GetValueOnRenderThread();

		if (ClearMethod == 0 && !ViewFamily.EngineShowFlags.Game)
		{
			// Do not clear the scene only if the view family is in game mode.
			ClearMethod = 1;
		}

		switch (ClearMethod)
		{
		case 0: // No clear
			bRequiresRHIClear = false;
			bRequiresFarZQuadClear = false;
			break;

		case 1: // RHICmdList.Clear
			bRequiresRHIClear = true;
			bRequiresFarZQuadClear = false;
			break;

		case 2: // Clear using far-z quad
			bRequiresFarZQuadClear = true;
			bRequiresRHIClear = false;
			break;
		}
	}

	// Always perform a full buffer clear for wireframe, shader complexity view mode, and stationary light overlap viewmode.
	if (ViewFamily.EngineShowFlags.Wireframe || ViewFamily.EngineShowFlags.ShaderComplexity || ViewFamily.EngineShowFlags.StationaryLightOverlap)
	{
		bRequiresRHIClear = true;
		bRequiresFarZQuadClear = false;
	}

	const bool bIsWireframeRenderpass = ViewFamily.EngineShowFlags.Wireframe && FSceneRenderer::ShouldCompositeEditorPrimitives(Views[0]);
	const bool bDebugViewMode = ViewFamily.UseDebugViewPS();
	const bool bRenderLightmapDensity = ViewFamily.EngineShowFlags.LightMapDensity && AllowDebugViewmodes();
	const bool bRenderSkyAtmosphereEditorNotifications = ShouldRenderSkyAtmosphereEditorNotifications();
	const bool bDoParallelBasePass = bEnableParallelBasePasses && !bDebugViewMode && !bRenderLightmapDensity; // DebugView and LightmapDensity are non-parallel substitutions inside BasePass
	const bool bNeedsBeginRender = AllowDebugViewmodes() &&
		(ViewFamily.EngineShowFlags.RequiredTextureResolution ||
			ViewFamily.EngineShowFlags.MaterialTextureScaleAccuracy ||
			ViewFamily.EngineShowFlags.MeshUVDensityAccuracy ||
			ViewFamily.EngineShowFlags.PrimitiveDistanceAccuracy ||
			ViewFamily.EngineShowFlags.ShaderComplexity ||
			ViewFamily.EngineShowFlags.LODColoration ||
			ViewFamily.EngineShowFlags.HLODColoration);

	const FExclusiveDepthStencil ExclusiveDepthStencil(BasePassDepthStencilAccess);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

	TStaticArray<FRDGTextureRef, MaxSimultaneousRenderTargets> BasePassTextures;
	int32 GBufferDIndex = INDEX_NONE;
	uint32 BasePassTextureCount = SceneContext.GetGBufferRenderTargets(GraphBuilder, BasePassTextures, GBufferDIndex);
	TArrayView<FRDGTextureRef> BasePassTexturesView = MakeArrayView(BasePassTextures.GetData(), BasePassTextureCount);
	FRDGTextureRef BasePassDepthTexture = SceneDepthTexture;
	FLinearColor SceneColorClearValue;

	if (bRequiresRHIClear)
	{
		if (ViewFamily.EngineShowFlags.ShaderComplexity)
		{
			SceneContext.ClearQuadOverdrawUAV(GraphBuilder);
		}

		if (ViewFamily.EngineShowFlags.ShaderComplexity || ViewFamily.EngineShowFlags.StationaryLightOverlap)
		{
			SceneColorClearValue = FLinearColor(0, 0, 0, kSceneColorClearAlpha);
		}
		else
		{
			SceneColorClearValue = FLinearColor(Views[0].BackgroundColor.R, Views[0].BackgroundColor.G, Views[0].BackgroundColor.B, kSceneColorClearAlpha);
		}

		ERenderTargetLoadAction ColorLoadAction = ERenderTargetLoadAction::ELoad;

		if (SceneColorTexture->Desc.ClearValue.GetClearColor() == SceneColorClearValue)
		{
			ColorLoadAction = ERenderTargetLoadAction::EClear;
		}
		else
		{
			ColorLoadAction = ERenderTargetLoadAction::ENoAction;
		}

		auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
		PassParameters->RenderTargets = GetRenderTargetBindings(ColorLoadAction, BasePassTexturesView);

		static TConsoleVariableData<int32>* CVarNoGBufferDClear = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.NoGBufferDClear"));
		if (CVarNoGBufferDClear && !!CVarNoGBufferDClear->GetValueOnRenderThread() && GBufferDIndex != INDEX_NONE)
		{
			PassParameters->RenderTargets[GBufferDIndex].SetLoadAction(ERenderTargetLoadAction::ENoAction);
		}

		if (SceneDepthLoadAction == ERenderTargetLoadAction::EClear)
		{
			PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(BasePassDepthTexture, SceneDepthLoadAction, SceneDepthLoadAction, ExclusiveDepthStencil);
		}

		GraphBuilder.AddPass(RDG_EVENT_NAME("GBufferClear"), PassParameters, ERDGPassFlags::Raster,
			[PassParameters, ColorLoadAction, SceneColorClearValue](FRHICommandList& RHICmdList)
		{
			// If no fast-clear action was used, we need to do an MRT shader clear.
			if (ColorLoadAction == ERenderTargetLoadAction::ENoAction)
			{
				const FRenderTargetBindingSlots& RenderTargets = PassParameters->RenderTargets;
				FLinearColor ClearColors[MaxSimultaneousRenderTargets];
				FRHITexture* Textures[MaxSimultaneousRenderTargets];
				int32 TextureIndex = 0;

				RenderTargets.Enumerate([&](const FRenderTargetBinding& RenderTarget)
				{
					FRHITexture* TextureRHI = RenderTarget.GetTexture()->GetRHI();
					ClearColors[TextureIndex] = TextureIndex == 0 ? SceneColorClearValue : TextureRHI->GetClearColor();
					Textures[TextureIndex] = TextureRHI;
					++TextureIndex;
				});

				// Clear color only; depth-stencil is fast cleared.
				DrawClearQuadMRT(RHICmdList, true, TextureIndex, ClearColors, false, 0, false, 0);
			}
		});

		if (bRenderSkyAtmosphereEditorNotifications)
		{
			// We only render this warning text when bRequiresRHIClear==true to make sure the scene color buffer is allocated at this stage.
			// When false, the option specifies that all pixels must be written to by a sky dome anyway.
			RenderSkyAtmosphereEditorNotifications(GraphBuilder, SceneColorTexture);
		}
	}

	if (ViewFamily.EngineShowFlags.Wireframe)
	{
		checkf(ExclusiveDepthStencil.IsDepthWrite(), TEXT("Wireframe base pass requires depth-write, but it is set to read-only."));

		SceneContext.GetEditorPrimitivesColor(GraphBuilder.RHICmdList);
		SceneContext.GetEditorPrimitivesDepth(GraphBuilder.RHICmdList);

		BasePassTextureCount = 1;
		BasePassTextures[0] = GraphBuilder.RegisterExternalTexture(SceneContext.EditorPrimitivesColor, ERenderTargetTexture::Targetable);
		BasePassTexturesView = MakeArrayView(BasePassTextures.GetData(), BasePassTextureCount);

		BasePassDepthTexture = GraphBuilder.RegisterExternalTexture(SceneContext.EditorPrimitivesDepth, ERenderTargetTexture::Targetable);

		auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
		PassParameters->RenderTargets = GetRenderTargetBindings(ERenderTargetLoadAction::EClear, BasePassTexturesView);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(BasePassDepthTexture, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::EClear, ExclusiveDepthStencil);

		GraphBuilder.AddPass(RDG_EVENT_NAME("WireframeClear"), PassParameters, ERDGPassFlags::Raster, [](FRHICommandList&) {});
	}

	// Render targets bindings should remain constant at this point.
	FRenderTargetBindingSlots BasePassRenderTargets = GetRenderTargetBindings(ERenderTargetLoadAction::ELoad, BasePassTexturesView);
	BasePassRenderTargets.DepthStencil = FDepthStencilBinding(BasePassDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, ExclusiveDepthStencil);

	BasePassRenderTargets.ShadingRateTexture = GVRSImageManager.GetVariableRateShadingImage(GraphBuilder, ViewFamily, nullptr, EVRSType::None);

	AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_BasePass));
	RenderBasePassInternal(GraphBuilder, BasePassRenderTargets, BasePassDepthStencilAccess, ForwardShadowMaskTexture, bDoParallelBasePass, bRenderLightmapDensity);
	AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_AfterBasePass));

	if (ViewFamily.ViewExtensions.Num() > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_ViewExtensionPostRenderBasePass);
		RDG_EVENT_SCOPE(GraphBuilder, "BasePass_ViewExtensions");
		auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
		PassParameters->RenderTargets = BasePassRenderTargets;
		for (auto& ViewExtension : ViewFamily.ViewExtensions)
		{
			for (FViewInfo& View : Views)
			{
				RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
				GraphBuilder.AddPass(
					{},
					PassParameters,
					ERDGPassFlags::Raster,
					[&ViewExtension, &View](FRHICommandListImmediate& RHICmdList)
				{
					ViewExtension->PostRenderBasePass_RenderThread(RHICmdList, View);
				});
			}
		}
	}

	if (bRequiresFarZQuadClear)
	{
		ClearGBufferAtMaxZ(GraphBuilder, Views, BasePassRenderTargets, SceneColorClearValue);
	}

	if (ShouldRenderAnisotropyPass())
	{
		AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_AnisotropyPass));
		RenderAnisotropyPass(GraphBuilder, SceneDepthTexture, bEnableParallelBasePasses);
		AddSetCurrentStatPass(GraphBuilder, GET_STATID(STAT_CLM_AfterAnisotropyPass));
	}

	if (SceneContext.GBufferA)
	{
		AddAsyncComputeSRVTransitionHackPass(GraphBuilder, GraphBuilder.RegisterExternalTexture(SceneContext.GBufferA));
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FOpaqueBasePassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FOpaqueBasePassUniformParameters, BasePass)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

static void RenderEditorPrimitivesForDPG(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FOpaqueBasePassParameters* PassParameters,
	const FMeshPassProcessorRenderState& DrawRenderState,
	ESceneDepthPriorityGroup DepthPriorityGroup)
{
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("%s", *UEnum::GetValueAsString(DepthPriorityGroup)),
		PassParameters,
		ERDGPassFlags::Raster,
		[&View, DrawRenderState, DepthPriorityGroup](FRHICommandList& RHICmdList)
	{
		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

		View.SimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::OpaqueAndMasked, DepthPriorityGroup);

		if (!View.Family->EngineShowFlags.CompositeEditorPrimitives)
		{
			const bool bNeedToSwitchVerticalAxis = RHINeedsToSwitchVerticalAxis(View.GetShaderPlatform());

			DrawDynamicMeshPass(View, RHICmdList,
				[&View, &DrawRenderState](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
			{
				FEditorPrimitivesBasePassMeshProcessor PassMeshProcessor(
					View.Family->Scene->GetRenderScene(),
					View.GetFeatureLevel(),
					&View,
					DrawRenderState,
					false,
					DynamicMeshPassContext);

				const uint64 DefaultBatchElementMask = ~0ull;

				for (int32 MeshIndex = 0; MeshIndex < View.ViewMeshElements.Num(); MeshIndex++)
				{
					const FMeshBatch& MeshBatch = View.ViewMeshElements[MeshIndex];
					PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
				}
			});

			const FBatchedElements& BatchedViewElements = DepthPriorityGroup == SDPG_World ? View.BatchedViewElements : View.TopBatchedViewElements;

			DrawDynamicMeshPass(View, RHICmdList,
				[&View, &DrawRenderState](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
			{
				FEditorPrimitivesBasePassMeshProcessor PassMeshProcessor(
					View.Family->Scene->GetRenderScene(),
					View.GetFeatureLevel(),
					&View,
					DrawRenderState,
					false,
					DynamicMeshPassContext);

				const uint64 DefaultBatchElementMask = ~0ull;

				for (int32 MeshIndex = 0; MeshIndex < View.TopViewMeshElements.Num(); MeshIndex++)
				{
					const FMeshBatch& MeshBatch = View.TopViewMeshElements[MeshIndex];
					PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
				}
			});

			// Draw the view's batched simple elements(lines, sprites, etc).
			View.TopBatchedViewElements.Draw(RHICmdList, DrawRenderState, View.GetFeatureLevel(), bNeedToSwitchVerticalAxis, View, false);
		}
	});
}

static bool HasEditorPrimitivesForDPG(const FViewInfo& View, ESceneDepthPriorityGroup DepthPriorityGroup)
{
	bool bHasPrimitives = View.SimpleElementCollector.HasPrimitives(DepthPriorityGroup);

	if (!View.Family->EngineShowFlags.CompositeEditorPrimitives)
	{
		const TIndirectArray<FMeshBatch>& ViewMeshElementList = (DepthPriorityGroup == SDPG_Foreground ? View.TopViewMeshElements : View.ViewMeshElements);
		bHasPrimitives |= ViewMeshElementList.Num() > 0;

		const FBatchedElements& BatchedViewElements = DepthPriorityGroup == SDPG_World ? View.BatchedViewElements : View.TopBatchedViewElements;
		bHasPrimitives |= BatchedViewElements.HasPrimsToDraw();
	}

	return bHasPrimitives;
}

static void RenderEditorPrimitives(
	FRDGBuilder& GraphBuilder,
	FOpaqueBasePassParameters* PassParameters,
	const FViewInfo& View,
	const FMeshPassProcessorRenderState& DrawRenderState)
{
	RDG_EVENT_SCOPE(GraphBuilder, "EditorPrimitives");

	RenderEditorPrimitivesForDPG(GraphBuilder, View, PassParameters, DrawRenderState, SDPG_World);

	if (HasEditorPrimitivesForDPG(View, SDPG_Foreground))
	{
		// Write foreground primitives into depth buffer without testing 
		{
			auto* DepthWritePassParameters = GraphBuilder.AllocParameters<FOpaqueBasePassParameters>();
			*DepthWritePassParameters = *PassParameters;

			// Change to depth writable
			DepthWritePassParameters->RenderTargets.DepthStencil.SetDepthStencilAccess(FExclusiveDepthStencil::DepthWrite_StencilWrite);

			FMeshPassProcessorRenderState NoDepthTestDrawRenderState(DrawRenderState);
			NoDepthTestDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_Always>::GetRHI());
			NoDepthTestDrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthWrite_StencilWrite);
			RenderEditorPrimitivesForDPG(GraphBuilder, View, DepthWritePassParameters, NoDepthTestDrawRenderState, SDPG_Foreground);
		}

		// Render foreground primitives with depth testing
		RenderEditorPrimitivesForDPG(GraphBuilder, View, PassParameters, DrawRenderState, SDPG_Foreground);
	}
}

void FDeferredShadingSceneRenderer::RenderBasePassInternal(
	FRDGBuilder& GraphBuilder,
	const FRenderTargetBindingSlots& BasePassRenderTargets,
	FExclusiveDepthStencil::Type BasePassDepthStencilAccess,
	FRDGTextureRef ForwardScreenSpaceShadowMask,
	bool bParallelBasePass,
	bool bRenderLightmapDensity)
{
	RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderBasePass);
	SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_RenderBasePass, FColor::Emerald);

	if (bRenderLightmapDensity)
	{
		// Override the base pass with the lightmap density pass if the viewmode is enabled.
		RenderLightMapDensities(GraphBuilder, BasePassRenderTargets);
	}
	else if (ViewFamily.UseDebugViewPS())
	{
		// Override the base pass with one of the debug view shader mode (see EDebugViewShaderMode) if required.
		RenderDebugViewMode(GraphBuilder, BasePassRenderTargets);
	}
	else
	{
		SCOPE_CYCLE_COUNTER(STAT_BasePassDrawTime);
		RDG_EVENT_SCOPE(GraphBuilder, "BasePass");
		RDG_GPU_STAT_SCOPE(GraphBuilder, Basepass);

		if (bParallelBasePass)
		{
			RDG_WAIT_FOR_TASKS_CONDITIONAL(GraphBuilder, IsBasePassWaitForTasksEnabled());

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				FViewInfo& View = Views[ViewIndex];
				RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
				RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

				FMeshPassProcessorRenderState DrawRenderState(View);
				SetupBasePassState(BasePassDepthStencilAccess, ViewFamily.EngineShowFlags.ShaderComplexity, DrawRenderState);

				FOpaqueBasePassParameters* PassParameters = GraphBuilder.AllocParameters<FOpaqueBasePassParameters>();
				PassParameters->BasePass = CreateOpaqueBasePassUniformBuffer(GraphBuilder, View, ForwardScreenSpaceShadowMask, nullptr, ViewIndex);
				PassParameters->RenderTargets = BasePassRenderTargets;

				const bool bShouldRenderView = View.ShouldRenderView();
				if (bShouldRenderView)
				{
					GraphBuilder.AddPass(
						RDG_EVENT_NAME("BasePassParallel"),
						PassParameters,
						ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,
						[this, &View, PassParameters](FRHICommandListImmediate& RHICmdList)
					{
						Scene->UniformBuffers.UpdateViewUniformBuffer(View);
						FRDGParallelCommandListSet ParallelCommandListSet(RHICmdList, GET_STATID(STAT_CLP_BasePass), *this, View, FParallelCommandListBindings(PassParameters));
						View.ParallelMeshDrawCommandPasses[EMeshPass::BasePass].DispatchDraw(&ParallelCommandListSet, RHICmdList);
					});
				}

				RenderEditorPrimitives(GraphBuilder, PassParameters, View, DrawRenderState);

				if (bShouldRenderView && View.Family->EngineShowFlags.Atmosphere)
				{
					GraphBuilder.AddPass(
						RDG_EVENT_NAME("SkyPassParallel"),
						PassParameters,
						ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,
						[this, &View, PassParameters](FRHICommandListImmediate& RHICmdList)
					{
						FRDGParallelCommandListSet ParallelCommandListSet(RHICmdList, GET_STATID(STAT_CLP_BasePass), *this, View, FParallelCommandListBindings(PassParameters));
						View.ParallelMeshDrawCommandPasses[EMeshPass::SkyPass].DispatchDraw(&ParallelCommandListSet, RHICmdList);
					});
				}
			}
		}
		else
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				FViewInfo& View = Views[ViewIndex];
				RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
				RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

				FMeshPassProcessorRenderState DrawRenderState(View);
				SetupBasePassState(BasePassDepthStencilAccess, ViewFamily.EngineShowFlags.ShaderComplexity, DrawRenderState);

				FOpaqueBasePassParameters* PassParameters = GraphBuilder.AllocParameters<FOpaqueBasePassParameters>();
				PassParameters->BasePass = CreateOpaqueBasePassUniformBuffer(GraphBuilder, View, ForwardScreenSpaceShadowMask, nullptr, ViewIndex);
				PassParameters->RenderTargets = BasePassRenderTargets;

				const bool bShouldRenderView = View.ShouldRenderView();
				if (bShouldRenderView)
				{
					GraphBuilder.AddPass(
						RDG_EVENT_NAME("BasePass"),
						PassParameters,
						ERDGPassFlags::Raster,
						[this, &View, PassParameters](FRHICommandList& RHICmdList)
					{
						Scene->UniformBuffers.UpdateViewUniformBuffer(View);
						SetStereoViewport(RHICmdList, View, 1.0f);
						View.ParallelMeshDrawCommandPasses[EMeshPass::BasePass].DispatchDraw(nullptr, RHICmdList);
					});
				}

				RenderEditorPrimitives(GraphBuilder, PassParameters, View, DrawRenderState);

				if (bShouldRenderView && View.Family->EngineShowFlags.Atmosphere)
				{
					GraphBuilder.AddPass(
						RDG_EVENT_NAME("SkyPass"),
						PassParameters,
						ERDGPassFlags::Raster,
						[this, &View, PassParameters](FRHICommandList& RHICmdList)
					{
						SetStereoViewport(RHICmdList, View, 1.0f);
						View.ParallelMeshDrawCommandPasses[EMeshPass::SkyPass].DispatchDraw(nullptr, RHICmdList);
					});
				}
			}
		}
	}
}

template<typename LightMapPolicyType>
bool FBasePassMeshProcessor::Process(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	EBlendMode BlendMode,
	FMaterialShadingModelField ShadingModels,
	const LightMapPolicyType& RESTRICT LightMapPolicy,
	const typename LightMapPolicyType::ElementDataType& RESTRICT LightMapElementData,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	const bool bRenderSkylight = Scene && Scene->ShouldRenderSkylightInBasePass(BlendMode) && ShadingModels.IsLit();
	const bool bRenderAtmosphericFog = IsTranslucentBlendMode(BlendMode) && (Scene && Scene->HasAtmosphericFog() && Scene->ReadOnlyCVARCache.bEnableAtmosphericFog);

	TMeshProcessorShaders<
		TBasePassVertexShaderPolicyParamType<LightMapPolicyType>,
		FBaseHS,
		FBaseDS,
		TBasePassPixelShaderPolicyParamType<LightMapPolicyType>> BasePassShaders;

	if (!GetBasePassShaders<LightMapPolicyType>(
		MaterialResource,
		VertexFactory->GetType(),
		LightMapPolicy,
		FeatureLevel,
		bRenderAtmosphericFog,
		bRenderSkylight,
		Get128BitRequirement(),
		BasePassShaders.HullShader,
		BasePassShaders.DomainShader,
		BasePassShaders.VertexShader,
		BasePassShaders.PixelShader
		))
	{
		return false;
	}


	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);

	SetDepthStencilStateForBasePass(
		ViewIfDynamicMeshCommand,
		DrawRenderState,
		FeatureLevel,
		MeshBatch,
		StaticMeshId,
		PrimitiveSceneProxy,
		MaterialResource,
		bEnableReceiveDecalOutput);

	if (bTranslucentBasePass)
	{
		SetTranslucentRenderState(DrawRenderState, MaterialResource, GShaderPlatformForFeatureLevel[FeatureLevel], TranslucencyPassType);
	}

	TBasePassShaderElementData<LightMapPolicyType> ShaderElementData(LightMapElementData);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, true);

	FMeshDrawCommandSortKey SortKey = FMeshDrawCommandSortKey::Default;

	if (bTranslucentBasePass)
	{
		SortKey = CalculateTranslucentMeshStaticSortKey(PrimitiveSceneProxy, MeshBatch.MeshIdInPrimitive);
	}
	else
	{
		SortKey = CalculateBasePassMeshStaticSortKey(EarlyZPassMode, BlendMode, BasePassShaders.VertexShader.GetShader(), BasePassShaders.PixelShader.GetShader());
	}

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		DrawRenderState,
		BasePassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);

	return true;
}

bool FBasePassMeshProcessor::AddMeshBatchForSimpleForwardShading(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material,
	const FLightMapInteraction& LightMapInteraction,
	bool bIsLitMaterial,
	bool bAllowStaticLighting,
	bool bUseVolumetricLightmap,
	bool bAllowIndirectLightingCache,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const EBlendMode BlendMode = Material.GetBlendMode();
	const FMaterialShadingModelField ShadingModels = Material.GetShadingModels();

	bool bResult = false;
	if (bAllowStaticLighting && LightMapInteraction.GetType() == LMIT_Texture)
	{
		const FShadowMapInteraction ShadowMapInteraction = (MeshBatch.LCI && bIsLitMaterial)
			? MeshBatch.LCI->GetShadowMapInteraction(FeatureLevel)
			: FShadowMapInteraction();

		if (ShadowMapInteraction.GetType() == SMIT_Texture)
		{
			bResult = Process< FUniformLightMapPolicy >(
				MeshBatch,
				BatchElementMask,
				StaticMeshId,
				PrimitiveSceneProxy,
				MaterialRenderProxy,
				Material,
				BlendMode,
				ShadingModels,
				FUniformLightMapPolicy(LMP_SIMPLE_STATIONARY_PRECOMPUTED_SHADOW_LIGHTING),
				MeshBatch.LCI,
				MeshFillMode,
				MeshCullMode);
		}
		else
		{
			bResult = Process< FUniformLightMapPolicy >(
				MeshBatch,
				BatchElementMask,
				StaticMeshId,
				PrimitiveSceneProxy,
				MaterialRenderProxy,
				Material,
				BlendMode,
				ShadingModels,
				FUniformLightMapPolicy(LMP_SIMPLE_LIGHTMAP_ONLY_LIGHTING),
				MeshBatch.LCI,
				MeshFillMode,
				MeshCullMode);
		}
	}
	else if (bIsLitMaterial
		&& bAllowStaticLighting
		&& bUseVolumetricLightmap
		&& PrimitiveSceneProxy)
	{
		bResult = Process< FUniformLightMapPolicy >(
			MeshBatch,
			BatchElementMask,
			StaticMeshId,
			PrimitiveSceneProxy,
			MaterialRenderProxy,
			Material,
			BlendMode,
			ShadingModels,
			FUniformLightMapPolicy(LMP_SIMPLE_STATIONARY_VOLUMETRICLIGHTMAP_SHADOW_LIGHTING),
			MeshBatch.LCI,
			MeshFillMode,
			MeshCullMode);
	}
	else if (bIsLitMaterial
		&& IsIndirectLightingCacheAllowed(FeatureLevel)
		&& bAllowIndirectLightingCache
		&& PrimitiveSceneProxy)
	{
		const FIndirectLightingCacheAllocation* IndirectLightingCacheAllocation = PrimitiveSceneProxy->GetPrimitiveSceneInfo()->IndirectLightingCacheAllocation;
		const bool bPrimitiveIsMovable = PrimitiveSceneProxy->IsMovable();
		const bool bPrimitiveUsesILC = PrimitiveSceneProxy->GetIndirectLightingCacheQuality() != ILCQ_Off;

		// Use the indirect lighting cache shaders if the object has a cache allocation
		// This happens for objects with unbuilt lighting
		if (bPrimitiveUsesILC &&
			((IndirectLightingCacheAllocation && IndirectLightingCacheAllocation->IsValid())
				// Use the indirect lighting cache shaders if the object is movable, it may not have a cache allocation yet because that is done in InitViews
				// And movable objects are sometimes rendered in the static draw lists
				|| bPrimitiveIsMovable))
		{
			// Use a lightmap policy that supports reading indirect lighting from a single SH sample
			bResult = Process< FUniformLightMapPolicy >(
				MeshBatch,
				BatchElementMask,
				StaticMeshId,
				PrimitiveSceneProxy,
				MaterialRenderProxy,
				Material,
				BlendMode,
				ShadingModels,
				FUniformLightMapPolicy(LMP_SIMPLE_STATIONARY_SINGLESAMPLE_SHADOW_LIGHTING),
				MeshBatch.LCI,
				MeshFillMode,
				MeshCullMode);
		}
		else
		{
			bResult = Process< FUniformLightMapPolicy >(
				MeshBatch,
				BatchElementMask,
				StaticMeshId,
				PrimitiveSceneProxy,
				MaterialRenderProxy,
				Material,
				BlendMode,
				ShadingModels,
				FUniformLightMapPolicy(LMP_SIMPLE_NO_LIGHTMAP),
				MeshBatch.LCI,
				MeshFillMode,
				MeshCullMode);
		}
	}
	else if (bIsLitMaterial)
	{
		// Always choosing shaders to support dynamic directional even if one is not present
		bResult = Process< FUniformLightMapPolicy >(
			MeshBatch,
			BatchElementMask,
			StaticMeshId,
			PrimitiveSceneProxy,
			MaterialRenderProxy,
			Material,
			BlendMode,
			ShadingModels,
			FUniformLightMapPolicy(LMP_SIMPLE_DIRECTIONAL_LIGHT_LIGHTING),
			MeshBatch.LCI,
			MeshFillMode,
			MeshCullMode);
	}
	else
	{
	bResult = Process< FUniformLightMapPolicy >(
			MeshBatch,
			BatchElementMask,
			StaticMeshId,
			PrimitiveSceneProxy,
			MaterialRenderProxy,
			Material,
			BlendMode,
			ShadingModels,
			FUniformLightMapPolicy(LMP_SIMPLE_NO_LIGHTMAP),
			MeshBatch.LCI,
			MeshFillMode,
			MeshCullMode);
	}
	return bResult;
}

void FBasePassMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (MeshBatch.bUseForMaterial)
	{
		// Determine the mesh's material and blend mode.
		const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
		while (MaterialRenderProxy)
		{
			const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
			if (Material && Material->GetRenderingThreadShaderMap())
			{
				if (TryAddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, *MaterialRenderProxy, *Material))
				{
					break;
				}
			}

			MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);
		}
	}
}

bool FBasePassMeshProcessor::TryAddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, const FMaterialRenderProxy& MaterialRenderProxy, const FMaterial& Material)
{
	// Determine the mesh's material and blend mode.
	const EBlendMode BlendMode = Material.GetBlendMode();
	const FMaterialShadingModelField ShadingModels = Material.GetShadingModels();
	const bool bIsTranslucent = IsTranslucentBlendMode(BlendMode);
	const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
	const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
	const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);

	bool bShouldDraw = false;

	if (bTranslucentBasePass)
	{
		if (bIsTranslucent && !Material.IsDeferredDecal())
		{
			switch (TranslucencyPassType)
			{
			case ETranslucencyPass::TPT_StandardTranslucency:
				bShouldDraw = !Material.IsTranslucencyAfterDOFEnabled();
				break;

			case ETranslucencyPass::TPT_TranslucencyAfterDOF:
				bShouldDraw = Material.IsTranslucencyAfterDOFEnabled();
				break;

			// only dual blended or modulate surfaces need background modulation
			case ETranslucencyPass::TPT_TranslucencyAfterDOFModulate:
				bShouldDraw = Material.IsTranslucencyAfterDOFEnabled() && (Material.IsDualBlendingEnabled(GetFeatureLevelShaderPlatform(FeatureLevel)) || BlendMode == BLEND_Modulate);
				break;

			case ETranslucencyPass::TPT_AllTranslucency:
				bShouldDraw = true;
				break;
			}
		}
	}
	else
	{
		bShouldDraw = !bIsTranslucent;
	}

	// Only draw opaque materials.
	bool bResult = true;
	if (bShouldDraw
		&& (!PrimitiveSceneProxy || PrimitiveSceneProxy->ShouldRenderInMainPass())
		&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain())
		&& ShouldIncludeMaterialInDefaultOpaquePass(Material))
	{
		// Check for a cached light-map.
		const bool bIsLitMaterial = ShadingModels.IsLit();
		static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
		const bool bAllowStaticLighting = (!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnRenderThread() != 0);

		const FLightMapInteraction LightMapInteraction = (bAllowStaticLighting && MeshBatch.LCI && bIsLitMaterial)
			? MeshBatch.LCI->GetLightMapInteraction(FeatureLevel)
			: FLightMapInteraction();

		// force LQ lightmaps based on system settings
		const bool bPlatformAllowsHighQualityLightMaps = AllowHighQualityLightmaps(FeatureLevel);
		const bool bAllowHighQualityLightMaps = bPlatformAllowsHighQualityLightMaps && LightMapInteraction.AllowsHighQualityLightmaps();

		const bool bAllowIndirectLightingCache = Scene && Scene->PrecomputedLightVolumes.Num() > 0;
		const bool bUseVolumetricLightmap = Scene && Scene->VolumetricLightmapSceneData.HasData();

		FMeshMaterialShaderElementData MeshMaterialShaderElementData;
		MeshMaterialShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, true);

		if (IsSimpleForwardShadingEnabled(GetFeatureLevelShaderPlatform(FeatureLevel)))
		{
			// Only compiling simple lighting shaders for HQ lightmaps to save on permutations
			check(bPlatformAllowsHighQualityLightMaps);
			bResult = AddMeshBatchForSimpleForwardShading(
				MeshBatch,
				BatchElementMask,
				StaticMeshId,
				PrimitiveSceneProxy,
				MaterialRenderProxy,
				Material,
				LightMapInteraction,
				bIsLitMaterial,
				bAllowStaticLighting,
				bUseVolumetricLightmap,
				bAllowIndirectLightingCache,
				MeshFillMode,
				MeshCullMode);
		}
		// Render volumetric translucent self-shadowing only for >= SM4 and fallback to non-shadowed for lesser shader models
		else if (bIsLitMaterial
			&& bIsTranslucent
			&& PrimitiveSceneProxy
			&& PrimitiveSceneProxy->CastsVolumetricTranslucentShadow())
		{
			checkSlow(ViewIfDynamicMeshCommand && ViewIfDynamicMeshCommand->bIsViewInfo);
			const FViewInfo* ViewInfo = (FViewInfo*)ViewIfDynamicMeshCommand;

			const int32 PrimitiveIndex = PrimitiveSceneProxy->GetPrimitiveSceneInfo()->GetIndex();

			const FUniformBufferRHIRef* UniformBufferPtr = ViewInfo->TranslucentSelfShadowUniformBufferMap.Find(PrimitiveIndex);

			FSelfShadowLightCacheElementData ElementData;
			ElementData.LCI = MeshBatch.LCI;
			ElementData.SelfShadowTranslucencyUniformBuffer = UniformBufferPtr ? (*UniformBufferPtr).GetReference() : GEmptyTranslucentSelfShadowUniformBuffer.GetUniformBufferRHI();

			if (bIsLitMaterial
				&& bAllowStaticLighting
				&& bUseVolumetricLightmap
				&& PrimitiveSceneProxy)
			{
				bResult = Process< FSelfShadowedVolumetricLightmapPolicy >(
					MeshBatch,
					BatchElementMask,
					StaticMeshId,
					PrimitiveSceneProxy,
					MaterialRenderProxy,
					Material,
					BlendMode,
					ShadingModels,
					FSelfShadowedVolumetricLightmapPolicy(),
					ElementData,
					MeshFillMode,
					MeshCullMode);
			}
			else if (IsIndirectLightingCacheAllowed(FeatureLevel)
				&& bAllowIndirectLightingCache
				&& PrimitiveSceneProxy)
			{
				// Apply cached point indirect lighting as well as self shadowing if needed
				bResult = Process< FSelfShadowedCachedPointIndirectLightingPolicy >(
					MeshBatch,
					BatchElementMask,
					StaticMeshId,
					PrimitiveSceneProxy,
					MaterialRenderProxy,
					Material,
					BlendMode,
					ShadingModels,
					FSelfShadowedCachedPointIndirectLightingPolicy(),
					ElementData,
					MeshFillMode,
					MeshCullMode);
			}
			else
			{
				bResult = Process< FSelfShadowedTranslucencyPolicy >(
					MeshBatch,
					BatchElementMask,
					StaticMeshId,
					PrimitiveSceneProxy,
					MaterialRenderProxy,
					Material,
					BlendMode,
					ShadingModels,
					FSelfShadowedTranslucencyPolicy(),
					ElementData.SelfShadowTranslucencyUniformBuffer,
					MeshFillMode,
					MeshCullMode);
			}
		}
		else
		{
			static const auto CVarSupportLowQualityLightmap = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportLowQualityLightmaps"));
			const bool bAllowLowQualityLightMaps = (!CVarSupportLowQualityLightmap) || (CVarSupportLowQualityLightmap->GetValueOnAnyThread() != 0);

			switch (LightMapInteraction.GetType())
			{
			case LMIT_Texture:
				if (bAllowHighQualityLightMaps)
				{
					const FShadowMapInteraction ShadowMapInteraction = (bAllowStaticLighting && MeshBatch.LCI && bIsLitMaterial)
						? MeshBatch.LCI->GetShadowMapInteraction(FeatureLevel)
						: FShadowMapInteraction();

					if (ShadowMapInteraction.GetType() == SMIT_Texture)
					{
						bResult = Process< FUniformLightMapPolicy >(
							MeshBatch,
							BatchElementMask,
							StaticMeshId,
							PrimitiveSceneProxy,
							MaterialRenderProxy,
							Material,
							BlendMode,
							ShadingModels,
							FUniformLightMapPolicy(LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP),
							MeshBatch.LCI,
							MeshFillMode,
							MeshCullMode);
					}
					else
					{
						bResult = Process< FUniformLightMapPolicy >(
							MeshBatch,
							BatchElementMask,
							StaticMeshId,
							PrimitiveSceneProxy,
							MaterialRenderProxy,
							Material,
							BlendMode,
							ShadingModels,
							FUniformLightMapPolicy(LMP_HQ_LIGHTMAP),
							MeshBatch.LCI,
							MeshFillMode,
							MeshCullMode);
					}
				}
				else if (bAllowLowQualityLightMaps)
				{
					bResult = Process< FUniformLightMapPolicy >(
						MeshBatch,
						BatchElementMask,
						StaticMeshId,
						PrimitiveSceneProxy,
						MaterialRenderProxy,
						Material,
						BlendMode,
						ShadingModels,
						FUniformLightMapPolicy(LMP_LQ_LIGHTMAP),
						MeshBatch.LCI,
						MeshFillMode,
						MeshCullMode);
				}
				else
				{
					bResult = Process< FUniformLightMapPolicy >(
						MeshBatch,
						BatchElementMask,
						StaticMeshId,
						PrimitiveSceneProxy,
						MaterialRenderProxy,
						Material,
						BlendMode,
						ShadingModels,
						FUniformLightMapPolicy(LMP_NO_LIGHTMAP),
						MeshBatch.LCI,
						MeshFillMode,
						MeshCullMode);
				}
				break;
			default:
				if (bIsLitMaterial
					&& bAllowStaticLighting
					&& Scene
					&& Scene->VolumetricLightmapSceneData.HasData()
					&& PrimitiveSceneProxy
					&& (PrimitiveSceneProxy->IsMovable()
						|| PrimitiveSceneProxy->NeedsUnbuiltPreviewLighting()
						|| PrimitiveSceneProxy->GetLightmapType() == ELightmapType::ForceVolumetric))
				{
					bResult = Process< FUniformLightMapPolicy >(
						MeshBatch,
						BatchElementMask,
						StaticMeshId,
						PrimitiveSceneProxy,
						MaterialRenderProxy,
						Material,
						BlendMode,
						ShadingModels,
						FUniformLightMapPolicy(LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING),
						MeshBatch.LCI,
						MeshFillMode,
						MeshCullMode);
				}
				else if (bIsLitMaterial
					&& IsIndirectLightingCacheAllowed(FeatureLevel)
					&& Scene
					&& Scene->PrecomputedLightVolumes.Num() > 0
					&& PrimitiveSceneProxy)
				{
					const FIndirectLightingCacheAllocation* IndirectLightingCacheAllocation = PrimitiveSceneProxy->GetPrimitiveSceneInfo()->IndirectLightingCacheAllocation;
					const bool bPrimitiveIsMovable = PrimitiveSceneProxy->IsMovable();
					const bool bPrimitiveUsesILC = PrimitiveSceneProxy->GetIndirectLightingCacheQuality() != ILCQ_Off;

					// Use the indirect lighting cache shaders if the object has a cache allocation
					// This happens for objects with unbuilt lighting
					if (bPrimitiveUsesILC &&
						((IndirectLightingCacheAllocation && IndirectLightingCacheAllocation->IsValid())
							// Use the indirect lighting cache shaders if the object is movable, it may not have a cache allocation yet because that is done in InitViews
							// And movable objects are sometimes rendered in the static draw lists
							|| bPrimitiveIsMovable))
					{
						if (CanIndirectLightingCacheUseVolumeTexture(FeatureLevel)
							// Translucency forces point sample for pixel performance
							&& !bIsTranslucent
							&& ((IndirectLightingCacheAllocation && !IndirectLightingCacheAllocation->bPointSample)
								|| (bPrimitiveIsMovable && PrimitiveSceneProxy->GetIndirectLightingCacheQuality() == ILCQ_Volume)))
						{
							// Use a lightmap policy that supports reading indirect lighting from a volume texture for dynamic objects
							bResult = Process< FUniformLightMapPolicy >(
								MeshBatch,
								BatchElementMask,
								StaticMeshId,
								PrimitiveSceneProxy,
								MaterialRenderProxy,
								Material,
								BlendMode,
								ShadingModels,
								FUniformLightMapPolicy(LMP_CACHED_VOLUME_INDIRECT_LIGHTING),
								MeshBatch.LCI,
								MeshFillMode,
								MeshCullMode);
						}
						else
						{
							// Use a lightmap policy that supports reading indirect lighting from a single SH sample
							bResult = Process< FUniformLightMapPolicy >(
								MeshBatch,
								BatchElementMask,
								StaticMeshId,
								PrimitiveSceneProxy,
								MaterialRenderProxy,
								Material,
								BlendMode,
								ShadingModels,
								FUniformLightMapPolicy(LMP_CACHED_POINT_INDIRECT_LIGHTING),
								MeshBatch.LCI,
								MeshFillMode,
								MeshCullMode);
						}
					}
					else
					{
						bResult = Process< FUniformLightMapPolicy >(
							MeshBatch,
							BatchElementMask,
							StaticMeshId,
							PrimitiveSceneProxy,
							MaterialRenderProxy,
							Material,
							BlendMode,
							ShadingModels,
							FUniformLightMapPolicy(LMP_NO_LIGHTMAP),
							MeshBatch.LCI,
							MeshFillMode,
							MeshCullMode);
					}
				}
				else
				{
					bResult = Process< FUniformLightMapPolicy >(
						MeshBatch,
						BatchElementMask,
						StaticMeshId,
						PrimitiveSceneProxy,
						MaterialRenderProxy,
						Material,
						BlendMode,
						ShadingModels,
						FUniformLightMapPolicy(LMP_NO_LIGHTMAP),
						MeshBatch.LCI,
						MeshFillMode,
						MeshCullMode);
				}
				break;
			};
		}
	}

	return bResult;
}

FBasePassMeshProcessor::FBasePassMeshProcessor(
	const FScene* Scene,
	ERHIFeatureLevel::Type InFeatureLevel,
	const FSceneView* InViewIfDynamicMeshCommand,
	const FMeshPassProcessorRenderState& InDrawRenderState,
	FMeshPassDrawListContext* InDrawListContext,
	EFlags Flags,
	ETranslucencyPass::Type InTranslucencyPassType)
	: FMeshPassProcessor(Scene, InFeatureLevel, InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InDrawRenderState)
	, TranslucencyPassType(InTranslucencyPassType)
	, bTranslucentBasePass(InTranslucencyPassType != ETranslucencyPass::TPT_MAX)
	, bEnableReceiveDecalOutput((Flags & EFlags::CanUseDepthStencil) == EFlags::CanUseDepthStencil)
	, EarlyZPassMode(Scene ? Scene->EarlyZPassMode : DDM_None)
	, bRequiresExplicit128bitRT((Flags & EFlags::bRequires128bitRT) == EFlags::bRequires128bitRT)
{
}

FMeshPassProcessor* CreateBasePassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	SetupBasePassState(Scene->DefaultBasePassDepthStencilAccess, false, PassDrawRenderState);

	const FBasePassMeshProcessor::EFlags Flags = FBasePassMeshProcessor::EFlags::CanUseDepthStencil;

	return new(FMemStack::Get()) FBasePassMeshProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, PassDrawRenderState, InDrawListContext, Flags);
}

FMeshPassProcessor* CreateTranslucencyStandardPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());

	const FBasePassMeshProcessor::EFlags Flags = FBasePassMeshProcessor::EFlags::CanUseDepthStencil;

	return new(FMemStack::Get()) FBasePassMeshProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, PassDrawRenderState, InDrawListContext, Flags, ETranslucencyPass::TPT_StandardTranslucency);
}

FMeshPassProcessor* CreateTranslucencyAfterDOFProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());

	const FBasePassMeshProcessor::EFlags Flags = FBasePassMeshProcessor::EFlags::CanUseDepthStencil;

	return new(FMemStack::Get()) FBasePassMeshProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, PassDrawRenderState, InDrawListContext, Flags, ETranslucencyPass::TPT_TranslucencyAfterDOF);
}

FMeshPassProcessor* CreateTranslucencyAfterDOFModulateProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());

	const FBasePassMeshProcessor::EFlags Flags = FBasePassMeshProcessor::EFlags::CanUseDepthStencil;

	return new(FMemStack::Get()) FBasePassMeshProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, PassDrawRenderState, InDrawListContext, Flags, ETranslucencyPass::TPT_TranslucencyAfterDOFModulate);
}

FMeshPassProcessor* CreateTranslucencyAllPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());

	const FBasePassMeshProcessor::EFlags Flags = FBasePassMeshProcessor::EFlags::CanUseDepthStencil;

	return new(FMemStack::Get()) FBasePassMeshProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, PassDrawRenderState, InDrawListContext, Flags, ETranslucencyPass::TPT_AllTranslucency);
}

FRegisterPassProcessorCreateFunction RegisterBasePass(&CreateBasePassProcessor, EShadingPath::Deferred, EMeshPass::BasePass, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterTranslucencyStandardPass(&CreateTranslucencyStandardPassProcessor, EShadingPath::Deferred, EMeshPass::TranslucencyStandard, EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterTranslucencyAfterDOFPass(&CreateTranslucencyAfterDOFProcessor, EShadingPath::Deferred, EMeshPass::TranslucencyAfterDOF, EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterTranslucencyAfterDOFModulatePass(&CreateTranslucencyAfterDOFModulateProcessor, EShadingPath::Deferred, EMeshPass::TranslucencyAfterDOFModulate, EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterTranslucencyAllPass(&CreateTranslucencyAllPassProcessor, EShadingPath::Deferred, EMeshPass::TranslucencyAll, EMeshPassFlags::MainView);