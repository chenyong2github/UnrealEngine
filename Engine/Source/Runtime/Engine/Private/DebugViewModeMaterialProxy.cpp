// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
DebugViewModeMaterialProxy.cpp : Contains definitions the debug view mode material shaders.
=============================================================================*/

#include "DebugViewModeMaterialProxy.h"
#include "DebugViewModeInterface.h"
#include "SceneInterface.h"
#include "EngineModule.h"
#include "RendererInterface.h"

#if WITH_EDITORONLY_DATA

FDebugViewModeMaterialProxy::FDebugViewModeMaterialProxy(
	UMaterialInterface* InMaterialInterface, 
	EMaterialQualityLevel::Type QualityLevel, 
	ERHIFeatureLevel::Type InFeatureLevel,
	bool InSynchronousCompilation, 
	EDebugViewShaderMode InDebugViewMode
)
	: FMaterial()
	, MaterialInterface(InMaterialInterface)
	, Material(nullptr)
	, FeatureLevel(InFeatureLevel)
	, Usage(EMaterialShaderMapUsage::DebugViewMode)
	, DebugViewMode(InDebugViewMode)
	, PixelShaderName(nullptr)
	, CachedMaterialUsage(0)
	, bValid(true)
	, bIsDefaultMaterial(InMaterialInterface->GetMaterial()->IsDefaultMaterial())
	, bSynchronousCompilation(InSynchronousCompilation)
{
	SetQualityLevelProperties(QualityLevel, false, FeatureLevel);
	const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(FeatureLevel);

	Material = InMaterialInterface->GetMaterial();
	MaterialInterface->AppendReferencedTextures(ReferencedTextures);

	FMaterialResource* Resource = InMaterialInterface->GetMaterialResource(FeatureLevel);
	if (Resource)
	{
		const FDebugViewModeInterface* DebugViewModeInterface = FDebugViewModeInterface::GetInterface(InDebugViewMode);
		if (DebugViewModeInterface)
		{
			PixelShaderName = DebugViewModeInterface->PixelShaderName;

			if (!DebugViewModeInterface->bNeedsOnlyLocalVertexFactor)
			{
				// Cache material usage.
				bIsUsedWithSkeletalMesh = Resource->IsUsedWithSkeletalMesh();
				bIsUsedWithLandscape = Resource->IsUsedWithLandscape();
				bIsUsedWithParticleSystem = Resource->IsUsedWithParticleSystem();
				bIsUsedWithParticleSprites = Resource->IsUsedWithParticleSprites();
				bIsUsedWithBeamTrails = Resource->IsUsedWithBeamTrails();
				bIsUsedWithMeshParticles = Resource->IsUsedWithMeshParticles();
				bIsUsedWithNiagaraSprites = Resource->IsUsedWithNiagaraSprites();
				bIsUsedWithNiagaraRibbons = Resource->IsUsedWithNiagaraRibbons();
				bIsUsedWithNiagaraMeshParticles = Resource->IsUsedWithNiagaraMeshParticles();
				bIsUsedWithMorphTargets = Resource->IsUsedWithMorphTargets();
				bIsUsedWithSplineMeshes = Resource->IsUsedWithSplineMeshes();
				bIsUsedWithInstancedStaticMeshes = Resource->IsUsedWithInstancedStaticMeshes();
				bIsUsedWithAPEXCloth = Resource->IsUsedWithAPEXCloth();
				bIsUsedWithWater = Resource->IsUsedWithWater();
			}
		}

		FMaterialShaderMapId ResourceId;
		Resource->GetShaderMapId(ShaderPlatform, ResourceId);

		FStaticParameterSet StaticParamSet;
		Resource->GetStaticParameterSet(ShaderPlatform, StaticParamSet);

		{
			TArray<FShaderType*> ShaderTypes;
			TArray<FVertexFactoryType*> VFTypes;
			TArray<const FShaderPipelineType*> ShaderPipelineTypes;
			GetDependentShaderAndVFTypes(ShaderPlatform, ShaderTypes, ShaderPipelineTypes, VFTypes);

			// Overwrite the shader map Id's dependencies with ones that came from the FMaterial actually being compiled (this)
			// This is necessary as we change FMaterial attributes like GetShadingModels(), which factor into the ShouldCache functions that determine dependent shader types
			ResourceId.SetShaderDependencies(ShaderTypes, ShaderPipelineTypes, VFTypes, ShaderPlatform);
		}

		ResourceId.Usage = Usage;

		CacheShaders(ResourceId, &StaticParamSet, ShaderPlatform);
	}
	else
	{
		bValid = false;
	}
}

bool FDebugViewModeMaterialProxy::RequiresSynchronousCompilation() const
{ 
	return bSynchronousCompilation;
}

const FMaterial& FDebugViewModeMaterialProxy::GetMaterialWithFallback(ERHIFeatureLevel::Type InFeatureLevel, const FMaterialRenderProxy*& OutFallbackMaterialRenderProxy) const
{
	if (GetRenderingThreadShaderMap())
	{
		return *this;
	}
	else
	{
		OutFallbackMaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
		return OutFallbackMaterialRenderProxy->GetMaterialWithFallback(InFeatureLevel, OutFallbackMaterialRenderProxy);
	}
}

