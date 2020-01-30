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
void GetUniformMobileBasePassShaders(
	const FMaterial& Material, 
	FVertexFactoryType* VertexFactoryType, 
	bool bEnableSkyLight,
	TMobileBasePassVSPolicyParamType<FUniformLightMapPolicy>*& VertexShader,
	TMobileBasePassPSPolicyParamType<FUniformLightMapPolicy>*& PixelShader
	)
{
	if (IsMobileHDR())
	{
		VertexShader = (TMobileBasePassVSPolicyParamType<FUniformLightMapPolicy>*)Material.GetShader<TMobileBasePassVS<TUniformLightMapPolicy<Policy>, HDR_LINEAR_64> >(VertexFactoryType);

		if (bEnableSkyLight)
		{
			PixelShader = (TMobileBasePassPSPolicyParamType<FUniformLightMapPolicy>*)Material.GetShader< TMobileBasePassPS<TUniformLightMapPolicy<Policy>, HDR_LINEAR_64, true, NumMovablePointLights> >(VertexFactoryType);
		}
		else
		{
			PixelShader = (TMobileBasePassPSPolicyParamType<FUniformLightMapPolicy>*)Material.GetShader< TMobileBasePassPS<TUniformLightMapPolicy<Policy>, HDR_LINEAR_64, false, NumMovablePointLights> >(VertexFactoryType);
		}	
	}
	else
	{
		VertexShader = (TMobileBasePassVSPolicyParamType<FUniformLightMapPolicy>*)Material.GetShader<TMobileBasePassVS<TUniformLightMapPolicy<Policy>, LDR_GAMMA_32> >(VertexFactoryType);

		if (bEnableSkyLight)
		{
			PixelShader = (TMobileBasePassPSPolicyParamType<FUniformLightMapPolicy>*)Material.GetShader< TMobileBasePassPS<TUniformLightMapPolicy<Policy>, LDR_GAMMA_32, true, NumMovablePointLights> >(VertexFactoryType);
		}
		else
		{
			PixelShader = (TMobileBasePassPSPolicyParamType<FUniformLightMapPolicy>*)Material.GetShader< TMobileBasePassPS<TUniformLightMapPolicy<Policy>, LDR_GAMMA_32, false, NumMovablePointLights> >(VertexFactoryType);
		}			
	}
}

template <int32 NumMovablePointLights>
void GetMobileBasePassShaders(
	ELightMapPolicyType LightMapPolicyType, 
	const FMaterial& Material, 
	FVertexFactoryType* VertexFactoryType, 
	bool bEnableSkyLight,
	TMobileBasePassVSPolicyParamType<FUniformLightMapPolicy>*& VertexShader,
	TMobileBasePassPSPolicyParamType<FUniformLightMapPolicy>*& PixelShader
	)
{
	switch (LightMapPolicyType)
	{
	case LMP_LQ_LIGHTMAP:
		GetUniformMobileBasePassShaders<LMP_LQ_LIGHTMAP, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
		break;
	case LMP_MOBILE_DISTANCE_FIELD_SHADOWS_AND_LQ_LIGHTMAP:
		GetUniformMobileBasePassShaders<LMP_MOBILE_DISTANCE_FIELD_SHADOWS_AND_LQ_LIGHTMAP, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
		break;
	case LMP_MOBILE_DISTANCE_FIELD_SHADOWS_LIGHTMAP_AND_CSM:
		GetUniformMobileBasePassShaders<LMP_MOBILE_DISTANCE_FIELD_SHADOWS_LIGHTMAP_AND_CSM, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
		break;
	case LMP_MOBILE_DIRECTIONAL_LIGHT_AND_SH_INDIRECT:
		GetUniformMobileBasePassShaders<LMP_MOBILE_DIRECTIONAL_LIGHT_AND_SH_INDIRECT, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
		break;
	case LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_AND_SH_INDIRECT:
		GetUniformMobileBasePassShaders<LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_AND_SH_INDIRECT, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
		break;
	case LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM_AND_SH_INDIRECT:
		GetUniformMobileBasePassShaders<LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM_AND_SH_INDIRECT, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
		break;
	case LMP_MOBILE_DIRECTIONAL_LIGHT_CSM_AND_SH_INDIRECT:
		GetUniformMobileBasePassShaders<LMP_MOBILE_DIRECTIONAL_LIGHT_CSM_AND_SH_INDIRECT, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
		break;
	case LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM:
		GetUniformMobileBasePassShaders<LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
		break;
	case LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_WITH_LIGHTMAP:
		GetUniformMobileBasePassShaders<LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_WITH_LIGHTMAP, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
		break;
	case LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM_WITH_LIGHTMAP:
		GetUniformMobileBasePassShaders<LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM_WITH_LIGHTMAP, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
		break;
	case LMP_NO_LIGHTMAP:
		GetUniformMobileBasePassShaders<LMP_NO_LIGHTMAP, NumMovablePointLights>(Material, VertexFactoryType, bEnableSkyLight, VertexShader, PixelShader);
		break;
	default:										
		check(false);
	}
}

