// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MaterialInterface.cpp: UMaterialInterface implementation.
=============================================================================*/

#include "Materials/MaterialInterface.h"
#include "RenderingThread.h"
#include "PrimitiveViewRelevance.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/AssetUserData.h"
#include "Engine/Texture2D.h"
#include "Engine/SubsurfaceProfile.h"
#include "Engine/TextureStreamingTypes.h"
#include "Algo/BinarySearch.h"
#include "Interfaces/ITargetPlatform.h"
#include "Components.h"
#include "ContentStreaming.h"
#include "MeshBatch.h"

/**
 * This is used to deprecate data that has been built with older versions.
 * To regenerate the data, commands like "BUILDMATERIALTEXTURESTREAMINGDATA" can be used in the editor.
 * Ideally the data would be stored the DDC instead of the asset, but this is not yet  possible because it requires the GPU.
 */
#define MATERIAL_TEXTURE_STREAMING_DATA_VERSION 1

//////////////////////////////////////////////////////////////////////////

UEnum* UMaterialInterface::SamplerTypeEnum = nullptr;

//////////////////////////////////////////////////////////////////////////

bool IsHairStrandsGeometrySupported(const EShaderPlatform Platform)
{
	check(Platform != SP_NumPlatforms);

	return FDataDrivenShaderPlatformInfo::GetSupportsHairStrandGeometry(Platform)
		&& GetMaxSupportedFeatureLevel(Platform) == ERHIFeatureLevel::SM5;
}

bool IsCompatibleWithHairStrands(const FMaterial* Material, const ERHIFeatureLevel::Type FeatureLevel)
{
	return
		ERHIFeatureLevel::SM5 == FeatureLevel &&
		Material && Material->IsUsedWithHairStrands() && 
		(Material->GetBlendMode() == BLEND_Opaque || Material->GetBlendMode() == BLEND_Masked);
}

bool IsCompatibleWithHairStrands(EShaderPlatform Platform, const FMaterialShaderParameters& Parameters)
{
	return
		IsHairStrandsGeometrySupported(Platform) &&
		Parameters.bIsUsedWithHairStrands &&
		(Parameters.BlendMode == BLEND_Opaque || Parameters.BlendMode == BLEND_Masked);
}

//////////////////////////////////////////////////////////////////////////

/** Copies the material's relevance flags to a primitive's view relevance flags. */
void FMaterialRelevance::SetPrimitiveViewRelevance(FPrimitiveViewRelevance& OutViewRelevance) const
{
	OutViewRelevance.Raw = Raw;
}

//////////////////////////////////////////////////////////////////////////

UMaterialInterface::UMaterialInterface(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		MaterialDomainString(MD_Surface); // find the enum for this now before we start saving
#if USE_EVENT_DRIVEN_ASYNC_LOAD_AT_BOOT_TIME
		if (!GIsInitialLoad || !GEventDrivenLoaderEnabled)
#endif
		{
			InitDefaultMaterials();
			AssertDefaultMaterialsExist();
		}

		if (SamplerTypeEnum == nullptr)
		{
			SamplerTypeEnum = StaticEnum<EMaterialSamplerType>();
			check(SamplerTypeEnum);
		}

		SetLightingGuid();
	}
}

void UMaterialInterface::PostLoad()
{
	Super::PostLoad();
#if USE_EVENT_DRIVEN_ASYNC_LOAD_AT_BOOT_TIME
	if (!GEventDrivenLoaderEnabled)
#endif
	{
		PostLoadDefaultMaterials();
	}

#if WITH_EDITORONLY_DATA
	if (TextureStreamingDataVersion != MATERIAL_TEXTURE_STREAMING_DATA_VERSION)
	{
		TextureStreamingData.Empty();
	}
#endif
}

void UMaterialInterface::GetUsedTexturesAndIndices(TArray<UTexture*>& OutTextures, TArray< TArray<int32> >& OutIndices, EMaterialQualityLevel::Type QualityLevel, ERHIFeatureLevel::Type FeatureLevel) const
{
	GetUsedTextures(OutTextures, QualityLevel, false, FeatureLevel, false);
	OutIndices.AddDefaulted(OutTextures.Num());
}

#if WITH_EDITORONLY_DATA
bool UMaterialInterface::GetStaticSwitchParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutValue, FGuid& OutExpressionGuid, bool bOveriddenOnly /*= false*/, bool bCheckParent /*= true*/) const
{
	return false;
}