bool FDebugViewModeMaterialProxy::GetVectorValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const
{
	return MaterialInterface->GetRenderProxy()->GetVectorValue(ParameterInfo, OutValue, Context);
}

bool FDebugViewModeMaterialProxy::GetScalarValue(const FMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const
{
	return MaterialInterface->GetRenderProxy()->GetScalarValue(ParameterInfo, OutValue, Context);
}

bool FDebugViewModeMaterialProxy::GetTextureValue(const FMaterialParameterInfo& ParameterInfo,const UTexture** OutValue, const FMaterialRenderContext& Context) const
{
	return MaterialInterface->GetRenderProxy()->GetTextureValue(ParameterInfo,OutValue,Context);
}

bool FDebugViewModeMaterialProxy::GetTextureValue(const FMaterialParameterInfo& ParameterInfo, const URuntimeVirtualTexture** OutValue, const FMaterialRenderContext& Context) const
{
	return MaterialInterface->GetRenderProxy()->GetTextureValue(ParameterInfo, OutValue, Context);
}

EMaterialDomain FDebugViewModeMaterialProxy::GetMaterialDomain() const
{
	return Material ? (EMaterialDomain)(Material->MaterialDomain) : MD_Surface;
}

bool FDebugViewModeMaterialProxy::IsTwoSided() const
{ 
	return MaterialInterface && MaterialInterface->IsTwoSided();
}

bool FDebugViewModeMaterialProxy::IsDitheredLODTransition() const
{ 
	return MaterialInterface && MaterialInterface->IsDitheredLODTransition();
}

bool FDebugViewModeMaterialProxy::IsLightFunction() const
{
	return Material && Material->MaterialDomain == MD_LightFunction;
}

bool FDebugViewModeMaterialProxy::IsDeferredDecal() const
{
	return	Material && Material->MaterialDomain == MD_DeferredDecal;
}

bool FDebugViewModeMaterialProxy::IsSpecialEngineMaterial() const
{
	return Material && Material->bUsedAsSpecialEngineMaterial;
}

bool FDebugViewModeMaterialProxy::IsWireframe() const
{
	return Material && Material->Wireframe;
}

bool FDebugViewModeMaterialProxy::IsMasked() const
{ 
	return Material && Material->IsMasked(); 
}

enum EBlendMode FDebugViewModeMaterialProxy::GetBlendMode() const
{ 
	return MaterialInterface ? MaterialInterface->GetBlendMode() : BLEND_Opaque;
}

FMaterialShadingModelField FDebugViewModeMaterialProxy::GetShadingModels() const
{ 
	return Material ? Material->GetShadingModels() : MSM_Unlit;
}

bool FDebugViewModeMaterialProxy::IsShadingModelFromMaterialExpression() const
{ 
	return Material ? Material->IsShadingModelFromMaterialExpression() : false;
}

float FDebugViewModeMaterialProxy::GetOpacityMaskClipValue() const
{ 
	return Material ? Material->GetOpacityMaskClipValue() : .5f;
}

bool FDebugViewModeMaterialProxy::GetCastDynamicShadowAsMasked() const
{
	return Material ? Material->GetCastShadowAsMasked() : false;
}

void FDebugViewModeMaterialProxy::GatherCustomOutputExpressions(TArray<class UMaterialExpressionCustomOutput*>& OutCustomOutputs) const
{
	if (Material)
	{
		Material->GetAllCustomOutputExpressions(OutCustomOutputs);
	}
}

void FDebugViewModeMaterialProxy::GatherExpressionsForCustomInterpolators(TArray<class UMaterialExpression*>& OutExpressions) const
{
	if (Material)
	{
		Material->GetAllExpressionsForCustomInterpolators(OutExpressions);
	}
}

enum EMaterialTessellationMode FDebugViewModeMaterialProxy::GetTessellationMode() const
{
	FMaterialResource* Resource = MaterialInterface->GetMaterialResource(FeatureLevel);
	return Resource ? Resource->GetTessellationMode() : MTM_NoTessellation;
}

bool FDebugViewModeMaterialProxy::IsCrackFreeDisplacementEnabled() const
{
	FMaterialResource* Resource = MaterialInterface->GetMaterialResource(FeatureLevel);
	return Resource ? Resource->IsCrackFreeDisplacementEnabled() : false;
}

bool FDebugViewModeMaterialProxy::IsAdaptiveTessellationEnabled() const
{
	FMaterialResource* Resource = MaterialInterface->GetMaterialResource(FeatureLevel);
	return Resource ? Resource->IsAdaptiveTessellationEnabled() : false;
}

float FDebugViewModeMaterialProxy::GetMaxDisplacement() const
{
	FMaterialResource* Resource = MaterialInterface->GetMaterialResource(FeatureLevel);
	return Resource ? Resource->GetMaxDisplacement() : 0.0f;
}

#endif // WITH_EDITORONLY_DATA
