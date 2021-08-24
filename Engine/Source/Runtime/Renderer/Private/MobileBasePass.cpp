// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MobileBasePassRendering.cpp: Base pass rendering implementation.
=============================================================================*/

#include "MobileBasePassRendering.h"
#include "TranslucentRendering.h"
#include "DynamicPrimitiveDrawing.h"
#include "ScenePrivate.h"
#include "ShaderPlatformQualitySettings.h"
#include "MaterialShaderQualitySettings.h"
#include "PrimitiveSceneInfo.h"
#include "MeshPassProcessor.inl"

template <ELightMapPolicyType Policy, int32 NumMovablePointLights>
bool GetUniformMobileBasePassShaders(
	const FMaterial& Material, 
	FVertexFactoryType* VertexFactoryType, 
	bool bEnableSkyLight,
	TShaderRef<TMobileBasePassVSPolicyParamType<FUniformLightMapPolicy>>& VertexShader,
	TShaderRef<TMobileBasePassPSPolicyParamType<FUniformLightMapPolicy>>& PixelShader
	)
{
	using FVertexShaderType = TMobileBasePassVSPolicyParamType<FUniformLightMapPolicy>;
	using FPixelShaderType = TMobileBasePassPSPolicyParamType<FUniformLightMapPolicy>;

	FMaterialShaderTypes ShaderTypes;
	if (IsMobileHDR())
	{
		ShaderTypes.AddShaderType<TMobileBasePassVS<TUniformLightMapPolicy<Policy>, HDR_LINEAR_64>>();

		if (bEnableSkyLight)
		{
			ShaderTypes.AddShaderType<TMobileBasePassPS<TUniformLightMapPolicy<Policy>, HDR_LINEAR_64, true, NumMovablePointLights>>();
		}
		else
		{
			ShaderTypes.AddShaderType<TMobileBasePassPS<TUniformLightMapPolicy<Policy>, HDR_LINEAR_64, false, NumMovablePointLights>>();
		}	
	}
	else
	{
		ShaderTypes.AddShaderType<TMobileBasePassVS<TUniformLightMapPolicy<Policy>, LDR_GAMMA_32>>();

		if (bEnableSkyLight)
		{
			ShaderTypes.AddShaderType<TMobileBasePassPS<TUniformLightMapPolicy<Policy>, LDR_GAMMA_32, true, NumMovablePointLights>>();
		}
		else
		{
			ShaderTypes.AddShaderType<TMobileBasePassPS<TUniformLightMapPolicy<Policy>, LDR_GAMMA_32, false, NumMovablePointLights>>();
		}			
	}

	FMaterialShaders Shaders;
	if (!Material.TryGetShaders(ShaderTypes, VertexFactoryType, Shaders))
	{
		return false;
	}

	Shaders.TryGetVertexShader(VertexShader);
	Shaders.TryGetPixelShader(PixelShader);
	return true;
}

template <int32 NumMovablePointLights>
bool GetMobileBasePassShaders(
	ELightMapPolicyType LightMapPolicyType, 
	const FMaterial& Material, 
	FVertexFactoryType* VertexFactoryType, 
	bool bEnableSkyLight,
	TShaderRef<TMobileBasePassVSPolicyParamType<FUniformLightMapPolicy>>& VertexShader,
	TShaderRef<TMobileBasePassPSPolicyParamType<FUniformLightMapPolicy>>& PixelShader
	)
{
	switch (LightMapPolicyType)
	{
	case LMP_NO_LIGHTMAP:
		return GetUniformMobileBasePassShaders<LMP_NO_LIGHTMAP, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
	case LMP_LQ_LIGHTMAP:
		return GetUniformMobileBasePassShaders<LMP_LQ_LIGHTMAP, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
	case LMP_MOBILE_DISTANCE_FIELD_SHADOWS_AND_LQ_LIGHTMAP:
		return GetUniformMobileBasePassShaders<LMP_MOBILE_DISTANCE_FIELD_SHADOWS_AND_LQ_LIGHTMAP, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
	case LMP_MOBILE_DISTANCE_FIELD_SHADOWS_LIGHTMAP_AND_CSM:
		return GetUniformMobileBasePassShaders<LMP_MOBILE_DISTANCE_FIELD_SHADOWS_LIGHTMAP_AND_CSM, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
	case LMP_MOBILE_DIRECTIONAL_LIGHT_CSM_AND_LIGHTMAP:
		return GetUniformMobileBasePassShaders<LMP_MOBILE_DIRECTIONAL_LIGHT_CSM_AND_LIGHTMAP, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
	case LMP_MOBILE_DIRECTIONAL_LIGHT_AND_SH_INDIRECT:
		return GetUniformMobileBasePassShaders<LMP_MOBILE_DIRECTIONAL_LIGHT_AND_SH_INDIRECT, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
	case LMP_MOBILE_DIRECTIONAL_LIGHT_CSM_AND_SH_INDIRECT:
		return GetUniformMobileBasePassShaders<LMP_MOBILE_DIRECTIONAL_LIGHT_CSM_AND_SH_INDIRECT, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
	case LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_WITH_LIGHTMAP:
		return GetUniformMobileBasePassShaders<LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_WITH_LIGHTMAP, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
	case LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM_WITH_LIGHTMAP:
		return GetUniformMobileBasePassShaders<LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM_WITH_LIGHTMAP, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
	case LMP_MOBILE_DIRECTIONAL_LIGHT_CSM:
		return GetUniformMobileBasePassShaders<LMP_MOBILE_DIRECTIONAL_LIGHT_CSM, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
	default:										
		check(false);
		return true;
	}
}