void MobileBasePass::GetShaders(
	ELightMapPolicyType LightMapPolicyType,
	int32 NumMovablePointLights, 
	const FMaterial& MaterialResource,
	FVertexFactoryType* VertexFactoryType,
	bool bEnableSkyLight, 
	TMobileBasePassVSPolicyParamType<FUniformLightMapPolicy>*& VertexShader,
	TMobileBasePassPSPolicyParamType<FUniformLightMapPolicy>*& PixelShader)
{
	bool bIsLit = (MaterialResource.GetShadingModels().IsLit());
	if (bIsLit && !UseSkylightPermutation(bEnableSkyLight, FReadOnlyCVARCache::Get().MobileSkyLightPermutation))	
	{
		bEnableSkyLight = !bEnableSkyLight;
	}
		
	switch (NumMovablePointLights)
	{
	case INT32_MAX:
		GetMobileBasePassShaders<INT32_MAX>(
			LightMapPolicyType,
			MaterialResource,
			VertexFactoryType,
			bEnableSkyLight,
			VertexShader,
			PixelShader
			);
		break;
	case 1:
		GetMobileBasePassShaders<1>(
			LightMapPolicyType, 
			MaterialResource, 
			VertexFactoryType, 
			bEnableSkyLight, 
			VertexShader,
			PixelShader
			);
		break;
	case 2:
		GetMobileBasePassShaders<2>(
			LightMapPolicyType,
			MaterialResource,
			VertexFactoryType,
			bEnableSkyLight,
			VertexShader,
			PixelShader
			);
		break;
	case 3:
		GetMobileBasePassShaders<3>(
			LightMapPolicyType,
			MaterialResource,
			VertexFactoryType,
			bEnableSkyLight,
			VertexShader,
			PixelShader
			);
		break;
	case 4:
		GetMobileBasePassShaders<4>(
			LightMapPolicyType,
			MaterialResource,
			VertexFactoryType,
			bEnableSkyLight,
			VertexShader,
			PixelShader
			);
		break;
	case 0:
	default:
		GetMobileBasePassShaders<0>(
			LightMapPolicyType,
			MaterialResource,
			VertexFactoryType,
			bEnableSkyLight,
			VertexShader,
			PixelShader
			);
		break;
	}
}