bool UMaterialInterface::GetStaticComponentMaskParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, bool& R, bool& G, bool& B, bool& A, FGuid& OutExpressionGuid, bool bOveriddenOnly /*= false*/, bool bCheckParent /*= true*/) const
{
	return false;
}
#endif // WITH_EDITORONLY_DATA

FMaterialRelevance UMaterialInterface::GetRelevance_Internal(const UMaterial* Material, ERHIFeatureLevel::Type InFeatureLevel) const
{
	if(Material)
	{
		const FMaterialResource* MaterialResource = GetMaterialResource(InFeatureLevel);

		// If material is invalid e.g. unparented instance, fallback to the passed in material
		if (!MaterialResource && Material)
		{
			MaterialResource = Material->GetMaterialResource(InFeatureLevel);	
		}

		if (!MaterialResource)
		{
			return FMaterialRelevance();
		}

		const bool bIsMobile = InFeatureLevel <= ERHIFeatureLevel::ES3_1;
		const bool bUsesSingleLayerWaterMaterial = MaterialResource->GetShadingModels().HasShadingModel(MSM_SingleLayerWater);
		const bool IsSinglePassWaterTranslucent = bIsMobile && bUsesSingleLayerWaterMaterial;
		const bool bIsMobilePixelProjectedTranslucent = MaterialResource->IsUsingPlanarForwardReflections() 
														&& IsUsingMobilePixelProjectedReflection(GetFeatureLevelShaderPlatform(InFeatureLevel));

		// Note that even though XX_GameThread() api is called, this function can be called on non game thread via 
		// GetRelevance_Concurrent()
		bool bUsesAnisotropy = MaterialResource->GetShadingModels().HasAnyShadingModel({ MSM_DefaultLit, MSM_ClearCoat }) && 
			MaterialResource->MaterialUsesAnisotropy_GameThread();

		const EBlendMode BlendMode = (EBlendMode)GetBlendMode();
		const bool bIsTranslucent = IsTranslucentBlendMode(BlendMode) || IsSinglePassWaterTranslucent || bIsMobilePixelProjectedTranslucent; // We want meshes with water materials to be scheduled for translucent pass on mobile. And we also have to render the meshes used for mobile pixel projection reflection in translucent pass.

		EMaterialDomain Domain = (EMaterialDomain)MaterialResource->GetMaterialDomain();
		bool bDecal = (Domain == MD_DeferredDecal);

		// Determine the material's view relevance.
		FMaterialRelevance MaterialRelevance;

		MaterialRelevance.ShadingModelMask = GetShadingModels().GetShadingModelField();

		if(bDecal)
		{
			MaterialRelevance.bDecal = bDecal;
			// we rely on FMaterialRelevance defaults are 0
		}
		else
		{
			// Check whether the material can be drawn in the separate translucency pass as per FMaterialResource::IsTranslucencyAfterDOFEnabled and IsMobileSeparateTranslucencyEnabled
			bool bSupportsSeparateTranslucency = Material->MaterialDomain != MD_UI && Material->MaterialDomain != MD_DeferredDecal;
			bool bMaterialSeparateTranslucency = bSupportsSeparateTranslucency && (bIsMobile ? Material->bEnableMobileSeparateTranslucency : Material->bEnableSeparateTranslucency);

			// If dual blending is supported, and we are rendering separate translucency, then we also need to render a second pass to the modulation buffer.
			// The modulation buffer can also be used for regular modulation shaders after DoF.
			bool bMaterialSeparateModulation =
				(MaterialResource->IsDualBlendingEnabled(GShaderPlatformForFeatureLevel[InFeatureLevel]) || BlendMode == BLEND_Modulate)
				&& bMaterialSeparateTranslucency;

			MaterialRelevance.bOpaque = !bIsTranslucent;
			MaterialRelevance.bMasked = IsMasked();
			MaterialRelevance.bDistortion = MaterialResource->IsDistorted();
			MaterialRelevance.bHairStrands = IsCompatibleWithHairStrands(MaterialResource, InFeatureLevel);
			MaterialRelevance.bSeparateTranslucency = bIsTranslucent && bMaterialSeparateTranslucency;
			MaterialRelevance.bSeparateTranslucencyModulate = bIsTranslucent && bMaterialSeparateModulation;
			MaterialRelevance.bNormalTranslucency = bIsTranslucent && !bMaterialSeparateTranslucency;
			MaterialRelevance.bDisableDepthTest = bIsTranslucent && Material->bDisableDepthTest;		
			MaterialRelevance.bUsesSceneColorCopy = bIsTranslucent && MaterialResource->RequiresSceneColorCopy_GameThread();
			MaterialRelevance.bOutputsTranslucentVelocity = Material->IsTranslucencyWritingVelocity();
			MaterialRelevance.bUsesGlobalDistanceField = MaterialResource->UsesGlobalDistanceField_GameThread();
			MaterialRelevance.bUsesWorldPositionOffset = MaterialResource->UsesWorldPositionOffset_GameThread();
			ETranslucencyLightingMode TranslucencyLightingMode = MaterialResource->GetTranslucencyLightingMode();
			MaterialRelevance.bTranslucentSurfaceLighting = bIsTranslucent && (TranslucencyLightingMode == TLM_SurfacePerPixelLighting || TranslucencyLightingMode == TLM_Surface);
			MaterialRelevance.bUsesSceneDepth = MaterialResource->MaterialUsesSceneDepthLookup_GameThread();
			MaterialRelevance.bHasVolumeMaterialDomain = MaterialResource->IsVolumetricPrimitive();
			MaterialRelevance.bUsesDistanceCullFade = MaterialResource->MaterialUsesDistanceCullFade_GameThread();
			MaterialRelevance.bUsesCustomDepthStencil = MaterialResource->UsesCustomDepthStencil_GameThread();
			MaterialRelevance.bShouldRenderDepthToTranslucency = MaterialResource->ShouldWriteDepthToTranslucentMaterial();
			MaterialRelevance.bUsesSkyMaterial = Material->bIsSky;
			MaterialRelevance.bUsesSingleLayerWaterMaterial = bUsesSingleLayerWaterMaterial;
			MaterialRelevance.bUsesAnisotropy = bUsesAnisotropy;
		}
		return MaterialRelevance;
	}
	else
	{
		return FMaterialRelevance();
	}
}