bool MobileBasePass::GetShaders(
	ELightMapPolicyType LightMapPolicyType,
	int32 NumMovablePointLights, 
	const FMaterial& MaterialResource,
	FVertexFactoryType* VertexFactoryType,
	bool bEnableSkyLight, 
	TShaderRef<TMobileBasePassVSPolicyParamType<FUniformLightMapPolicy>>& VertexShader,
	TShaderRef<TMobileBasePassPSPolicyParamType<FUniformLightMapPolicy>>& PixelShader)
{
	bool bIsLit = (MaterialResource.GetShadingModels().IsLit());
	if (bIsLit && !UseSkylightPermutation(bEnableSkyLight, FReadOnlyCVARCache::Get().MobileSkyLightPermutation))	
	{
		bEnableSkyLight = !bEnableSkyLight;
	}

	switch (NumMovablePointLights)
	{
	case INT32_MAX:
		return GetMobileBasePassShaders<INT32_MAX>(
			LightMapPolicyType,
			MaterialResource,
			VertexFactoryType,
			bEnableSkyLight,
			VertexShader,
			PixelShader
			);
	case 1:
		return GetMobileBasePassShaders<1>(
			LightMapPolicyType, 
			MaterialResource, 
			VertexFactoryType, 
			bEnableSkyLight, 
			VertexShader,
			PixelShader
			);
	case 2:
		return GetMobileBasePassShaders<2>(
			LightMapPolicyType,
			MaterialResource,
			VertexFactoryType,
			bEnableSkyLight,
			VertexShader,
			PixelShader
			);
	case 3:
		return GetMobileBasePassShaders<3>(
			LightMapPolicyType,
			MaterialResource,
			VertexFactoryType,
			bEnableSkyLight,
			VertexShader,
			PixelShader
			);
	case 4:
		return GetMobileBasePassShaders<4>(
			LightMapPolicyType,
			MaterialResource,
			VertexFactoryType,
			bEnableSkyLight,
			VertexShader,
			PixelShader
			);
	case 0:
	default:
		return GetMobileBasePassShaders<0>(
			LightMapPolicyType,
			MaterialResource,
			VertexFactoryType,
			bEnableSkyLight,
			VertexShader,
			PixelShader
			);
	}
}

static bool UseSkyReflectionCapture(const FScene* RenderScene)
{
	return RenderScene
		&& RenderScene->ReflectionSceneData.RegisteredReflectionCapturePositions.Num() == 0
		&& RenderScene->SkyLight
		&& RenderScene->SkyLight->ProcessedTexture
		&& RenderScene->SkyLight->ProcessedTexture->TextureRHI;
}

const FLightSceneInfo* MobileBasePass::GetDirectionalLightInfo(const FScene* Scene, const FPrimitiveSceneProxy* PrimitiveSceneProxy)
{
	const FLightSceneInfo* MobileDirectionalLight = nullptr;
	if (PrimitiveSceneProxy && Scene)
	{
		const int32 LightChannel = GetFirstLightingChannelFromMask(PrimitiveSceneProxy->GetLightingChannelMask());
		MobileDirectionalLight = LightChannel >= 0 ? Scene->MobileDirectionalLights[LightChannel] : nullptr;
	}
	return MobileDirectionalLight;
}

int32 MobileBasePass::CalcNumMovablePointLights(const FMaterial& InMaterial, const FPrimitiveSceneProxy* InPrimitiveSceneProxy)
{
	const FReadOnlyCVARCache& ReadOnlyCVARCache = FReadOnlyCVARCache::Get();
	const bool bIsUnlit = InMaterial.GetShadingModels().IsUnlit();
	int32 OutNumMovablePointLights = (InPrimitiveSceneProxy && !bIsUnlit) ? FMath::Min<int32>(InPrimitiveSceneProxy->GetPrimitiveSceneInfo()->NumMobileMovablePointLights, ReadOnlyCVARCache.NumMobileMovablePointLights) : 0;
	if (OutNumMovablePointLights > 0 && ReadOnlyCVARCache.bMobileMovablePointLightsUseStaticBranch)
	{
		OutNumMovablePointLights = INT32_MAX;
	}
	return OutNumMovablePointLights;
}

bool MobileBasePass::StaticCanReceiveCSM(const FLightSceneInfo* LightSceneInfo, const FPrimitiveSceneProxy* PrimitiveSceneProxy)
{
	// For movable directional lights, when CSM culling is disabled the default behavior is to receive CSM.
	static auto* CVarMobileEnableMovableLightCSMShaderCulling = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.EnableMovableLightCSMShaderCulling"));
	if (LightSceneInfo && LightSceneInfo->Proxy->IsMovable() && CVarMobileEnableMovableLightCSMShaderCulling->GetValueOnRenderThread() == 0)
	{		
		return true;
	}

	// If culling is enabled then CSM receiving is determined during InitDynamicShadows.
	// If culling is disabled then stationary directional lights default to no CSM. 
	return false; 
}