static bool UseSkyReflectionCapture(const FScene* RenderScene)
{
	return RenderScene
		&& RenderScene->ReflectionSceneData.RegisteredReflectionCapturePositions.Num() == 0
		&& RenderScene->SkyLight
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
	ERHIFeatureLevel::Type FeatureLevel)
{
	// Unlit uses NoLightmapPolicy with 0 point lights
	ELightMapPolicyType SelectedLightmapPolicy = LMP_NO_LIGHTMAP;

	// Check for a cached light-map.
	const bool bIsLitMaterial = ShadingModels.IsLit();
	if (bIsLitMaterial)
	{
		const FLightMapInteraction LightMapInteraction = (Mesh.LCI && bIsLitMaterial)
			? Mesh.LCI->GetLightMapInteraction(FeatureLevel)
			: FLightMapInteraction();

		const FReadOnlyCVARCache& ReadOnlyCVARCache = FReadOnlyCVARCache::Get();
		const bool bUseMovableLight = MobileDirectionalLight && !MobileDirectionalLight->Proxy->HasStaticShadowing() && ReadOnlyCVARCache.bMobileAllowMovableDirectionalLights;
		const bool bUseStaticAndCSM = MobileDirectionalLight && MobileDirectionalLight->Proxy->UseCSMForDynamicObjects()
			&& bPrimReceivesCSM
			&& ReadOnlyCVARCache.bMobileEnableStaticAndCSMShadowReceivers;

		const bool bMovableWithCSM = bUseMovableLight && MobileDirectionalLight->ShouldRenderViewIndependentWholeSceneShadows() && bPrimReceivesCSM;

		const bool bPrimitiveUsesILC = PrimitiveSceneProxy
									&& (PrimitiveSceneProxy->IsMovable() || PrimitiveSceneProxy->NeedsUnbuiltPreviewLighting() || PrimitiveSceneProxy->GetLightmapType() == ELightmapType::ForceVolumetric)
									&& PrimitiveSceneProxy->WillEverBeLit()
									&& PrimitiveSceneProxy->GetIndirectLightingCacheQuality() != ILCQ_Off;

		const bool bHasValidVLM = Scene && Scene->VolumetricLightmapSceneData.HasData()
								&& ReadOnlyCVARCache.bAllowStaticLighting;

		const bool bHasValidILC = Scene && Scene->PrecomputedLightVolumes.Num() > 0
								&& IsIndirectLightingCacheAllowed(FeatureLevel);

		if (LightMapInteraction.GetType() == LMIT_Texture && ReadOnlyCVARCache.bAllowStaticLighting && ReadOnlyCVARCache.bEnableLowQualityLightmaps)
		{
			// Lightmap path
			const FShadowMapInteraction ShadowMapInteraction = (Mesh.LCI && bIsLitMaterial)
				? Mesh.LCI->GetShadowMapInteraction(FeatureLevel)
				: FShadowMapInteraction();

			if (bUseMovableLight)
			{
				// final determination of whether CSMs are rendered can be view dependent, thus we always need to clear the CSMs even if we're not going to render to them based on the condition below.
				if (MobileDirectionalLight->ShouldRenderViewIndependentWholeSceneShadows() && bMovableWithCSM)
				{
					SelectedLightmapPolicy = LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM_WITH_LIGHTMAP;
				}
				else
				{
					SelectedLightmapPolicy = LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_WITH_LIGHTMAP;
				}
			}
			else if (bUseStaticAndCSM)
			{
				if (ShadowMapInteraction.GetType() == SMIT_Texture && MobileDirectionalLight->ShouldRenderViewIndependentWholeSceneShadows() && ReadOnlyCVARCache.bMobileAllowDistanceFieldShadows)
				{
					SelectedLightmapPolicy = LMP_MOBILE_DISTANCE_FIELD_SHADOWS_LIGHTMAP_AND_CSM;
				}
				else
				{
					SelectedLightmapPolicy = LMP_LQ_LIGHTMAP;
				}
			}
			else
			{
				if (ShadowMapInteraction.GetType() == SMIT_Texture && ReadOnlyCVARCache.bMobileAllowDistanceFieldShadows)
				{
					SelectedLightmapPolicy = LMP_MOBILE_DISTANCE_FIELD_SHADOWS_AND_LQ_LIGHTMAP;
				}
				else
				{
					SelectedLightmapPolicy = LMP_LQ_LIGHTMAP;
				}
			}
		}
		else if ((bHasValidVLM || bHasValidILC) && bPrimitiveUsesILC)
		{
			if (bUseMovableLight)
			{
				if (MobileDirectionalLight && MobileDirectionalLight->ShouldRenderViewIndependentWholeSceneShadows() && bMovableWithCSM)
				{
					SelectedLightmapPolicy = LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM_AND_SH_INDIRECT;
				}
				else
				{
					SelectedLightmapPolicy = LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_AND_SH_INDIRECT;
				}
			}
			else
			{
				if (bUseStaticAndCSM)
				{
					SelectedLightmapPolicy = LMP_MOBILE_DIRECTIONAL_LIGHT_CSM_AND_SH_INDIRECT;
				}
				else
				{
					SelectedLightmapPolicy = LMP_MOBILE_DIRECTIONAL_LIGHT_AND_SH_INDIRECT;
				}
			}
		}
		else if (bMovableWithCSM || bUseStaticAndCSM)
		{
			// final determination of whether CSMs are rendered can be view dependent, thus we always need to clear the CSMs even if we're not going to render to them based on the condition below.
			SelectedLightmapPolicy = LMP_MOBILE_MOVABLE_DIRECTIONAL_LIGHT_CSM;
		}
	}
		
	return SelectedLightmapPolicy;
}