FMaterialParameterInfo UMaterialInterface::GetParameterInfo(EMaterialParameterAssociation Association, FName ParameterName, UMaterialFunctionInterface* LayerFunction) const
{
	int32 Index = 0;
	if (Association != GlobalParameter)
	{
		Index = LayerFunction ? GetLayerParameterIndex(Association, LayerFunction) : INDEX_NONE;
		if (Index == INDEX_NONE)
		{
			return FMaterialParameterInfo();
		}
	}

	return FMaterialParameterInfo(ParameterName, Association, Index);
}

FMaterialRelevance UMaterialInterface::GetRelevance(ERHIFeatureLevel::Type InFeatureLevel) const
{
	// Find the interface's concrete material.
	const UMaterial* Material = GetMaterial();
	return GetRelevance_Internal(Material, InFeatureLevel);
}

FMaterialRelevance UMaterialInterface::GetRelevance_Concurrent(ERHIFeatureLevel::Type InFeatureLevel) const
{
	// Find the interface's concrete material.
	const UMaterial* Material = GetMaterial_Concurrent();
	return GetRelevance_Internal(Material, InFeatureLevel);
}

int32 UMaterialInterface::GetWidth() const
{
	return ME_PREV_THUMBNAIL_SZ+(ME_STD_BORDER*2);
}

int32 UMaterialInterface::GetHeight() const
{
	return ME_PREV_THUMBNAIL_SZ+ME_CAPTION_HEIGHT+(ME_STD_BORDER*2);
}


void UMaterialInterface::SetForceMipLevelsToBeResident( bool OverrideForceMiplevelsToBeResident, bool bForceMiplevelsToBeResidentValue, float ForceDuration, int32 CinematicTextureGroups, bool bFastResponse )
{
	TArray<UTexture*> Textures;
	
	GetUsedTextures(Textures, EMaterialQualityLevel::Num, false, ERHIFeatureLevel::Num, true);
	for ( int32 TextureIndex=0; TextureIndex < Textures.Num(); ++TextureIndex )
	{
		UTexture2D* Texture = Cast<UTexture2D>(Textures[TextureIndex]);
		if ( Texture )
		{
			Texture->SetForceMipLevelsToBeResident( ForceDuration, CinematicTextureGroups );
			if (OverrideForceMiplevelsToBeResident)
			{
				Texture->bForceMiplevelsToBeResident = bForceMiplevelsToBeResidentValue;
			}

			if (bFastResponse && (ForceDuration > 0.f || Texture->bForceMiplevelsToBeResident))
			{
				static IConsoleVariable* CVarAllowFastForceResident = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Streaming.AllowFastForceResident"));

				Texture->bIgnoreStreamingMipBias = CVarAllowFastForceResident && CVarAllowFastForceResident->GetInt();
				if (Texture->IsStreamable())
				{
					IStreamingManager::Get().GetRenderAssetStreamingManager().FastForceFullyResident(Texture);
				}
			}
		}
	}
}