ELightMapPolicyType MobileBasePass::SelectMeshLightmapPolicy(
	const FScene* Scene, 
	const FMeshBatch& Mesh, 
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FLightSceneInfo* MobileDirectionalLight,
	FMaterialShadingModelField ShadingModels, 
	bool bPrimReceivesCSM,
	bool bUsesDeferredShading,
	ERHIFeatureLevel::Type FeatureLevel,
	EBlendMode BlendMode)
{
	// Unlit uses NoLightmapPolicy with 0 point lights
	ELightMapPolicyType SelectedLightmapPolicy = LMP_NO_LIGHTMAP;
	
	const bool bIsLitMaterial = ShadingModels.IsLit();
	if (bIsLitMaterial)
	{
		const FReadOnlyCVARCache& ReadOnlyCVARCache = FReadOnlyCVARCache::Get();

		if (!ReadOnlyCVARCache.bAllowStaticLighting)
		{
			if (!IsTranslucentBlendMode(BlendMode))
			{
				// Whether to use a single CSM permutation with a branch in the shader
				bPrimReceivesCSM |= MobileUseCSMShaderBranch();
			}
			
			// no precomputed lighting
			if (!bPrimReceivesCSM || bUsesDeferredShading)
			{
				SelectedLightmapPolicy = LMP_NO_LIGHTMAP;
			}
			else
			{
				SelectedLightmapPolicy = LMP_MOBILE_DIRECTIONAL_LIGHT_CSM;				
			}
		}
		else
		{
			// Check for a cached light-map.
			const FLightMapInteraction LightMapInteraction = (Mesh.LCI != nullptr)
				? Mesh.LCI->GetLightMapInteraction(FeatureLevel)
				: FLightMapInteraction();
		
			const bool bUseMovableLight = MobileDirectionalLight && !MobileDirectionalLight->Proxy->HasStaticShadowing() && ReadOnlyCVARCache.bMobileAllowMovableDirectionalLights;
			const bool bUseStaticAndCSM = MobileDirectionalLight && MobileDirectionalLight->Proxy->UseCSMForDynamicObjects()
											&& bPrimReceivesCSM
											&& ReadOnlyCVARCache.bMobileEnableStaticAndCSMShadowReceivers;

			const bool bMovableWithCSM = bUseMovableLight && MobileDirectionalLight->ShouldRenderViewIndependentWholeSceneShadows() && bPrimReceivesCSM;

			const bool bPrimitiveUsesILC = PrimitiveSceneProxy
										&& (PrimitiveSceneProxy->IsMovable() || PrimitiveSceneProxy->NeedsUnbuiltPreviewLighting() || PrimitiveSceneProxy->GetLightmapType() == ELightmapType::ForceVolumetric)
										&& PrimitiveSceneProxy->WillEverBeLit()
										&& PrimitiveSceneProxy->GetIndirectLightingCacheQuality() != ILCQ_Off;

			const bool bHasValidVLM = Scene && Scene->VolumetricLightmapSceneData.HasData();

			const bool bHasValidILC = Scene && Scene->PrecomputedLightVolumes.Num() > 0
									&& IsIndirectLightingCacheAllowed(FeatureLevel);

			if (LightMapInteraction.GetType() == LMIT_Texture && ReadOnlyCVARCache.bEnableLowQualityLightmaps)
			{
				// Lightmap path
				if (bUseMovableLight)
				{
					if (bUsesDeferredShading)
					{
						SelectedLightmapPolicy = LMP_LQ_LIGHTMAP;
					}
					else if (bMovableWithCSM && !bUsesDeferredShading)
					{
						SelectedLightmapPolicy = LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM_WITH_LIGHTMAP;
					}
					else
					{
						SelectedLightmapPolicy = LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_WITH_LIGHTMAP;						
					}
				}
				else
				{
					const FShadowMapInteraction ShadowMapInteraction = (Mesh.LCI != nullptr)
						? Mesh.LCI->GetShadowMapInteraction(FeatureLevel)
						: FShadowMapInteraction();

					if (bUseStaticAndCSM && !bUsesDeferredShading)
					{
						if (ShadowMapInteraction.GetType() == SMIT_Texture && 
							MobileDirectionalLight->ShouldRenderViewIndependentWholeSceneShadows() && 
							ReadOnlyCVARCache.bMobileAllowDistanceFieldShadows)
						{
							SelectedLightmapPolicy = LMP_MOBILE_DISTANCE_FIELD_SHADOWS_LIGHTMAP_AND_CSM;
						}
						else
						{
							SelectedLightmapPolicy = LMP_MOBILE_DIRECTIONAL_LIGHT_CSM_AND_LIGHTMAP;
						}
					}
					else
					{
						if (ShadowMapInteraction.GetType() == SMIT_Texture &&
							ReadOnlyCVARCache.bMobileAllowDistanceFieldShadows)
						{
							SelectedLightmapPolicy = LMP_MOBILE_DISTANCE_FIELD_SHADOWS_AND_LQ_LIGHTMAP;
						}
						else
						{
							SelectedLightmapPolicy = LMP_LQ_LIGHTMAP;
						}
					}
				}
			}
			else if ((bHasValidVLM || bHasValidILC) && bPrimitiveUsesILC)
			{
				if ((bUseStaticAndCSM || bMovableWithCSM) && !bUsesDeferredShading)
				{
					SelectedLightmapPolicy = LMP_MOBILE_DIRECTIONAL_LIGHT_CSM_AND_SH_INDIRECT;
				}
				else
				{
					SelectedLightmapPolicy = LMP_MOBILE_DIRECTIONAL_LIGHT_AND_SH_INDIRECT;
				}
			}
		}
	}
		
	return SelectedLightmapPolicy;
}