void MobileBasePass::SetOpaqueRenderState(FMeshPassProcessorRenderState& DrawRenderState, const FPrimitiveSceneProxy* PrimitiveSceneProxy, const FMaterial& Material, bool bEnableReceiveDecalOutput)
{
	bool bEncodedHDR = GetMobileHDRMode() == EMobileHDRMode::EnabledRGBE && Material.GetMaterialDomain() != MD_UI;
	if (bEncodedHDR)
	{
		DrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
	}
		
	if (bEnableReceiveDecalOutput)
	{
		const uint8 StencilValue = (PrimitiveSceneProxy && !PrimitiveSceneProxy->ReceivesDecals() ? 0x01 : 0x00);
		
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<
				true, CF_DepthNearOrEqual,
				true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
				false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
				// decals atm are singe user of stencil in mobile base pass
				// don't use masking as it has significant performance hit on Mali GPUs (T860MP2)
				0x00, 0xff /*GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1)*/ >::GetRHI());

		DrawRenderState.SetStencilRef(GET_STENCIL_BIT_MASK(RECEIVE_DECAL, StencilValue)); // we hash the stencil group because we only have 6 bits.
	}
	else
	{
		// default depth state should be already set
	}
}

void MobileBasePass::SetTranslucentRenderState(FMeshPassProcessorRenderState& DrawRenderState, const FMaterial& Material)
{
	bool bEncodedHDR = GetMobileHDRMode() == EMobileHDRMode::EnabledRGBE && Material.GetMaterialDomain() != MD_UI;

	if (bEncodedHDR == false)
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
			else
			check(0);
		};
	}
	else
	{
		DrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
	}

	if (Material.ShouldDisableDepthTest())
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
	}
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
					}
				}
			}
			for (int32 i = 0; i < MaxNumReflections; i++)
			{
				ShaderBindings.AddTexture(HQReflectionCubemaps[i], HQReflectionSamplers[i], ReflectionCubemapTextures[i]->SamplerStateRHI, ReflectionCubemapTextures[i]->TextureRHI);
			}
			ShaderBindings.Add(HQReflectionInvAverageBrigtnessParams, ReflectionParams);
			ShaderBindings.Add(HQReflectionPositionsAndRadii, CapturePositions);
		}
		else if (ReflectionParameter.IsBound())
		{
			FRHIUniformBuffer* ReflectionUB = GDefaultMobileReflectionCaptureUniformBuffer.GetUniformBufferRHI();
			// If no reflection captures are available then attempt to use sky light's texture.
			if (UseSkyReflectionCapture(Scene) && FeatureLevel > ERHIFeatureLevel::ES2) // not-supported on ES2 at the moment
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
		
		if (LightPositionAndInvRadiusParameter.IsBound() || SpotLightDirectionAndSpecularScaleParameter.IsBound())
		{
			// Set dynamic point lights
			FMobileBasePassMovableLightInfo LightInfo(PrimitiveSceneProxy);
			ShaderBindings.Add(NumDynamicPointLightsParameter, LightInfo.NumMovablePointLights);
			ShaderBindings.Add(LightPositionAndInvRadiusParameter, LightInfo.LightPositionAndInvRadius);
			ShaderBindings.Add(LightColorAndFalloffExponentParameter, LightInfo.LightColorAndFalloffExponent);
			ShaderBindings.Add(SpotLightDirectionAndSpecularScaleParameter, LightInfo.SpotLightDirectionAndSpecularScale);
			ShaderBindings.Add(SpotLightAnglesParameter, LightInfo.SpotLightAngles);
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
	EFlags Flags,
	ETranslucencyPass::Type InTranslucencyPassType)
	: FMeshPassProcessor(Scene, InFeatureLevel, InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InDrawRenderState)
	, TranslucencyPassType(InTranslucencyPassType)
	, bTranslucentBasePass(InTranslucencyPassType != ETranslucencyPass::TPT_MAX)
	, bCanReceiveCSM((Flags & EFlags::CanReceiveCSM) == EFlags::CanReceiveCSM)
	, bEnableReceiveDecalOutput((Flags & EFlags::CanUseDepthStencil) == EFlags::CanUseDepthStencil)
{}

void FMobileBasePassMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (!MeshBatch.bUseForMaterial)
	{
		return;
	}
	
	// Determine the mesh's material and blend mode.
	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);
	const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
	const EBlendMode BlendMode = Material.GetBlendMode();
	const FMaterialShadingModelField ShadingModels = Material.GetShadingModels();
	const bool bIsTranslucent = IsTranslucentBlendMode(BlendMode);
	const bool bUsesWaterMaterial = ShadingModels.HasShadingModel(MSM_SingleLayerWater); // Water goes into the translucent pass
	
	if (bTranslucentBasePass)
	{
		bool bShouldDraw = (bIsTranslucent || bUsesWaterMaterial) &&
		(TranslucencyPassType == ETranslucencyPass::TPT_AllTranslucency
		|| (TranslucencyPassType == ETranslucencyPass::TPT_StandardTranslucency && !Material.IsMobileSeparateTranslucencyEnabled())
		|| (TranslucencyPassType == ETranslucencyPass::TPT_TranslucencyAfterDOF && Material.IsMobileSeparateTranslucencyEnabled()));
							
		if (bShouldDraw)
		{
			check(bCanReceiveCSM == false);
			const FLightSceneInfo* MobileDirectionalLight = MobileBasePass::GetDirectionalLightInfo(Scene, PrimitiveSceneProxy);
			ELightMapPolicyType LightmapPolicyType = MobileBasePass::SelectMeshLightmapPolicy(Scene, MeshBatch, PrimitiveSceneProxy, MobileDirectionalLight, ShadingModels, bCanReceiveCSM, FeatureLevel);
			Process(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, MaterialRenderProxy, Material, BlendMode, ShadingModels, LightmapPolicyType, MeshBatch.LCI);
		}
	}
	else
	{
		// opaque materials.
		if (!bIsTranslucent && !bUsesWaterMaterial)
		{
			const FLightSceneInfo* MobileDirectionalLight = MobileBasePass::GetDirectionalLightInfo(Scene, PrimitiveSceneProxy);
			ELightMapPolicyType LightmapPolicyType = MobileBasePass::SelectMeshLightmapPolicy(Scene, MeshBatch, PrimitiveSceneProxy, MobileDirectionalLight, ShadingModels, bCanReceiveCSM, FeatureLevel);
			Process(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, MaterialRenderProxy, Material, BlendMode, ShadingModels, LightmapPolicyType, MeshBatch.LCI);
		}
	}
}