void UMaterialInterface::RecacheAllMaterialUniformExpressions(bool bRecreateUniformBuffer)
{
	// For each interface, reacache its uniform parameters
	for( TObjectIterator<UMaterialInterface> MaterialIt; MaterialIt; ++MaterialIt )
	{
		MaterialIt->RecacheUniformExpressions(bRecreateUniformBuffer);
	}
}

bool UMaterialInterface::IsReadyForFinishDestroy()
{
	bool bIsReady = Super::IsReadyForFinishDestroy();
	bIsReady = bIsReady && ParentRefFence.IsFenceComplete(); 
	return bIsReady;
}

void UMaterialInterface::BeginDestroy()
{
	ParentRefFence.BeginFence();
	Super::BeginDestroy();
}

void UMaterialInterface::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	SetLightingGuid();
}

#if WITH_EDITOR
void UMaterialInterface::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// flush the lighting guid on all changes
	SetLightingGuid();

	LightmassSettings.EmissiveBoost = FMath::Max(LightmassSettings.EmissiveBoost, 0.0f);
	LightmassSettings.DiffuseBoost = FMath::Max(LightmassSettings.DiffuseBoost, 0.0f);
	LightmassSettings.ExportResolutionScale = FMath::Clamp(LightmassSettings.ExportResolutionScale, 0.0f, 16.0f);

	for (UAssetUserData* Datum : AssetUserData)
	{
		if (Datum != nullptr)
		{
			Datum->PostEditChangeOwner();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UMaterialInterface::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	if (AssetImportData)
	{
		OutTags.Add( FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden) );
	}

	Super::GetAssetRegistryTags(OutTags);
}
#endif // WITH_EDITOR

void UMaterialInterface::GetLightingGuidChain(bool bIncludeTextures, TArray<FGuid>& OutGuids) const
{
#if WITH_EDITORONLY_DATA
	OutGuids.Add(LightingGuid);
#endif // WITH_EDITORONLY_DATA
}

bool UMaterialInterface::GetVectorParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue, bool bOveriddenOnly) const
{
	// is never called but because our system wants a UMaterialInterface instance we cannot use "virtual =0"
	return false;
}

#if WITH_EDITOR
bool UMaterialInterface::IsVectorParameterUsedAsChannelMask(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutValue) const
{
	return false;
}

bool UMaterialInterface::GetVectorParameterChannelNames(const FHashedMaterialParameterInfo& ParameterInfo, FParameterChannelNames& OutValue) const
{
	return false;
}

bool UMaterialInterface::GetScalarParameterSliderMinMax(const FHashedMaterialParameterInfo& ParameterInfo, float& OutSliderMin, float& OutSliderMax) const
{
	return false;
}
#endif // WITH_EDITOR

bool UMaterialInterface::GetScalarParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, float& OutValue, bool bOveriddenOnly) const
{
	// is never called but because our system wants a UMaterialInterface instance we cannot use "virtual =0"
	return false;
}

#if WITH_EDITOR
bool UMaterialInterface::IsScalarParameterUsedAsAtlasPosition(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutValue, TSoftObjectPtr<class UCurveLinearColor>& Curve, TSoftObjectPtr<class UCurveLinearColorAtlas>& Atlas) const
{
	return false;
}
#endif // WITH_EDITOR

bool UMaterialInterface::GetScalarCurveParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, FInterpCurveFloat& OutValue) const
{
	return false;
}

bool UMaterialInterface::GetVectorCurveParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, FInterpCurveVector& OutValue) const
{
	return false;
}

bool UMaterialInterface::GetLinearColorParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue) const
{
	return false;
}

bool UMaterialInterface::GetLinearColorCurveParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, FInterpCurveLinearColor& OutValue) const
{
	return false;
}

bool UMaterialInterface::GetTextureParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, UTexture*& OutValue, bool bOveriddenOnly) const
{
	return false;
}