void MobileBasePass::SetOpaqueRenderState(FMeshPassProcessorRenderState& DrawRenderState, const FPrimitiveSceneProxy* PrimitiveSceneProxy, const FMaterial& Material, bool bEnableReceiveDecalOutput, bool bUsesDeferredShading)
{
	uint8 StencilValue = 0;
	if (bEnableReceiveDecalOutput)
	{
		uint8 ReceiveDecals = (PrimitiveSceneProxy && !PrimitiveSceneProxy->ReceivesDecals() ? 0x01 : 0x00);
		StencilValue|= GET_STENCIL_BIT_MASK(RECEIVE_DECAL, ReceiveDecals);
	}
	
	if (bUsesDeferredShading)
	{
		// store into [1-3] bits
		uint8 ShadingModel = Material.GetShadingModels().IsLit() ? MSM_DefaultLit : MSM_Unlit;
		StencilValue|= GET_STENCIL_MOBILE_SM_MASK(ShadingModel);
	}
		
	if (bEnableReceiveDecalOutput || bUsesDeferredShading)
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<
				true, CF_DepthNearOrEqual,
				true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
				false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
				// don't use masking as it has significant performance hit on Mali GPUs (T860MP2)
				0x00, 0xff >::GetRHI());

		DrawRenderState.SetStencilRef(StencilValue); 
	}
	else
	{
		// default depth state should be already set
	}

	if (Material.GetBlendMode() == BLEND_Masked && Material.IsUsingAlphaToCoverage())
	{
		DrawRenderState.SetBlendState(TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero, 
														CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
														CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero, 
														CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero, 
														CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero, 
														CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
														CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero, 
														CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero, 
														true>::GetRHI());
	}
}

void MobileBasePass::SetTranslucentRenderState(FMeshPassProcessorRenderState& DrawRenderState, const FMaterial& Material)
{
	const bool bIsUsingMobilePixelProjectedReflection = Material.IsUsingPlanarForwardReflections() && IsUsingMobilePixelProjectedReflection(GetFeatureLevelShaderPlatform(Material.GetFeatureLevel()));

	if (Material.GetShadingModels().HasShadingModel(MSM_ThinTranslucent))
	{
		// the mobile thin translucent fallback uses a similar mode as BLEND_Translucent, but multiplies color by 1 insead of SrcAlpha.
		DrawRenderState.SetBlendState(TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI());
	}
	else
	{
		switch (Material.GetBlendMode())
		{
		case BLEND_Translucent:
			if (Material.ShouldWriteOnlyAlpha())
			{
				DrawRenderState.SetBlendState(TStaticBlendState<CW_ALPHA, BO_Add, BF_Zero, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI());
			}
			else
			{
				DrawRenderState.SetBlendState(TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI());
			}
			break;
		case BLEND_Additive:
			// Add to the existing scene color
			DrawRenderState.SetBlendState(TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI());
			break;
		case BLEND_Modulate:
			// Modulate with the existing scene color
			DrawRenderState.SetBlendState(TStaticBlendState<CW_RGB, BO_Add, BF_DestColor, BF_Zero>::GetRHI());
			break;
		case BLEND_AlphaComposite:
			// Blend with existing scene color. New color is already pre-multiplied by alpha.
			DrawRenderState.SetBlendState(TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI());
			break;
		case BLEND_AlphaHoldout:
			// Blend by holding out the matte shape of the source alpha
			DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_Zero, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI());
			break;
		default:
			if (Material.GetShadingModels().HasShadingModel(MSM_SingleLayerWater))
			{
				// Single layer water is an opaque marerial rendered as translucent on Mobile. We force pre-multiplied alpha to achieve water depth based transmittance.
				DrawRenderState.SetBlendState(TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI());
			}
			else if (bIsUsingMobilePixelProjectedReflection)
			{
				if (GetMobilePixelProjectedReflectionQuality() == EMobilePixelProjectedReflectionQuality::BestPerformance)
				{
					// We only render the meshes used for mobile pixel projected reflection once for the BestPerformance quality level, so we have to write the depth to alpha channel in translucent pass.
					DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RGBA>::GetRHI());
				}
				else
				{
					// We render the meshes used for mobile pixel projected reflection twice for the BetterPerformance or greater quality level, so we don't have to write the depth to alpha channel in translucent pass.
					DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RGB>::GetRHI());
				}
			}
			else
			{
				check(0);
			}
		};
	}

	if (Material.ShouldDisableDepthTest())
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
	}
	else if (bIsUsingMobilePixelProjectedReflection && !IsTranslucentBlendMode(Material.GetBlendMode()))
	{
		if (GetMobilePixelProjectedReflectionQuality() == EMobilePixelProjectedReflectionQuality::BestPerformance)
		{
			// We only render the opaque meshes used for mobile pixel projected reflection once for the BestPerformance quality level, so we have to write the depth to depth RT in translucent pass.
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
		}
	}
#if PLATFORM_HOLOLENS
	else if (Material.ShouldWriteDepthToTranslucentMaterial())
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
	}
#endif
}

bool MobileBasePass::StationarySkyLightHasBeenApplied(const FScene* Scene, ELightMapPolicyType LightMapPolicyType)
{
	return Scene
		&& Scene->SkyLight
		&& Scene->SkyLight->bWantsStaticShadowing
		&& (LightMapPolicyType == LMP_LQ_LIGHTMAP
			|| LightMapPolicyType == LMP_MOBILE_DISTANCE_FIELD_SHADOWS_AND_LQ_LIGHTMAP
			|| LightMapPolicyType == LMP_MOBILE_DISTANCE_FIELD_SHADOWS_LIGHTMAP_AND_CSM
			|| LightMapPolicyType == LMP_MOBILE_DIRECTIONAL_LIGHT_CSM_AND_LIGHTMAP
			|| LightMapPolicyType == LMP_MOBILE_DIRECTIONAL_LIGHT_AND_SH_INDIRECT
			|| LightMapPolicyType == LMP_MOBILE_DIRECTIONAL_LIGHT_CSM_AND_SH_INDIRECT
			|| LightMapPolicyType == LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_WITH_LIGHTMAP
			|| LightMapPolicyType == LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM_WITH_LIGHTMAP);
}