void FMobileBasePassMeshProcessor::Process(
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
	
	//The stationary skylight contribution has been added to the LowQuality Lightmap for StaticMeshActor on mobile, so we should skip the sky light spherical harmonic contribution for it.
	//Enable skylight if LowQualityLightmaps is disabled or the Lightmap has not been built.
	bool bEnableSkyLight = ShadingModels.IsLit() && Scene && Scene->ShouldRenderSkylightInBasePass(BlendMode) && !(FReadOnlyCVARCache::Get().bAllowStaticLighting && FReadOnlyCVARCache::Get().bEnableLowQualityLightmaps && PrimitiveSceneProxy && PrimitiveSceneProxy->IsStatic() && MeshBatch.LCI && MeshBatch.LCI->GetLightMapInteraction(FeatureLevel).GetType() == LMIT_Texture && !MaterialResource.IsTwoSided());
	int32 NumMovablePointLights = MobileBasePass::CalcNumMovablePointLights(MaterialResource, PrimitiveSceneProxy);

	MobileBasePass::GetShaders(
		LightMapPolicyType, 
		NumMovablePointLights, 
		MaterialResource, 
		MeshBatch.VertexFactory->GetType(), 
		bEnableSkyLight, 
		BasePassShaders.VertexShader, 
		BasePassShaders.PixelShader);

	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);
	if (bTranslucentBasePass)
	{
		MobileBasePass::SetTranslucentRenderState(DrawRenderState, MaterialResource);
	}
	else
	{
		MobileBasePass::SetOpaqueRenderState(DrawRenderState, PrimitiveSceneProxy, MaterialResource, bEnableReceiveDecalOutput && IsMobileHDR());
	}

	FMeshDrawCommandSortKey SortKey; 
	if (bTranslucentBasePass)
	{
		SortKey = CalculateTranslucentMeshStaticSortKey(PrimitiveSceneProxy, MeshBatch.MeshIdInPrimitive);
		// We always want water to be rendered first on mobile in order to mimic other renderers where it is opaque. We shift the other priorities by 1.
		SortKey.Translucent.Priority = ShadingModels.HasShadingModel(MSM_SingleLayerWater) ? uint16(0) : uint16(FMath::Clamp(uint32(SortKey.Translucent.Priority) + 1, 0u, uint32(USHRT_MAX)));
	}
	else
	{
		// Background primitives will be rendered last in masked/non-masked buckets
		bool bBackground = PrimitiveSceneProxy ? PrimitiveSceneProxy->TreatAsBackgroundForOcclusion() : false;
		// Default static sort key separates masked and non-masked geometry, generic mesh sorting will also sort by PSO
		// if platform wants front to back sorting, this key will be recomputed in InitViews
		SortKey = GetBasePassStaticSortKey(BlendMode, bBackground);
	}
	
	ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, MaterialResource);
	ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, MaterialResource);

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
	FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer, Scene->UniformBuffers.MobileOpaqueBasePassUniformBuffer);
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