bool UMaterialInterface::GetRuntimeVirtualTextureParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, URuntimeVirtualTexture*& OutValue, bool bOveriddenOnly) const
{
	return false;
}

#if WITH_EDITOR
bool UMaterialInterface::GetTextureParameterChannelNames(const FHashedMaterialParameterInfo& ParameterInfo, FParameterChannelNames& OutValue) const
{
	return false;
}
#endif

bool UMaterialInterface::GetFontParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, class UFont*& OutFontValue, int32& OutFontPage, bool bOveriddenOnly) const
{
	return false;
}

bool UMaterialInterface::GetRefractionSettings(float& OutBiasValue) const
{
	return false;
}

#if WITH_EDITOR
bool UMaterialInterface::GetParameterDesc(const FHashedMaterialParameterInfo& ParameterInfo, FString& OutDesc, const TArray<struct FStaticMaterialLayersParameter>* MaterialLayersParameters) const
{
	return false;
}

bool UMaterialInterface::GetGroupName(const FHashedMaterialParameterInfo& ParameterInfo, FName& OutDesc) const
{
	return false;
}
#endif // WITH_EDITOR

UMaterial* UMaterialInterface::GetBaseMaterial()
{
	return GetMaterial();
}

bool DoesMaterialUseTexture(const UMaterialInterface* Material,const UTexture* CheckTexture)
{
	//Do not care if we're running dedicated server
	if (FPlatformProperties::IsServerOnly())
	{
		return false;
	}

	TArray<UTexture*> Textures;
	Material->GetUsedTextures(Textures, EMaterialQualityLevel::Num, true, GMaxRHIFeatureLevel, true);
	for (int32 i = 0; i < Textures.Num(); i++)
	{
		if (Textures[i] == CheckTexture)
		{
			return true;
		}
	}
	return false;
}

float UMaterialInterface::GetOpacityMaskClipValue() const
{
	return 0.0f;
}

EBlendMode UMaterialInterface::GetBlendMode() const
{
	return BLEND_Opaque;
}

bool UMaterialInterface::IsTwoSided() const
{
	return false;
}

bool UMaterialInterface::IsDitheredLODTransition() const
{
	return false;
}

bool UMaterialInterface::IsTranslucencyWritingCustomDepth() const
{
	return false;
}

bool UMaterialInterface::IsTranslucencyWritingVelocity() const
{
	return false;
}

bool UMaterialInterface::IsMasked() const
{
	return false;
}

bool UMaterialInterface::IsDeferredDecal() const
{
	return false;
}
bool UMaterialInterface::GetCastDynamicShadowAsMasked() const
{
	return false;
}

FMaterialShadingModelField UMaterialInterface::GetShadingModels() const
{
	return MSM_DefaultLit;
}

bool UMaterialInterface::IsShadingModelFromMaterialExpression() const
{
	return false;
}

USubsurfaceProfile* UMaterialInterface::GetSubsurfaceProfile_Internal() const
{
	return NULL;
}

bool UMaterialInterface::CastsRayTracedShadows() const
{
	return true;
}

void UMaterialInterface::SetFeatureLevelToCompile(ERHIFeatureLevel::Type FeatureLevel, bool bShouldCompile)
{
	uint32 FeatureLevelBit = (1 << FeatureLevel);
	if (bShouldCompile)
	{
		FeatureLevelsToForceCompile |= FeatureLevelBit;
	}
	else
	{
		FeatureLevelsToForceCompile &= (~FeatureLevelBit);
	}
}

uint32 UMaterialInterface::FeatureLevelsForAllMaterials = 0;

void UMaterialInterface::SetGlobalRequiredFeatureLevel(ERHIFeatureLevel::Type FeatureLevel, bool bShouldCompile)
{
	uint32 FeatureLevelBit = (1 << FeatureLevel);
	if (bShouldCompile)
	{
		FeatureLevelsForAllMaterials |= FeatureLevelBit;
	}
	else
	{
		FeatureLevelsForAllMaterials &= (~FeatureLevelBit);
	}
}


uint32 UMaterialInterface::GetFeatureLevelsToCompileForRendering() const
{
	return FeatureLevelsToForceCompile | GetFeatureLevelsToCompileForAllMaterials();
}