static FMeshDrawCommandSortKey GetBasePassStaticSortKey(EBlendMode BlendMode, bool bBackground)
{
	FMeshDrawCommandSortKey SortKey;
	SortKey.PackedData = (BlendMode == EBlendMode::BLEND_Masked ? 1 : 0);
	SortKey.PackedData|= (bBackground ? 2 : 0); // background flag in second bit
	return SortKey;
}

template<>
void TMobileBasePassPSPolicyParamType<FUniformLightMapPolicy>::GetShaderBindings(
	const FScene* Scene,
	ERHIFeatureLevel::Type FeatureLevel,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material,
	const FMeshPassProcessorRenderState& DrawRenderState,
	const TMobileBasePassShaderElementData<FUniformLightMapPolicy>& ShaderElementData,
	FMeshDrawSingleShaderBindings& ShaderBindings) const
{
	FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

	FUniformLightMapPolicy::GetPixelShaderBindings(
		PrimitiveSceneProxy,
		ShaderElementData.LightMapPolicyElementData,
		this,
		ShaderBindings);

	if (Scene)
	{
		// test for HQ reflection parameter existence
		if (HQReflectionCubemaps[0].IsBound() || HQReflectionCubemaps[1].IsBound() || HQReflectionCubemaps[2].IsBound())
		{
			static const int32 MaxNumReflections = FPrimitiveSceneInfo::MaxCachedReflectionCaptureProxies;
			static_assert(MaxNumReflections == 3, "Update reflection array initializations to match MaxCachedReflectionCaptureProxies");
			// set reflection parameters
			FTexture* ReflectionCubemapTextures[MaxNumReflections] = { GBlackTextureCube, GBlackTextureCube, GBlackTextureCube };
			FVector4 CapturePositions[MaxNumReflections] = { FVector4(0, 0, 0, 0), FVector4(0, 0, 0, 0), FVector4(0, 0, 0, 0) };
			FVector4 ReflectionParams(0.0f, 0.0f, 0.0f, 0.0f);
			FVector4 ReflectanceMaxValueRGBMParams(0.0f, 0.0f, 0.0f, 0.0f);
			FMatrix CaptureBoxTransformArray[MaxNumReflections] = { FMatrix(EForceInit::ForceInitToZero), FMatrix(EForceInit::ForceInitToZero), FMatrix(EForceInit::ForceInitToZero) };
			FVector4 CaptureBoxScalesArray[MaxNumReflections] = { FVector4(EForceInit::ForceInitToZero), FVector4(EForceInit::ForceInitToZero), FVector4(EForceInit::ForceInitToZero) };
			FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneProxy ? PrimitiveSceneProxy->GetPrimitiveSceneInfo() : nullptr;
			if (PrimitiveSceneInfo)
			{
				for (int32 i = 0; i < MaxNumReflections; i++)
				{
					const FReflectionCaptureProxy* ReflectionProxy = PrimitiveSceneInfo->CachedReflectionCaptureProxies[i];
					if (ReflectionProxy)
					{
						CapturePositions[i] = ReflectionProxy->Position;
						CapturePositions[i].W = ReflectionProxy->InfluenceRadius;
						if (ReflectionProxy->EncodedHDRCubemap && ReflectionProxy->EncodedHDRCubemap->IsInitialized())
						{
							ReflectionCubemapTextures[i] = PrimitiveSceneInfo->CachedReflectionCaptureProxies[i]->EncodedHDRCubemap;
						}
						//To keep ImageBasedReflectionLighting coherence with PC, use AverageBrightness instead of InvAverageBrightness to calculate the IBL contribution
						ReflectionParams[i] = ReflectionProxy->EncodedHDRAverageBrightness;

						ReflectanceMaxValueRGBMParams[i] = ReflectionProxy->MaxValueRGBM;
						if (ReflectionProxy->Shape == EReflectionCaptureShape::Box)
						{
							CaptureBoxTransformArray[i] = ReflectionProxy->BoxTransform;
							CaptureBoxScalesArray[i] = FVector4(ReflectionProxy->BoxScales, ReflectionProxy->BoxTransitionDistance);
						}
					}
					else if (Scene->SkyLight != nullptr && Scene->SkyLight->ProcessedTexture != nullptr)
					{
						// NegativeInfluence to signal the shader we are defaulting to SkyLight if there are no ReflectionComponents in the Level
						CapturePositions[i].W = -1.0f;
						ReflectionCubemapTextures[i] = Scene->SkyLight->ProcessedTexture;
						ReflectionParams[3] = FMath::FloorLog2(Scene->SkyLight->ProcessedTexture->GetSizeX());
						break;
					}
				}
			}

			for (int32 i = 0; i < MaxNumReflections; i++)
			{
				ShaderBindings.AddTexture(HQReflectionCubemaps[i], HQReflectionSamplers[i], ReflectionCubemapTextures[i]->SamplerStateRHI, ReflectionCubemapTextures[i]->TextureRHI);
			}
			ShaderBindings.Add(HQReflectionInvAverageBrigtnessParams, ReflectionParams);
			ShaderBindings.Add(HQReflectanceMaxValueRGBMParams, ReflectanceMaxValueRGBMParams);
			ShaderBindings.Add(HQReflectionPositionsAndRadii, CapturePositions);
			ShaderBindings.Add(HQReflectionCaptureBoxTransformArray, CaptureBoxTransformArray);
			ShaderBindings.Add(HQReflectionCaptureBoxScalesArray, CaptureBoxScalesArray);
		}
		else if (ReflectionParameter.IsBound())
		{
			FRHIUniformBuffer* ReflectionUB = GDefaultMobileReflectionCaptureUniformBuffer.GetUniformBufferRHI();
			// If no reflection captures are available then attempt to use sky light's texture.
			if (UseSkyReflectionCapture(Scene))
			{
				ReflectionUB = Scene->UniformBuffers.MobileSkyReflectionUniformBuffer;
			}
			else
			{
				FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneProxy ? PrimitiveSceneProxy->GetPrimitiveSceneInfo() : nullptr;
				if (PrimitiveSceneInfo && PrimitiveSceneInfo->CachedReflectionCaptureProxy)
				{
					ReflectionUB = PrimitiveSceneInfo->CachedReflectionCaptureProxy->MobileUniformBuffer;
				}
			}
			ShaderBindings.Add(ReflectionParameter, ReflectionUB);
		}
		
		if (NumDynamicPointLightsParameter.IsBound())
		{
			static FHashedName MobileMovablePointLightHashedName[MAX_BASEPASS_DYNAMIC_POINT_LIGHTS] = { FHashedName(TEXT("MobileMovablePointLight0")), FHashedName(TEXT("MobileMovablePointLight1")), FHashedName(TEXT("MobileMovablePointLight2")), FHashedName(TEXT("MobileMovablePointLight3")) };

			// Set dynamic point lights
			FMobileBasePassMovableLightInfo LightInfo(PrimitiveSceneProxy);
			ShaderBindings.Add(NumDynamicPointLightsParameter, LightInfo.NumMovablePointLights);
			for (int32 i = 0; i < MAX_BASEPASS_DYNAMIC_POINT_LIGHTS; ++i)
			{
				if (i < LightInfo.NumMovablePointLights && LightInfo.MovablePointLightUniformBuffer[i])
				{
					ShaderBindings.Add(GetUniformBufferParameter(MobileMovablePointLightHashedName[i]), LightInfo.MovablePointLightUniformBuffer[i]);
				}
				else
				{
					ShaderBindings.Add(GetUniformBufferParameter(MobileMovablePointLightHashedName[i]), GDummyMovablePointLightUniformBuffer.GetUniformBufferRHI());
				}
			}
		}
	}
	else
	{
		ensure(!ReflectionParameter.IsBound());
	}

	// Set directional light UB
	if (MobileDirectionLightBufferParam.IsBound() && Scene)
	{
		int32 UniformBufferIndex = PrimitiveSceneProxy ? GetFirstLightingChannelFromMask(PrimitiveSceneProxy->GetLightingChannelMask()) + 1 : 0;
		ShaderBindings.Add(MobileDirectionLightBufferParam, Scene->UniformBuffers.MobileDirectionalLightUniformBuffers[UniformBufferIndex]);
	}

	if (CSMDebugHintParams.IsBound())
	{
		static const auto CVarsCSMDebugHint = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.Mobile.Shadow.CSMDebugHint"));
		float CSMDebugValue = CVarsCSMDebugHint->GetValueOnRenderThread();
		ShaderBindings.Add(CSMDebugHintParams, CSMDebugValue);
	}
}

FMobileBasePassMeshProcessor::FMobileBasePassMeshProcessor(
	const FScene* Scene,
	ERHIFeatureLevel::Type InFeatureLevel,
	const FSceneView* InViewIfDynamicMeshCommand,
	const FMeshPassProcessorRenderState& InDrawRenderState,
	FMeshPassDrawListContext* InDrawListContext,
	EFlags InFlags,
	ETranslucencyPass::Type InTranslucencyPassType)
	: FMeshPassProcessor(Scene, InFeatureLevel, InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InDrawRenderState)
	, TranslucencyPassType(InTranslucencyPassType)
	, Flags(InFlags)
	, bTranslucentBasePass(InTranslucencyPassType != ETranslucencyPass::TPT_MAX)
	, bUsesDeferredShading(!bTranslucentBasePass && IsMobileDeferredShadingEnabled(GetFeatureLevelShaderPlatform(InFeatureLevel)))
{
}

bool FMobileBasePassMeshProcessor::TryAddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, const FMaterialRenderProxy& MaterialRenderProxy, const FMaterial& Material)
{
	const EBlendMode BlendMode = Material.GetBlendMode();
	const FMaterialShadingModelField ShadingModels = Material.GetShadingModels();
	const bool bIsTranslucent = IsTranslucentBlendMode(BlendMode);
	const bool bUsesWaterMaterial = ShadingModels.HasShadingModel(MSM_SingleLayerWater); // Water goes into the translucent pass
	const bool bIsUsingMobilePixelProjectedReflection = Material.IsUsingPlanarForwardReflections() 
														&& IsUsingMobilePixelProjectedReflection(GetFeatureLevelShaderPlatform(Material.GetFeatureLevel()));
	const bool bCanReceiveCSM = ((Flags & EFlags::CanReceiveCSM) == EFlags::CanReceiveCSM);

	bool bResult = true;
	if (bTranslucentBasePass)
	{
		// Skipping TPT_TranslucencyAfterDOFModulate. That pass is only needed for Dual Blending, which is not supported on Mobile.
		bool bShouldDraw = (bIsTranslucent || bUsesWaterMaterial || bIsUsingMobilePixelProjectedReflection) &&
		(TranslucencyPassType == ETranslucencyPass::TPT_AllTranslucency
		|| (TranslucencyPassType == ETranslucencyPass::TPT_StandardTranslucency && !Material.IsMobileSeparateTranslucencyEnabled())
		|| (TranslucencyPassType == ETranslucencyPass::TPT_TranslucencyAfterDOF && Material.IsMobileSeparateTranslucencyEnabled()));

		if (bShouldDraw)
		{
			check(bCanReceiveCSM == false);
			const FLightSceneInfo* MobileDirectionalLight = MobileBasePass::GetDirectionalLightInfo(Scene, PrimitiveSceneProxy);
			// Opaque meshes used for mobile pixel projected reflection could receive CSM in translucent pass.
			ELightMapPolicyType LightmapPolicyType = MobileBasePass::SelectMeshLightmapPolicy(Scene, MeshBatch, PrimitiveSceneProxy, MobileDirectionalLight, ShadingModels, bCanReceiveCSM || (!bIsTranslucent && bIsUsingMobilePixelProjectedReflection), false, FeatureLevel, BlendMode);
			bResult = Process(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, MaterialRenderProxy, Material, BlendMode, ShadingModels, LightmapPolicyType, MeshBatch.LCI);
		}
	}
	else
	{
		// opaque materials.
		// We have to render the opaque meshes used for mobile pixel projected reflection both in opaque and translucent pass if the quality level is greater than BestPerformance
		if (!bIsTranslucent && !bUsesWaterMaterial && (!bIsUsingMobilePixelProjectedReflection || GetMobilePixelProjectedReflectionQuality() > EMobilePixelProjectedReflectionQuality::BestPerformance))
		{
			const FLightSceneInfo* MobileDirectionalLight = MobileBasePass::GetDirectionalLightInfo(Scene, PrimitiveSceneProxy);
			ELightMapPolicyType LightmapPolicyType = MobileBasePass::SelectMeshLightmapPolicy(Scene, MeshBatch, PrimitiveSceneProxy, MobileDirectionalLight, ShadingModels, bCanReceiveCSM, bUsesDeferredShading, FeatureLevel, BlendMode);
			bResult = Process(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, MaterialRenderProxy, Material, BlendMode, ShadingModels, LightmapPolicyType, MeshBatch.LCI);
		}
	}

	return bResult;
}

void FMobileBasePassMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (!MeshBatch.bUseForMaterial || (PrimitiveSceneProxy && !PrimitiveSceneProxy->ShouldRenderInMainPass()))
	{
		return;
	}
	
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

bool FMobileBasePassMeshProcessor::Process(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		EBlendMode BlendMode,
		FMaterialShadingModelField ShadingModels,
		const ELightMapPolicyType LightMapPolicyType,
		const FUniformLightMapPolicy::ElementDataType& RESTRICT LightMapElementData)
{
	TMeshProcessorShaders<
		TMobileBasePassVSPolicyParamType<FUniformLightMapPolicy>,
		FBaseHS,
		FBaseDS,
		TMobileBasePassPSPolicyParamType<FUniformLightMapPolicy>> BasePassShaders;
	
	bool bEnableSkyLight = false;
	
	if (Scene && Scene->SkyLight)
	{
		//The stationary skylight contribution has been added both to the LowQuality Lightmap and ILC on mobile, so we should skip the sky light spherical harmonic contribution for it.
		bool bStationarySkyLightHasBeenApplied = MobileBasePass::StationarySkyLightHasBeenApplied(Scene, LightMapPolicyType);

		//Two side material should enable sky light for the back face since only the front face has light map and it will be corrected in base pass shader.
		bool bSkipStationarySkyLight = bStationarySkyLightHasBeenApplied && !MaterialResource.IsTwoSided();

		bEnableSkyLight = ShadingModels.IsLit() && Scene->ShouldRenderSkylightInBasePass(BlendMode) && (!bSkipStationarySkyLight);
	}

	int32 NumMovablePointLights = 0;
	if (!bUsesDeferredShading)
	{
		NumMovablePointLights = MobileBasePass::CalcNumMovablePointLights(MaterialResource, PrimitiveSceneProxy);
	}

	if (!MobileBasePass::GetShaders(
		LightMapPolicyType,
		NumMovablePointLights,
		MaterialResource,
		MeshBatch.VertexFactory->GetType(),
		bEnableSkyLight,
		BasePassShaders.VertexShader,
		BasePassShaders.PixelShader))
	{
		return false;
	}

	const bool bMaskedInEarlyPass = (MaterialResource.IsMasked() || MeshBatch.bDitheredLODTransition) && Scene && MaskedInEarlyPass(Scene->GetShaderPlatform());
	const bool bForcePassDrawRenderState = ((Flags & EFlags::ForcePassDrawRenderState) == EFlags::ForcePassDrawRenderState);

	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);
	if (!bForcePassDrawRenderState)
	{
		if (bTranslucentBasePass)
		{
			MobileBasePass::SetTranslucentRenderState(DrawRenderState, MaterialResource);
		}
		else if((MeshBatch.bUseForDepthPass && Scene->EarlyZPassMode == DDM_AllOpaque) || bMaskedInEarlyPass)
		{
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Equal>::GetRHI());
		}
		else
		{
			const bool bEnableReceiveDecalOutput = ((Flags & EFlags::CanUseDepthStencil) == EFlags::CanUseDepthStencil);
			MobileBasePass::SetOpaqueRenderState(DrawRenderState, PrimitiveSceneProxy, MaterialResource, bEnableReceiveDecalOutput && IsMobileHDR(), bUsesDeferredShading);
		}
	}

	FMeshDrawCommandSortKey SortKey; 
	if (bTranslucentBasePass)
	{
		const bool bIsUsingMobilePixelProjectedReflection = MaterialResource.IsUsingPlanarForwardReflections() 
															&& IsUsingMobilePixelProjectedReflection(GetFeatureLevelShaderPlatform(MaterialResource.GetFeatureLevel()));

		SortKey = CalculateTranslucentMeshStaticSortKey(PrimitiveSceneProxy, MeshBatch.MeshIdInPrimitive);
		// We always want water to be rendered first on mobile in order to mimic other renderers where it is opaque. We shift the other priorities by 1.
		// And we also want to render the meshes used for mobile pixel projected reflection first if it is opaque.
		const bool bIsTranslucent = IsTranslucentBlendMode(BlendMode);
		SortKey.Translucent.Priority = ShadingModels.HasShadingModel(MSM_SingleLayerWater) || (!bIsTranslucent && bIsUsingMobilePixelProjectedReflection) ? uint16(0) : uint16(FMath::Clamp(uint32(SortKey.Translucent.Priority) + 1, 0u, uint32(USHRT_MAX)));
	}
	else
	{
		// Background primitives will be rendered last in masked/non-masked buckets
		bool bBackground = PrimitiveSceneProxy ? PrimitiveSceneProxy->TreatAsBackgroundForOcclusion() : false;
		// Default static sort key separates masked and non-masked geometry, generic mesh sorting will also sort by PSO
		// if platform wants front to back sorting, this key will be recomputed in InitViews
		SortKey = GetBasePassStaticSortKey(BlendMode, bBackground);
	}
	
	const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
	ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, MaterialResource, OverrideSettings);
	ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, MaterialResource, OverrideSettings);

	TMobileBasePassShaderElementData<FUniformLightMapPolicy> ShaderElementData(LightMapElementData);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

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