void UMaterialInterface::UpdateMaterialRenderProxy(FMaterialRenderProxy& Proxy)
{
	// no 0 pointer
	check(&Proxy);

	FMaterialShadingModelField MaterialShadingModels = GetShadingModels();

	// for better performance we only update SubsurfaceProfileRT if the feature is used
	if (UseSubsurfaceProfile(MaterialShadingModels))
	{
		FSubsurfaceProfileStruct Settings;

		USubsurfaceProfile* LocalSubsurfaceProfile = GetSubsurfaceProfile_Internal();
		
		if (LocalSubsurfaceProfile)
		{
			Settings = LocalSubsurfaceProfile->Settings;
		}

		FMaterialRenderProxy* InProxy = &Proxy;
		ENQUEUE_RENDER_COMMAND(UpdateMaterialRenderProxySubsurface)(
			[Settings, LocalSubsurfaceProfile, InProxy](FRHICommandListImmediate& RHICmdList)
			{
				uint32 AllocationId = 0;

				if (LocalSubsurfaceProfile)
				{
					AllocationId = GSubsurfaceProfileTextureObject.AddOrUpdateProfile(Settings, LocalSubsurfaceProfile);

					check(AllocationId >= 0 && AllocationId <= 255);
				}
				InProxy->SetSubsurfaceProfileRT(LocalSubsurfaceProfile);
			});
	}
}

bool FMaterialTextureInfo::IsValid(bool bCheckTextureIndex) const
{ 
#if WITH_EDITORONLY_DATA
	if (bCheckTextureIndex && (TextureIndex < 0 || TextureIndex >= TEXSTREAM_MAX_NUM_TEXTURES_PER_MATERIAL))
	{
		return false;
	}
#endif
	return TextureName != NAME_None && SamplingScale > SMALL_NUMBER && UVChannelIndex >= 0 && UVChannelIndex < TEXSTREAM_MAX_NUM_UVCHANNELS; 
}

void UMaterialInterface::SortTextureStreamingData(bool bForceSort, bool bFinalSort)
{
#if WITH_EDITOR
	// In cook that was already done in the save.
	if (!bTextureStreamingDataSorted || bForceSort)
	{
		TSet<const UTexture*> UsedTextures;
		if (bFinalSort)
		{
			TSet<const UTexture*> UnfilteredUsedTextures;
			GetReferencedTexturesAndOverrides(UnfilteredUsedTextures);

			// Sort some of the conditions that could make the texture unstreamable, to make the data leaner.
			// Note that because we are cooking, UStreamableRenderAsset::bIsStreamable is not reliable here.
			for (const UTexture* UnfilteredTexture : UnfilteredUsedTextures)
			{
				if (UnfilteredTexture && !UnfilteredTexture->NeverStream && UnfilteredTexture->LODGroup != TEXTUREGROUP_UI && UnfilteredTexture->MipGenSettings != TMGS_NoMipmaps)
				{
					UsedTextures.Add(UnfilteredTexture);
				}
			}
		}

		for (int32 Index = 0; Index < TextureStreamingData.Num(); ++Index)
		{
			FMaterialTextureInfo& TextureData = TextureStreamingData[Index];
			UTexture* Texture = Cast<UTexture>(TextureData.TextureReference.ResolveObject());

			// Also, when cooking, only keep textures that are directly referenced by this material to prevent non-deterministic cooking.
			// This would happen if a texture reference resolves to a texture not used anymore by this material. The resolved object could then be valid or not.
			if (Texture && (!bFinalSort || UsedTextures.Contains(Texture)))
			{
				TextureData.TextureName = Texture->GetFName();
			}
			else if (bFinalSort) // In the final sort we remove null names as they will never match.
			{
				TextureStreamingData.RemoveAtSwap(Index);
				--Index;
			}
			else
			{
				TextureData.TextureName = NAME_None;
			}
		}

		// Sort by name to be compatible with FindTextureStreamingDataIndexRange
		TextureStreamingData.Sort([](const FMaterialTextureInfo& Lhs, const FMaterialTextureInfo& Rhs) 
		{ 
			// Sort by register indices when the name are the same, as when initially added in the streaming data.
			if (Lhs.TextureName == Rhs.TextureName)
			{
				return Lhs.TextureIndex < Rhs.TextureIndex;

			}
			return Lhs.TextureName.LexicalLess(Rhs.TextureName); 
		});
		bTextureStreamingDataSorted = true;
	}
#endif // WITH_EDITOR
}

extern 	TAutoConsoleVariable<int32> CVarStreamingUseMaterialData;