FMeshPassProcessor* CreateMobileBasePassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer, Scene->UniformBuffers.MobileOpaqueBasePassUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	PassDrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RGBA>::GetRHI());
	PassDrawRenderState.SetDepthStencilAccess(Scene->DefaultBasePassDepthStencilAccess);
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());

	const FMobileBasePassMeshProcessor::EFlags Flags = FMobileBasePassMeshProcessor::EFlags::CanUseDepthStencil;

	return new(FMemStack::Get()) FMobileBasePassMeshProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, PassDrawRenderState, InDrawListContext, Flags);
}

FMeshPassProcessor* CreateMobileBasePassCSMProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer, Scene->UniformBuffers.MobileCSMOpaqueBasePassUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	PassDrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RGBA>::GetRHI());
	PassDrawRenderState.SetDepthStencilAccess(Scene->DefaultBasePassDepthStencilAccess);
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());

	const FMobileBasePassMeshProcessor::EFlags Flags = FMobileBasePassMeshProcessor::EFlags::CanReceiveCSM | FMobileBasePassMeshProcessor::EFlags::CanUseDepthStencil;

	return new(FMemStack::Get()) FMobileBasePassMeshProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, PassDrawRenderState, InDrawListContext, Flags);
}

FMeshPassProcessor* CreateMobileTranslucencyStandardPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer, Scene->UniformBuffers.MobileTranslucentBasePassUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
	PassDrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthRead_StencilRead);
	

	const FMobileBasePassMeshProcessor::EFlags Flags = FMobileBasePassMeshProcessor::EFlags::CanUseDepthStencil;

	return new(FMemStack::Get()) FMobileBasePassMeshProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, PassDrawRenderState, InDrawListContext, Flags, ETranslucencyPass::TPT_StandardTranslucency);
}

FMeshPassProcessor* CreateMobileTranslucencyAfterDOFProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer, Scene->UniformBuffers.MobileTranslucentBasePassUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
	PassDrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthRead_StencilRead);

	const FMobileBasePassMeshProcessor::EFlags Flags = FMobileBasePassMeshProcessor::EFlags::CanUseDepthStencil;

	return new(FMemStack::Get()) FMobileBasePassMeshProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, PassDrawRenderState, InDrawListContext, Flags, ETranslucencyPass::TPT_TranslucencyAfterDOF);
}

FMeshPassProcessor* CreateMobileTranslucencyAllPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer, Scene->UniformBuffers.MobileTranslucentBasePassUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
	PassDrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthRead_StencilRead);

	const FMobileBasePassMeshProcessor::EFlags Flags = FMobileBasePassMeshProcessor::EFlags::CanUseDepthStencil;

	return new(FMemStack::Get()) FMobileBasePassMeshProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, PassDrawRenderState, InDrawListContext, Flags, ETranslucencyPass::TPT_AllTranslucency);
}

FRegisterPassProcessorCreateFunction RegisterMobileBasePass(&CreateMobileBasePassProcessor, EShadingPath::Mobile, EMeshPass::BasePass, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterMobileBasePassCSM(&CreateMobileBasePassCSMProcessor, EShadingPath::Mobile, EMeshPass::MobileBasePassCSM, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterMobileTranslucencyAllPass(&CreateMobileTranslucencyAllPassProcessor, EShadingPath::Mobile, EMeshPass::TranslucencyAll, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterMobileTranslucencyStandardPass(&CreateMobileTranslucencyStandardPassProcessor, EShadingPath::Mobile, EMeshPass::TranslucencyStandard, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterMobileTranslucencyAfterDOFPass(&CreateMobileTranslucencyAfterDOFProcessor, EShadingPath::Mobile, EMeshPass::TranslucencyAfterDOF, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);
// Skipping EMeshPass::TranslucencyAfterDOFModulate because dual blending is not supported on mobile