bool UMaterialInterface::FindTextureStreamingDataIndexRange(FName TextureName, int32& LowerIndex, int32& HigherIndex) const
{
#if WITH_EDITORONLY_DATA
	// Because of redirectors (when textures are renammed), the texture names might be invalid and we need to udpate the data at every load.
	// Normally we would do that in the post load, but since the process needs to resolve the SoftObjectPaths, this is forbidden at that place.
	// As a workaround, we do it on demand. Note that this is not required in cooked build as it is done in the presave.
	const_cast<UMaterialInterface*>(this)->SortTextureStreamingData(false, false);
#endif

	if (CVarStreamingUseMaterialData.GetValueOnGameThread() == 0 || CVarStreamingUseNewMetrics.GetValueOnGameThread() == 0)
	{
		return false;
	}

	const int32 MatchingIndex = Algo::BinarySearchBy(TextureStreamingData, TextureName, &FMaterialTextureInfo::TextureName, FNameLexicalLess());
	if (MatchingIndex != INDEX_NONE)
	{
		// Find the range of entries for this texture. 
		// This is possible because the same texture could be bound to several register and also be used with different sampling UV.
		LowerIndex = MatchingIndex;
		HigherIndex = MatchingIndex;
		while (HigherIndex + 1 < TextureStreamingData.Num() && TextureStreamingData[HigherIndex + 1].TextureName == TextureName)
		{
			++HigherIndex;
		}
		return true;
	}
	return false;
}

void UMaterialInterface::SetTextureStreamingData(const TArray<FMaterialTextureInfo>& InTextureStreamingData)
{
	TextureStreamingData = InTextureStreamingData;
#if WITH_EDITORONLY_DATA
	bTextureStreamingDataSorted = false;
	TextureStreamingDataVersion = InTextureStreamingData.Num() ? MATERIAL_TEXTURE_STREAMING_DATA_VERSION : 0;
	TextureStreamingDataMissingEntries.Empty();
#endif
	SortTextureStreamingData(true, false);
}

float UMaterialInterface::GetTextureDensity(FName TextureName, const FMeshUVChannelInfo& UVChannelData) const
{
	ensure(UVChannelData.bInitialized);

	int32 LowerIndex = INDEX_NONE;
	int32 HigherIndex = INDEX_NONE;
	if (FindTextureStreamingDataIndexRange(TextureName, LowerIndex, HigherIndex))
	{
		// Compute the max, at least one entry will be valid. 
		float MaxDensity = 0;
		for (int32 Index = LowerIndex; Index <= HigherIndex; ++Index)
		{
			const FMaterialTextureInfo& MatchingData = TextureStreamingData[Index];
			ensure(MatchingData.IsValid() && MatchingData.TextureName == TextureName);
			MaxDensity = FMath::Max<float>(UVChannelData.LocalUVDensities[MatchingData.UVChannelIndex] / MatchingData.SamplingScale, MaxDensity);
		}
		return MaxDensity;
	}

	// Otherwise return 0 to indicate the data is not found.
	return 0;
}

bool UMaterialInterface::UseAnyStreamingTexture() const
{
	TArray<UTexture*> Textures;
	GetUsedTextures(Textures, EMaterialQualityLevel::Num, true, ERHIFeatureLevel::Num, true);

	for (UTexture* Texture : Textures)
	{
		if (Texture && Texture->IsStreamable())
		{
			return true;
		}
	}
	return false;
}

void UMaterialInterface::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);
	if (TargetPlatform && TargetPlatform->RequiresCookedData())
	{
		SortTextureStreamingData(true, true);
	}
}

void UMaterialInterface::AddAssetUserData(UAssetUserData* InUserData)
{
	if (InUserData != nullptr)
	{
		UAssetUserData* ExistingData = GetAssetUserDataOfClass(InUserData->GetClass());
		if (ExistingData != nullptr)
		{
			AssetUserData.Remove(ExistingData);
		}
		AssetUserData.Add(InUserData);
	}
}

UAssetUserData* UMaterialInterface::GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for (int32 DataIdx = 0; DataIdx < AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if (Datum != nullptr && Datum->IsA(InUserDataClass))
		{
			return Datum;
		}
	}
	return nullptr;
}

void UMaterialInterface::RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for (int32 DataIdx = 0; DataIdx < AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if (Datum != nullptr && Datum->IsA(InUserDataClass))
		{
			AssetUserData.RemoveAt(DataIdx);
			return;
		}
	}
}

