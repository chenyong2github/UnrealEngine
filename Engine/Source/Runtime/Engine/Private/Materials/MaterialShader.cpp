// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialShader.h"
#include "Stats/StatsMisc.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialFunctionInterface.h"
#include "ProfilingDebugging/DiagnosticTable.h"
#include "MeshMaterialShaderType.h"
#include "MaterialShaderMapLayout.h"
#include "ShaderCompiler.h"
#include "DerivedDataCacheInterface.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "ShaderDerivedDataVersion.h"
#include "ProfilingDebugging/CookStats.h"
#include "UObject/ReleaseObjectVersion.h"
#include "UObject/EditorObjectVersion.h"
#include "ProfilingDebugging/LoadTimeTracker.h"

#if ENABLE_COOK_STATS
namespace MaterialShaderCookStats
{
	static FCookStats::FDDCResourceUsageStats UsageStats;
	static int32 ShadersCompiled = 0;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("MaterialShader.Usage"), TEXT(""));
		AddStat(TEXT("MaterialShader.Misc"), FCookStatsManager::CreateKeyValueArray(
			TEXT("ShadersCompiled"), ShadersCompiled
			));
	});
}
#endif

//
// Globals
//
FCriticalSection FMaterialShaderMap::GIdToMaterialShaderMapCS;
TMap<FMaterialShaderMapId,FMaterialShaderMap*> FMaterialShaderMap::GIdToMaterialShaderMap[SP_NumPlatforms];
#if ALLOW_SHADERMAP_DEBUG_DATA
TArray<FMaterialShaderMap*> FMaterialShaderMap::AllMaterialShaderMaps;
#endif

/** 
 * Tracks material resources and their shader maps that are being compiled.
 * Uses a TRefCountPtr as this will be the only reference to a shader map while it is being compiled.
 */
TMap<TRefCountPtr<FMaterialShaderMap>, TArray<FMaterial*> > FMaterialShaderMap::ShaderMapsBeingCompiled;

static inline bool ShouldCacheMaterialShader(const FMaterialShaderType* ShaderType, EShaderPlatform Platform, const FMaterial* Material, int32 PermutationId)
{
	return ShaderType->ShouldCompilePermutation(Platform, Material, PermutationId) && Material->ShouldCache(Platform, ShaderType, nullptr);
}


/** Converts an EMaterialShadingModel to a string description. */
FString GetShadingModelString(EMaterialShadingModel ShadingModel)
{
	FString ShadingModelName;
	switch(ShadingModel)
	{
		case MSM_Unlit:				ShadingModelName = TEXT("MSM_Unlit"); break;
		case MSM_DefaultLit:		ShadingModelName = TEXT("MSM_DefaultLit"); break;
		case MSM_Subsurface:		ShadingModelName = TEXT("MSM_Subsurface"); break;
		case MSM_PreintegratedSkin:	ShadingModelName = TEXT("MSM_PreintegratedSkin"); break;
		case MSM_ClearCoat:			ShadingModelName = TEXT("MSM_ClearCoat"); break;
		case MSM_SubsurfaceProfile:	ShadingModelName = TEXT("MSM_SubsurfaceProfile"); break;
		case MSM_TwoSidedFoliage:	ShadingModelName = TEXT("MSM_TwoSidedFoliage"); break;
		case MSM_Cloth:				ShadingModelName = TEXT("MSM_Cloth"); break;
		case MSM_Eye:				ShadingModelName = TEXT("MSM_Eye"); break;
		case MSM_SingleLayerWater:	ShadingModelName = TEXT("MSM_SingleLayerWater"); break;
		case MSM_ThinTranslucent:	ShadingModelName = TEXT("MSM_ThinTranslucent"); break;
		default: ShadingModelName = TEXT("Unknown"); break;
	}
	return ShadingModelName;
}


/** Converts an FMaterialShadingModelField to a string description containing all the shading models present, delimited by "|" */
FString GetShadingModelFieldString(FMaterialShadingModelField ShadingModels, const FShadingModelToStringDelegate& Delegate, const FString& Delimiter)
{
	FString ShadingModelsName;
	uint32 TempShadingModels = (uint32)ShadingModels.GetShadingModelField();

	while (TempShadingModels)
	{
		uint32 BitIndex = FMath::CountTrailingZeros(TempShadingModels); // Find index of first set bit
		TempShadingModels &= ~(1 << BitIndex); // Flip first set bit to 0
		ShadingModelsName += Delegate.Execute((EMaterialShadingModel)BitIndex); // Add the name of the shading model corresponding to that bit

		// If there are more bits left, add a pipe limiter to the string 
		if (TempShadingModels)
		{
			ShadingModelsName.Append(Delimiter);
		}
	}

	return ShadingModelsName;
}

/** Converts an FMaterialShadingModelField to a string description containing all the shading models present, delimited by "|" */
FString GetShadingModelFieldString(FMaterialShadingModelField ShadingModels)
{
	return GetShadingModelFieldString(ShadingModels, FShadingModelToStringDelegate::CreateStatic(&GetShadingModelString), TEXT("|"));
}

/** Converts an EBlendMode to a string description. */
FString GetBlendModeString(EBlendMode BlendMode)
{
	FString BlendModeName;
	switch(BlendMode)
	{
		case BLEND_Opaque: BlendModeName = TEXT("BLEND_Opaque"); break;
		case BLEND_Masked: BlendModeName = TEXT("BLEND_Masked"); break;
		case BLEND_Translucent: BlendModeName = TEXT("BLEND_Translucent"); break;
		case BLEND_Additive: BlendModeName = TEXT("BLEND_Additive"); break;
		case BLEND_Modulate: BlendModeName = TEXT("BLEND_Modulate"); break;
		case BLEND_AlphaComposite: BlendModeName = TEXT("BLEND_AlphaComposite"); break;
		case BLEND_AlphaHoldout: BlendModeName = TEXT("BLEND_AlphaHoldout"); break;
		default: BlendModeName = TEXT("Unknown"); break;
	}
	return BlendModeName;
}

#if WITH_EDITOR
/** Creates a string key for the derived data cache given a shader map id. */
static FString GetMaterialShaderMapKeyString(const FMaterialShaderMapId& ShaderMapId, EShaderPlatform Platform, const ITargetPlatform* TargetPlatform)
{
	FName Format = LegacyShaderPlatformToShaderFormat(Platform);
	FString ShaderMapKeyString = Format.ToString() + TEXT("_") + FString(FString::FromInt(GetTargetPlatformManagerRef().ShaderFormatVersion(Format))) + TEXT("_");

	ShaderMapAppendKeyString(Platform, ShaderMapKeyString);
	ShaderMapId.AppendKeyString(ShaderMapKeyString);
	FMaterialAttributeDefinitionMap::AppendDDCKeyString(ShaderMapKeyString);
	return FDerivedDataCacheInterface::BuildCacheKey(TEXT("MATSM"), MATERIALSHADERMAP_DERIVEDDATA_VER, *ShaderMapKeyString);
}
#endif // WITH_EDITOR

/** Called for every material shader to update the appropriate stats. */
void UpdateMaterialShaderCompilingStats(const FMaterial* Material)
{
	INC_DWORD_STAT_BY(STAT_ShaderCompiling_NumTotalMaterialShaders,1);

	switch(Material->GetBlendMode())
	{
		case BLEND_Opaque: INC_DWORD_STAT_BY(STAT_ShaderCompiling_NumOpaqueMaterialShaders,1); break;
		case BLEND_Masked: INC_DWORD_STAT_BY(STAT_ShaderCompiling_NumMaskedMaterialShaders,1); break;
		default: INC_DWORD_STAT_BY(STAT_ShaderCompiling_NumTransparentMaterialShaders,1); break;
	}

	FMaterialShadingModelField ShadingModels = Material->GetShadingModels();
	
	if (ShadingModels.HasOnlyShadingModel(MSM_Unlit))
	{
		INC_DWORD_STAT_BY(STAT_ShaderCompiling_NumUnlitMaterialShaders, 1);
	}
	else if (ShadingModels.HasAnyShadingModel({ MSM_DefaultLit, MSM_Subsurface, MSM_PreintegratedSkin, MSM_ClearCoat, MSM_Cloth, MSM_SubsurfaceProfile, MSM_TwoSidedFoliage, MSM_SingleLayerWater, MSM_ThinTranslucent }))
	{
		INC_DWORD_STAT_BY(STAT_ShaderCompiling_NumLitMaterialShaders, 1);
	}


	if (Material->IsSpecialEngineMaterial())
	{
		INC_DWORD_STAT_BY(STAT_ShaderCompiling_NumSpecialMaterialShaders,1);
	}
	if (Material->IsUsedWithParticleSystem())
	{
		INC_DWORD_STAT_BY(STAT_ShaderCompiling_NumParticleMaterialShaders,1);
	}
	if (Material->IsUsedWithSkeletalMesh())
	{
		INC_DWORD_STAT_BY(STAT_ShaderCompiling_NumSkinnedMaterialShaders,1);
	}
}


const FStaticMaterialLayersParameter::ID FStaticMaterialLayersParameter::GetID() const
{
	ID Result;
	Result.ParameterID = *this;
	Result.Functions = Value.GetID();

	return Result;
}


UMaterialFunctionInterface* FStaticMaterialLayersParameter::GetParameterAssociatedFunction(const FHashedMaterialParameterInfo& InParameterInfo) const
{
	check(InParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter);

	// Grab the associated layer or blend
	UMaterialFunctionInterface* Function = nullptr;

	if (InParameterInfo.Association == EMaterialParameterAssociation::LayerParameter)
	{
		if (Value.Layers.IsValidIndex(InParameterInfo.Index))
		{
			Function = Value.Layers[InParameterInfo.Index];
		}
	}
	else if (InParameterInfo.Association == EMaterialParameterAssociation::BlendParameter)
	{
		if (Value.Blends.IsValidIndex(InParameterInfo.Index))
		{
			Function = Value.Blends[InParameterInfo.Index];
		}
	}

	return Function;
}

/** 
* Tests this set against another for equality, disregarding override settings.
* 
* @param ReferenceSet	The set to compare against
* @return				true if the sets are equal
*/
bool FStaticParameterSet::operator==(const FStaticParameterSet& ReferenceSet) const
{
	if (StaticSwitchParameters.Num() != ReferenceSet.StaticSwitchParameters.Num()
		|| StaticComponentMaskParameters.Num() != ReferenceSet.StaticComponentMaskParameters.Num()
		|| TerrainLayerWeightParameters.Num() != ReferenceSet.TerrainLayerWeightParameters.Num()
		|| MaterialLayersParameters.Num() != ReferenceSet.MaterialLayersParameters.Num())
	{
		return false;
	}

	if (StaticSwitchParameters != ReferenceSet.StaticSwitchParameters)
	{
		return false;
	}

	if (StaticComponentMaskParameters != ReferenceSet.StaticComponentMaskParameters)
	{
		return false;
	}

	if (TerrainLayerWeightParameters != ReferenceSet.TerrainLayerWeightParameters)
	{
		return false;
	}

	if (MaterialLayersParameters != ReferenceSet.MaterialLayersParameters)
	{
		return false;
	}

	return true;
}

void FStaticParameterSet::SortForEquivalent()
{
	StaticSwitchParameters.Sort([](const FStaticSwitchParameter& A, const FStaticSwitchParameter& B) { return B.ExpressionGUID < A.ExpressionGUID; });
	StaticComponentMaskParameters.Sort([](const FStaticComponentMaskParameter& A, const FStaticComponentMaskParameter& B) { return B.ExpressionGUID < A.ExpressionGUID; });
	TerrainLayerWeightParameters.Sort([](const FStaticTerrainLayerWeightParameter& A, const FStaticTerrainLayerWeightParameter& B) { return B.ExpressionGUID < A.ExpressionGUID; });
	MaterialLayersParameters.Sort([](const FStaticMaterialLayersParameter& A, const FStaticMaterialLayersParameter& B) { return B.ExpressionGUID < A.ExpressionGUID; });
}

bool FStaticParameterSet::Equivalent(const FStaticParameterSet& ReferenceSet) const
{
	if (StaticSwitchParameters.Num() == ReferenceSet.StaticSwitchParameters.Num()
		&& StaticComponentMaskParameters.Num() == ReferenceSet.StaticComponentMaskParameters.Num()
		&& TerrainLayerWeightParameters.Num() == ReferenceSet.TerrainLayerWeightParameters.Num()
		&& MaterialLayersParameters.Num() == ReferenceSet.MaterialLayersParameters.Num())
	{
		// this is not ideal, but it is easy to code up
		FStaticParameterSet Temp1 = *this;
		FStaticParameterSet Temp2 = ReferenceSet;
		Temp1.SortForEquivalent();
		Temp2.SortForEquivalent();
		bool bResult = (Temp1 == Temp2);
		ensure(!bResult || (*this) == ReferenceSet); // if this never fires, then we really didn't need to sort did we?
		return bResult;
	}
	return false;
}

void FMaterialShaderMapId::Serialize(FArchive& Ar, bool bLoadedByCookedMaterial)
{
	SCOPED_LOADTIMER(FMaterialShaderMapId_Serialize);

	// Note: FMaterialShaderMapId is saved both in packages (legacy UMaterialInstance) and the DDC (FMaterialShaderMap)
	// Backwards compatibility only works with FMaterialShaderMapId's stored in packages.  
	// Only serialized in legacy packages if UE4Ver() < VER_UE4_PURGED_FMATERIAL_COMPILE_OUTPUTS
	// You must bump MATERIALSHADERMAP_DERIVEDDATA_VER as well if changing the serialization of FMaterialShaderMapId.
	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);

	// Ensure saved content is correct
	check(!Ar.IsSaving() || IsContentValid());

#if WITH_EDITOR
	const bool bIsSavingCooked = Ar.IsSaving() && Ar.IsCooking();
	bIsCookedId = bLoadedByCookedMaterial;

	if (!bIsSavingCooked && !bLoadedByCookedMaterial)
	{
		uint32 UsageInt = Usage;
		Ar << UsageInt;
		Usage = (EMaterialShaderMapUsage::Type)UsageInt;

		Ar << BaseMaterialId;
	}
#endif

	if (Ar.UE4Ver() >= VER_UE4_PURGED_FMATERIAL_COMPILE_OUTPUTS)
	{
		Ar << (int32&)QualityLevel;
		Ar << (int32&)FeatureLevel;
	}
	else
	{
		uint8 LegacyQualityLevel;
		Ar << LegacyQualityLevel;
	}

#if WITH_EDITOR
	if (!bIsSavingCooked && !bLoadedByCookedMaterial)
	{
		Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);

		if (Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::MaterialShaderMapIdSerialization)
		{
			// Serialize using old path
			FStaticParameterSet ParameterSet;
			ParameterSet.Serialize(Ar);
			UpdateFromParameterSet(ParameterSet);
		}
		else
		{
			Ar << StaticSwitchParameters;
			Ar << StaticComponentMaskParameters;
			Ar << TerrainLayerWeightParameters;
			Ar << MaterialLayersParameterIDs;
		}

		Ar << ReferencedFunctions;

		if (Ar.UE4Ver() >= VER_UE4_COLLECTIONS_IN_SHADERMAPID)
		{
			Ar << ReferencedParameterCollections;
		}

		if (Ar.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::AddedMaterialSharedInputs &&
			Ar.CustomVer(FReleaseObjectVersion::GUID) < FReleaseObjectVersion::RemovedMaterialSharedInputCollection)
		{
			TArray<FGuid> Deprecated;
			Ar << Deprecated;
		}

		Ar << ShaderTypeDependencies;
		if (Ar.UE4Ver() >= VER_UE4_PURGED_FMATERIAL_COMPILE_OUTPUTS)
		{
			Ar << ShaderPipelineTypeDependencies;
		}
		Ar << VertexFactoryTypeDependencies;

		if (Ar.UE4Ver() >= VER_UE4_PURGED_FMATERIAL_COMPILE_OUTPUTS)
		{
			Ar << TextureReferencesHash;
		}
		else
		{
			FSHAHash LegacyHash;
			Ar << LegacyHash;
		}

		if (Ar.UE4Ver() >= VER_UE4_MATERIAL_INSTANCE_BASE_PROPERTY_OVERRIDES)
		{
			Ar << BasePropertyOverridesHash;
		}
	}
	else
	{
		if (bIsSavingCooked)
		{
			// Saving cooked data, this should be valid
			GetMaterialHash(CookedShaderMapIdHash);
			checkf(CookedShaderMapIdHash != FSHAHash(), TEXT("Tried to save an invalid shadermap id hash during cook"));
		}
		
		Ar << CookedShaderMapIdHash;
	}
#else
	// Cooked so can assume this is valid
	Ar << CookedShaderMapIdHash;
	checkf(CookedShaderMapIdHash != FSHAHash(), TEXT("Loaded an invalid cooked shadermap id hash"));
#endif // WITH_EDITOR

	// Based on the comment above, ShaderMapId would only be embedded into packages for archives < VER_UE4_PURGED_FMATERIAL_COMPILE_OUTPUTS (~2013 era).
	// Since backward compatibility only works for the packages, we can assume that any archive with a version newer than that will have
	// the LayoutParams serialized. The other option (old DDC) should be prevented by us having mutated MATERIALSHADERMAP_DERIVEDDATA_VER
	if (Ar.UE4Ver() >= VER_UE4_PURGED_FMATERIAL_COMPILE_OUTPUTS)
	{
		LayoutParams.Serialize(Ar);
	}
	else
	{
		LayoutParams.InitializeForCurrent();
	}

	// Ensure loaded content is correct
	check(!Ar.IsLoading() || IsContentValid());
}

#if WITH_EDITOR
/** Hashes the material-specific part of this shader map Id. */
void FMaterialShaderMapId::GetMaterialHash(FSHAHash& OutHash) const
{
	check(IsContentValid());
	FSHA1 HashState;

	HashState.Update((const uint8*)&Usage, sizeof(Usage));

	HashState.Update((const uint8*)&BaseMaterialId, sizeof(BaseMaterialId));

	FString QualityLevelString;
	GetMaterialQualityLevelName(QualityLevel, QualityLevelString);
	HashState.UpdateWithString(*QualityLevelString, QualityLevelString.Len());

	HashState.Update((const uint8*)&FeatureLevel, sizeof(FeatureLevel));


	//Hash the static parameters
	for (const FStaticSwitchParameter& StaticSwitchParameter : StaticSwitchParameters)
	{
		StaticSwitchParameter.UpdateHash(HashState);
	}
	for (const FStaticComponentMaskParameter& StaticComponentMaskParameter : StaticComponentMaskParameters)
	{
		StaticComponentMaskParameter.UpdateHash(HashState);
	}
	for (const FStaticTerrainLayerWeightParameter& StaticTerrainLayerWeightParameter : TerrainLayerWeightParameters)
	{
		StaticTerrainLayerWeightParameter.UpdateHash(HashState);
	}
	for (const FStaticMaterialLayersParameter::ID &LayerParameterID : MaterialLayersParameterIDs)
	{
		LayerParameterID.UpdateHash(HashState);
	}

	for (int32 FunctionIndex = 0; FunctionIndex < ReferencedFunctions.Num(); FunctionIndex++)
	{
		HashState.Update((const uint8*)&ReferencedFunctions[FunctionIndex], sizeof(ReferencedFunctions[FunctionIndex]));
	}

	for (int32 CollectionIndex = 0; CollectionIndex < ReferencedParameterCollections.Num(); CollectionIndex++)
	{
		HashState.Update((const uint8*)&ReferencedParameterCollections[CollectionIndex], sizeof(ReferencedParameterCollections[CollectionIndex]));
	}

	for (int32 VertexFactoryIndex = 0; VertexFactoryIndex < VertexFactoryTypeDependencies.Num(); VertexFactoryIndex++)
	{
		HashState.Update((const uint8*)&VertexFactoryTypeDependencies[VertexFactoryIndex].VFSourceHash, sizeof(VertexFactoryTypeDependencies[VertexFactoryIndex].VFSourceHash));
	}

	HashState.Update((const uint8*)&TextureReferencesHash, sizeof(TextureReferencesHash));

	HashState.Update((const uint8*)&BasePropertyOverridesHash, sizeof(BasePropertyOverridesHash));

	HashState.Final();
	HashState.GetHash(&OutHash.Hash[0]);
}
#endif // WITH_EDITOR

/** 
* Tests this set against another for equality.
* 
* @param ReferenceSet	The set to compare against
* @return				true if the sets are equal
*/
bool FMaterialShaderMapId::operator==(const FMaterialShaderMapId& ReferenceSet) const
{
	// Ensure data is in valid state for comparison
	check(IsContentValid() && ReferenceSet.IsContentValid());

#if WITH_EDITOR
	if (IsCookedId() != ReferenceSet.IsCookedId())
	{
		return false;
	}

	if (!IsCookedId())
	{
		if (Usage != ReferenceSet.Usage
			|| BaseMaterialId != ReferenceSet.BaseMaterialId)
		{
			return false;
		}
	}
	else
#endif
	if (CookedShaderMapIdHash != ReferenceSet.CookedShaderMapIdHash)
	{
		return false;
	}

	if (QualityLevel != ReferenceSet.QualityLevel
		|| FeatureLevel != ReferenceSet.FeatureLevel)
	{
		return false;
	}

	if (LayoutParams != ReferenceSet.LayoutParams)
	{
		return false;
	}

#if WITH_EDITOR
	if (!IsCookedId())
	{
		if (StaticSwitchParameters.Num() != ReferenceSet.StaticSwitchParameters.Num()
			|| StaticComponentMaskParameters.Num() != ReferenceSet.StaticComponentMaskParameters.Num()
			|| TerrainLayerWeightParameters.Num() != ReferenceSet.TerrainLayerWeightParameters.Num()
			|| MaterialLayersParameterIDs.Num() != ReferenceSet.MaterialLayersParameterIDs.Num()
			|| ReferencedFunctions.Num() != ReferenceSet.ReferencedFunctions.Num()
			|| ReferencedParameterCollections.Num() != ReferenceSet.ReferencedParameterCollections.Num()
			|| ShaderTypeDependencies.Num() != ReferenceSet.ShaderTypeDependencies.Num()
			|| ShaderPipelineTypeDependencies.Num() != ReferenceSet.ShaderPipelineTypeDependencies.Num()
			|| VertexFactoryTypeDependencies.Num() != ReferenceSet.VertexFactoryTypeDependencies.Num())
		{
			return false;
		}

		if (StaticSwitchParameters != ReferenceSet.StaticSwitchParameters
			|| StaticComponentMaskParameters != ReferenceSet.StaticComponentMaskParameters
			|| TerrainLayerWeightParameters != ReferenceSet.TerrainLayerWeightParameters
			|| MaterialLayersParameterIDs != ReferenceSet.MaterialLayersParameterIDs)
		{
			return false;
		}

		for (int32 RefFunctionIndex = 0; RefFunctionIndex < ReferenceSet.ReferencedFunctions.Num(); RefFunctionIndex++)
		{
			const FGuid& ReferenceGuid = ReferenceSet.ReferencedFunctions[RefFunctionIndex];

			if (ReferencedFunctions[RefFunctionIndex] != ReferenceGuid)
			{
				return false;
			}
		}

		for (int32 RefCollectionIndex = 0; RefCollectionIndex < ReferenceSet.ReferencedParameterCollections.Num(); RefCollectionIndex++)
		{
			const FGuid& ReferenceGuid = ReferenceSet.ReferencedParameterCollections[RefCollectionIndex];

			if (ReferencedParameterCollections[RefCollectionIndex] != ReferenceGuid)
			{
				return false;
			}
		}

		for (int32 ShaderIndex = 0; ShaderIndex < ShaderTypeDependencies.Num(); ShaderIndex++)
		{
			const FShaderTypeDependency& ShaderTypeDependency = ShaderTypeDependencies[ShaderIndex];

			if (ShaderTypeDependency != ReferenceSet.ShaderTypeDependencies[ShaderIndex])
			{
				return false;
			}
		}

		for (int32 ShaderPipelineIndex = 0; ShaderPipelineIndex < ShaderPipelineTypeDependencies.Num(); ShaderPipelineIndex++)
		{
			const FShaderPipelineTypeDependency& ShaderPipelineTypeDependency = ShaderPipelineTypeDependencies[ShaderPipelineIndex];

			if (ShaderPipelineTypeDependency != ReferenceSet.ShaderPipelineTypeDependencies[ShaderPipelineIndex])
			{
				return false;
			}
		}

		for (int32 VFIndex = 0; VFIndex < VertexFactoryTypeDependencies.Num(); VFIndex++)
		{
			const FVertexFactoryTypeDependency& VFDependency = VertexFactoryTypeDependencies[VFIndex];

			if (VFDependency != ReferenceSet.VertexFactoryTypeDependencies[VFIndex])
			{
				return false;
			}
		}

		if (TextureReferencesHash != ReferenceSet.TextureReferencesHash)
		{
			return false;
		}

		if( BasePropertyOverridesHash != ReferenceSet.BasePropertyOverridesHash )
		{
			return false;
		}
	}
#endif // WITH_EDITOR

	return true;
}

/** Ensure content is valid - for example overrides are set deterministically for serialization and sorting */
bool FMaterialShaderMapId::IsContentValid() const
{
#if WITH_EDITOR
	//We expect overrides to be set to false
	for (const FStaticSwitchParameter& StaticSwitchParameter : StaticSwitchParameters)
	{
		if (StaticSwitchParameter.bOverride != false)
		{
			return false;
		}
	}
	for (const FStaticComponentMaskParameter& StaticComponentMaskParameter : StaticComponentMaskParameters)
	{
		if (StaticComponentMaskParameter.bOverride != false)
		{
			return false;
		}
	}
	for (const FStaticTerrainLayerWeightParameter& StaticTerrainLayerWeightParameter : TerrainLayerWeightParameters)
{
		if (StaticTerrainLayerWeightParameter.bOverride != false)
		{
			return false;
		}
	}
	for (const FStaticMaterialLayersParameter::ID &LayerParameterID : MaterialLayersParameterIDs)
	{
		if (LayerParameterID.ParameterID.bOverride != false)
		{
			return false;
		}
	}
#endif // WITH_EDITOR
	return true;
}


#if WITH_EDITOR
void FMaterialShaderMapId::UpdateFromParameterSet(const FStaticParameterSet& StaticParameters)
{
	StaticSwitchParameters = StaticParameters.StaticSwitchParameters;
	StaticComponentMaskParameters = StaticParameters.StaticComponentMaskParameters;
	TerrainLayerWeightParameters = StaticParameters.TerrainLayerWeightParameters;

	MaterialLayersParameterIDs.SetNum(StaticParameters.MaterialLayersParameters.Num());
	for (int i = 0; i < StaticParameters.MaterialLayersParameters.Num(); ++i)
	{
		MaterialLayersParameterIDs[i] = StaticParameters.MaterialLayersParameters[i].GetID();
	}

	//since bOverrides aren't used to check id matches, make sure they're consistently set to false in the static parameter set as part of the id.
	//this ensures deterministic cook results, rather than allowing bOverride to be set in the shader map's copy of the id based on the first id used.
	for (FStaticSwitchParameter& StaticSwitchParameter : StaticSwitchParameters)
	{
		StaticSwitchParameter.bOverride = false;
	}
	for (FStaticComponentMaskParameter& StaticComponentMaskParameter : StaticComponentMaskParameters)
	{
		StaticComponentMaskParameter.bOverride = false;
	}
	for (FStaticTerrainLayerWeightParameter& StaticTerrainLayerWeightParameter : TerrainLayerWeightParameters)
	{
		StaticTerrainLayerWeightParameter.bOverride = false;
	}
	for (FStaticMaterialLayersParameter::ID &LayerParameterID : MaterialLayersParameterIDs)
	{
		LayerParameterID.ParameterID.bOverride = false;
	}
}	
#endif // WITH_EDITOR

#if WITH_EDITOR
void FMaterialShaderMapId::AppendKeyString(FString& KeyString) const
{
	check(IsContentValid());
	KeyString += BaseMaterialId.ToString();
	KeyString += TEXT("_");

	FString QualityLevelName;
	GetMaterialQualityLevelName(QualityLevel, QualityLevelName);
	KeyString += QualityLevelName + TEXT("_");

	FString FeatureLevelString;
	GetFeatureLevelName(FeatureLevel, FeatureLevelString);
	KeyString += FeatureLevelString + TEXT("_");

	LayoutParams.AppendKeyString(KeyString);

	for (const FStaticSwitchParameter& StaticSwitchParameter : StaticSwitchParameters)
	{
		StaticSwitchParameter.AppendKeyString(KeyString);
	}
	for (const FStaticComponentMaskParameter& StaticComponentMaskParameter : StaticComponentMaskParameters)
	{
		StaticComponentMaskParameter.AppendKeyString(KeyString);
	}
	for (const FStaticTerrainLayerWeightParameter& StaticTerrainLayerWeightParameter : TerrainLayerWeightParameters)
	{
		StaticTerrainLayerWeightParameter.AppendKeyString(KeyString);
	}
	for (const FStaticMaterialLayersParameter::ID &LayerParameterID : MaterialLayersParameterIDs)
	{
		LayerParameterID.AppendKeyString(KeyString);
	}

	KeyString += TEXT("_");
	KeyString += FString::FromInt(Usage);
	KeyString += TEXT("_");

	// Add any referenced functions to the key so that we will recompile when they are changed
	for (int32 FunctionIndex = 0; FunctionIndex < ReferencedFunctions.Num(); FunctionIndex++)
	{
		KeyString += ReferencedFunctions[FunctionIndex].ToString();
	}

	{
		const FSHAHash LayoutHash = Freeze::HashLayout(StaticGetTypeLayoutDesc<FMaterialShaderMapContent>(), LayoutParams);
		KeyString += TEXT("_");
		KeyString += LayoutHash.ToString();
		KeyString += TEXT("_");
	}

	KeyString += TEXT("_");

	for (int32 CollectionIndex = 0; CollectionIndex < ReferencedParameterCollections.Num(); CollectionIndex++)
	{
		KeyString += ReferencedParameterCollections[CollectionIndex].ToString();
	}

	TMap<const TCHAR*,FCachedUniformBufferDeclaration> ReferencedUniformBuffers;

	// Add the inputs for any shaders that are stored inline in the shader map
	for (int32 ShaderIndex = 0; ShaderIndex < ShaderTypeDependencies.Num(); ShaderIndex++)
	{
		const FShaderTypeDependency& ShaderTypeDependency = ShaderTypeDependencies[ShaderIndex];
		const FShaderType* ShaderType = FindShaderTypeByName(ShaderTypeDependency.ShaderTypeName);

		KeyString += TEXT("_");
		KeyString += ShaderType->GetName();
		KeyString += ShaderTypeDependency.SourceHash.ToString();

		const FSHAHash LayoutHash = Freeze::HashLayout(ShaderType->GetLayout(), LayoutParams);
		KeyString += LayoutHash.ToString();

		const TMap<const TCHAR*,FCachedUniformBufferDeclaration>& ReferencedUniformBufferStructsCache = ShaderType->GetReferencedUniformBufferStructsCache();

		for (TMap<const TCHAR*,FCachedUniformBufferDeclaration>::TConstIterator It(ReferencedUniformBufferStructsCache); It; ++It)
		{
			ReferencedUniformBuffers.Add(It.Key(), It.Value());
		}
	}

	// Add the inputs for any shader pipelines that are stored inline in the shader map
	for (int32 TypeIndex = 0; TypeIndex < ShaderPipelineTypeDependencies.Num(); TypeIndex++)
	{
		const FShaderPipelineTypeDependency& Dependency = ShaderPipelineTypeDependencies[TypeIndex];
		const FShaderPipelineType* ShaderPipelineType = FShaderPipelineType::GetShaderPipelineTypeByName(Dependency.ShaderPipelineTypeName);

		KeyString += TEXT("_");
		KeyString += ShaderPipelineType->GetName();
		KeyString += Dependency.StagesSourceHash.ToString();

		for (const FShaderType* ShaderType : ShaderPipelineType->GetStages())
		{
			const TMap<const TCHAR*, FCachedUniformBufferDeclaration>& ReferencedUniformBufferStructsCache = ShaderType->GetReferencedUniformBufferStructsCache();

			// Gather referenced uniform buffers
			for (TMap<const TCHAR*, FCachedUniformBufferDeclaration>::TConstIterator It(ReferencedUniformBufferStructsCache); It; ++It)
			{
				ReferencedUniformBuffers.Add(It.Key(), It.Value());
			}
		}
	}

	// Add the inputs for any shaders that are stored inline in the shader map
	for (int32 VFIndex = 0; VFIndex < VertexFactoryTypeDependencies.Num(); VFIndex++)
	{
		KeyString += TEXT("_");

		const FVertexFactoryTypeDependency& VFDependency = VertexFactoryTypeDependencies[VFIndex];
		const FVertexFactoryType* VertexFactoryType = FVertexFactoryType::GetVFByName(VFDependency.VertexFactoryTypeName);

		KeyString += VertexFactoryType->GetName();
		KeyString += VFDependency.VFSourceHash.ToString();

		for (int32 Frequency = 0; Frequency < SF_NumFrequencies; Frequency++)
		{
			const FTypeLayoutDesc* ParameterLayout = VertexFactoryType->GetShaderParameterLayout((EShaderFrequency)Frequency);
			if (ParameterLayout)
			{
				const FSHAHash LayoutHash = Freeze::HashLayout(*ParameterLayout, LayoutParams);
				KeyString += LayoutHash.ToString();
			}
		}

		const TMap<const TCHAR*,FCachedUniformBufferDeclaration>& ReferencedUniformBufferStructsCache = VertexFactoryType->GetReferencedUniformBufferStructsCache();

		for (TMap<const TCHAR*,FCachedUniformBufferDeclaration>::TConstIterator It(ReferencedUniformBufferStructsCache); It; ++It)
		{
			ReferencedUniformBuffers.Add(It.Key(), It.Value());
		}
	}

	{
		TArray<uint8> TempData;
		FSerializationHistory SerializationHistory;
		FMemoryWriter Ar(TempData, true);
		FShaderSaveArchive SaveArchive(Ar, SerializationHistory);

		// Save uniform buffer member info so we can detect when layout has changed
		SerializeUniformBufferInfo(SaveArchive, ReferencedUniformBuffers);

		SerializationHistory.AppendKeyString(KeyString);
	}

	KeyString += BytesToHex(&TextureReferencesHash.Hash[0], sizeof(TextureReferencesHash.Hash));

	KeyString += BytesToHex(&BasePropertyOverridesHash.Hash[0], sizeof(BasePropertyOverridesHash.Hash));
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void FMaterialShaderMapId::SetShaderDependencies(const TArray<FShaderType*>& ShaderTypes, const TArray<const FShaderPipelineType*>& ShaderPipelineTypes, const TArray<FVertexFactoryType*>& VFTypes, EShaderPlatform ShaderPlatform)
{
	if (!FPlatformProperties::RequiresCookedData())
	{
		for (int32 ShaderTypeIndex = 0; ShaderTypeIndex < ShaderTypes.Num(); ShaderTypeIndex++)
		{
			FShaderTypeDependency Dependency;
			Dependency.ShaderTypeName = ShaderTypes[ShaderTypeIndex]->GetHashedName();
			Dependency.SourceHash = ShaderTypes[ShaderTypeIndex]->GetSourceHash(ShaderPlatform);
			ShaderTypeDependencies.Add(Dependency);
		}

		for (int32 VFTypeIndex = 0; VFTypeIndex < VFTypes.Num(); VFTypeIndex++)
		{
			FVertexFactoryTypeDependency Dependency;
			Dependency.VertexFactoryTypeName = VFTypes[VFTypeIndex]->GetHashedName();
			Dependency.VFSourceHash = VFTypes[VFTypeIndex]->GetSourceHash(ShaderPlatform);
			VertexFactoryTypeDependencies.Add(Dependency);
		}

		for (int32 TypeIndex = 0; TypeIndex < ShaderPipelineTypes.Num(); TypeIndex++)
		{
			const FShaderPipelineType* Pipeline = ShaderPipelineTypes[TypeIndex];
			FShaderPipelineTypeDependency Dependency;
			Dependency.ShaderPipelineTypeName = Pipeline->GetHashedName();
			Dependency.StagesSourceHash = Pipeline->GetSourceHash(ShaderPlatform);
			ShaderPipelineTypeDependencies.Add(Dependency);
		}
	}
}
#endif // WITH_EDITOR

/**
 * Enqueues a compilation for a new shader of this type.
 * @param Material - The material to link the shader with.
 */
FShaderCompileJob* FMaterialShaderType::BeginCompileShader(
	uint32 ShaderMapId,
	int32 PermutationId,
	const FMaterial* Material,
	FShaderCompilerEnvironment* MaterialEnvironment,
	const FShaderPipelineType* ShaderPipeline,
	EShaderPlatform Platform,
	TArray<TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe>>& NewJobs,
	const FString& DebugDescription,
	const FString& DebugExtension
	)
{
	FShaderCompileJob* NewJob = new FShaderCompileJob(ShaderMapId, nullptr, this, PermutationId);

	NewJob->Input.SharedEnvironment = MaterialEnvironment;
	FShaderCompilerEnvironment& ShaderEnvironment = NewJob->Input.Environment;

	UE_LOG(LogShaders, Verbose, TEXT("			%s"), GetName());
	COOK_STAT(MaterialShaderCookStats::ShadersCompiled++);

	//update material shader stats
	UpdateMaterialShaderCompilingStats(Material);

	Material->SetupExtaCompilationSettings(Platform, NewJob->Input.ExtraSettings);

	// Allow the shader type to modify the compile environment.
	SetupCompileEnvironment(Platform, Material, PermutationId, ShaderEnvironment);

	// Compile the shader environment passed in with the shader type's source code.
	::GlobalBeginCompileShader(
		Material->GetFriendlyName(),
		nullptr,
		this,
		ShaderPipeline,
		GetShaderFilename(),
		GetFunctionName(),
		FShaderTarget(GetFrequency(),Platform),
		TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe>(NewJob),
		NewJobs,
		true,
		DebugDescription,
		DebugExtension
		);
	return NewJob;
}

void FMaterialShaderType::BeginCompileShaderPipeline(
	uint32 ShaderMapId,
	EShaderPlatform Platform,
	const FMaterial* Material,
	FShaderCompilerEnvironment* MaterialEnvironment,
	const FShaderPipelineType* ShaderPipeline,
	const TArray<const FShaderType*>& ShaderStages,
	TArray<TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe>>& NewJobs,
	const FString& DebugDescription,
	const FString& DebugExtension)
{
	check(ShaderStages.Num() > 0);
	check(ShaderPipeline);
	UE_LOG(LogShaders, Verbose, TEXT("	Pipeline: %s"), ShaderPipeline->GetName());

	// Add all the jobs as individual first, then add the dependencies into a pipeline job
	auto* NewPipelineJob = new FShaderPipelineCompileJob(ShaderMapId, ShaderPipeline, ShaderStages.Num());
	for (int32 Index = 0; Index < ShaderStages.Num(); ++Index)
	{
		FMaterialShaderType* ShaderStage = const_cast<FMaterialShaderType*>(static_cast<const FMaterialShaderType*>(ShaderStages[Index]));
		ShaderStage->BeginCompileShader(ShaderMapId, kUniqueShaderPermutationId, Material, MaterialEnvironment, ShaderPipeline, Platform, NewPipelineJob->StageJobs, DebugDescription, DebugExtension);
	}

	NewJobs.Add(TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe>(NewPipelineJob));
}

/**
 * Either creates a new instance of this type or returns an equivalent existing shader.
 * @param Material - The material to link the shader with.
 * @param CurrentJob - Compile job that was enqueued by BeginCompileShader.
 */
FShader* FMaterialShaderType::FinishCompileShader(
	const FUniformExpressionSet& UniformExpressionSet,
	const FSHAHash& MaterialShaderMapHash,
	const FShaderCompileJob& CurrentJob,
	const FShaderPipelineType* ShaderPipelineType,
	const FString& InDebugDescription
	)
{
	check(CurrentJob.bSucceeded);

	if (ShaderPipelineType && !ShaderPipelineType->ShouldOptimizeUnusedOutputs(CurrentJob.Input.Target.GetPlatform()))
	{
		// If sharing shaders in this pipeline, remove it from the type/id so it uses the one in the shared shadermap list
		ShaderPipelineType = nullptr;
	}

	FShader* Shader = ConstructCompiled(CompiledShaderInitializerType(this, CurrentJob.PermutationId, CurrentJob.Output, UniformExpressionSet, MaterialShaderMapHash, ShaderPipelineType, nullptr, InDebugDescription));
	CurrentJob.Output.ParameterMap.VerifyBindingsAreComplete(GetName(), CurrentJob.Output.Target, CurrentJob.VFType);

	return Shader;
}

bool FMaterialShaderType::ShouldCompilePermutation(EShaderPlatform Platform, const FMaterialShaderParameters& MaterialParameters, int32 PermutationId) const
{
	return FShaderType::ShouldCompilePermutation(FMaterialShaderPermutationParameters(Platform, MaterialParameters, PermutationId));
}

bool FMaterialShaderType::ShouldCompilePipeline(const FShaderPipelineType* ShaderPipelineType, EShaderPlatform Platform, const FMaterialShaderParameters& MaterialParameters)
{
	const FMaterialShaderPermutationParameters Parameters(Platform, MaterialParameters, kUniqueShaderPermutationId);
	for (const FShaderType* ShaderType : ShaderPipelineType->GetStages())
	{
		checkSlow(ShaderType->GetMaterialShaderType());
		if (!ShaderType->ShouldCompilePermutation(Parameters))
		{
			return false;
		}
	}
	return true;
}

void FMaterialShaderType::SetupCompileEnvironment(EShaderPlatform Platform, const FMaterialShaderParameters& MaterialParameters, int32 PermutationId, FShaderCompilerEnvironment& Environment)
{
	// Allow the shader type to modify its compile environment.
	ModifyCompilationEnvironment(FMaterialShaderPermutationParameters(Platform, MaterialParameters, PermutationId), Environment);
}

/**
* Finds the shader map for a material.
* @param StaticParameterSet - The static parameter set identifying the shader map
* @param Platform - The platform to lookup for
* @return NULL if no cached shader map was found.
*/
TRefCountPtr<FMaterialShaderMap> FMaterialShaderMap::FindId(const FMaterialShaderMapId& ShaderMapId, EShaderPlatform InPlatform)
{
	FScopeLock ScopeLock(&GIdToMaterialShaderMapCS);
	check(ShaderMapId.IsValid());
	TRefCountPtr<FMaterialShaderMap> Result = GIdToMaterialShaderMap[InPlatform].FindRef(ShaderMapId);
	check(Result == nullptr || (!Result->bDeletedThroughDeferredCleanup && Result->bRegistered) );
	return Result;
}

#if ALLOW_SHADERMAP_DEBUG_DATA
/** Flushes the given shader types from any loaded FMaterialShaderMap's. */
void FMaterialShaderMap::FlushShaderTypes(TArray<const FShaderType*>& ShaderTypesToFlush, TArray<const FShaderPipelineType*>& ShaderPipelineTypesToFlush, TArray<const FVertexFactoryType*>& VFTypesToFlush)
{
	for (int32 ShaderMapIndex = 0; ShaderMapIndex < AllMaterialShaderMaps.Num(); ShaderMapIndex++)
	{
		FMaterialShaderMap* CurrentShaderMap = AllMaterialShaderMaps[ShaderMapIndex];

		for (int32 ShaderTypeIndex = 0; ShaderTypeIndex < ShaderTypesToFlush.Num(); ShaderTypeIndex++)
		{
			CurrentShaderMap->FlushShadersByShaderType(ShaderTypesToFlush[ShaderTypeIndex]);
		}
		for (int32 VFTypeIndex = 0; VFTypeIndex < VFTypesToFlush.Num(); VFTypeIndex++)
		{
			CurrentShaderMap->FlushShadersByVertexFactoryType(VFTypesToFlush[VFTypeIndex]);
		}
		for (int32 TypeIndex = 0; TypeIndex < ShaderPipelineTypesToFlush.Num(); TypeIndex++)
		{
			CurrentShaderMap->FlushShadersByShaderPipelineType(ShaderPipelineTypesToFlush[TypeIndex]);
		}
	}
}
#endif

#if WITH_EDITOR
void FMaterialShaderMap::GetAllOutdatedTypes(TArray<const FShaderType*>& OutdatedShaderTypes, TArray<const FShaderPipelineType*>& OutdatedShaderPipelineTypes, TArray<const FVertexFactoryType*>& OutdatedFactoryTypes)
{
#if ALLOW_SHADERMAP_DEBUG_DATA
	for (const FMaterialShaderMap* ShaderMap : AllMaterialShaderMaps)
	{
		ShaderMap->GetOutdatedTypes(OutdatedShaderTypes, OutdatedShaderPipelineTypes, OutdatedFactoryTypes);
	}
#endif
}

void FMaterialShaderMap::LoadFromDerivedDataCache(const FMaterial* Material, const FMaterialShaderMapId& ShaderMapId, EShaderPlatform InPlatform, const ITargetPlatform* TargetPlatform, TRefCountPtr<FMaterialShaderMap>& InOutShaderMap)
{
	if (InOutShaderMap != NULL)
	{
		check(InOutShaderMap->GetShaderPlatform() == InPlatform);
		// If the shader map was non-NULL then it was found in memory but is incomplete, attempt to load the missing entries from memory
		InOutShaderMap->LoadMissingShadersFromMemory(Material);
	}
	else
	{
		// Shader map was not found in memory, try to load it from the DDC
		STAT(double MaterialDDCTime = 0);
		{
			SCOPE_SECONDS_COUNTER(MaterialDDCTime);
			COOK_STAT(auto Timer = MaterialShaderCookStats::UsageStats.TimeSyncWork());

			TArray<uint8> CachedData;
			const FString DataKey = GetMaterialShaderMapKeyString(ShaderMapId, InPlatform, TargetPlatform);

			bool CheckCache = true;

			// If NoShaderDDC then don't check for a material the first time we encounter it to simulate
			// a cold DDC
			static bool bNoShaderDDC = FParse::Param(FCommandLine::Get(), TEXT("noshaderddc"));

			if (bNoShaderDDC)
			{
				static TSet<uint32> SeenKeys;

				const uint32 KeyHash = GetTypeHash(DataKey);

				if (!SeenKeys.Contains(KeyHash))
				{
					CheckCache = false;
					SeenKeys.Add(KeyHash);
				}
			}

			if (CheckCache && GetDerivedDataCacheRef().GetSynchronous(*DataKey, CachedData, Material->GetFriendlyName()))
			{
				COOK_STAT(Timer.AddHit(CachedData.Num()));
				InOutShaderMap = new FMaterialShaderMap();
				FMemoryReader Ar(CachedData, true);

				// Deserialize from the cached data
				InOutShaderMap->Serialize(Ar);
				//InOutShaderMap->RegisterSerializedShaders(false);
		
				const FString InDataKey = GetMaterialShaderMapKeyString(InOutShaderMap->GetShaderMapId(), InPlatform, TargetPlatform);
				checkSlow(InOutShaderMap->GetShaderMapId() == ShaderMapId);

				// Register in the global map
				InOutShaderMap->Register(InPlatform);
			}
			else
			{
				// We should be build the data later, and we can track that the resource was built there when we push it to the DDC.
				COOK_STAT(Timer.TrackCyclesOnly());
				InOutShaderMap = nullptr;
			}
		}
		INC_FLOAT_STAT_BY(STAT_ShaderCompiling_DDCLoading,(float)MaterialDDCTime);
	}
}

void FMaterialShaderMap::SaveToDerivedDataCache(const ITargetPlatform* TargetPlatform)
{
	COOK_STAT(auto Timer = MaterialShaderCookStats::UsageStats.TimeSyncWork());
	TArray<uint8> SaveData;
	FMemoryWriter Ar(SaveData, true);
	Serialize(Ar);

	GetDerivedDataCacheRef().Put(*GetMaterialShaderMapKeyString(ShaderMapId, GetShaderPlatform(), TargetPlatform), SaveData, FStringView(GetFriendlyName()));
	COOK_STAT(Timer.AddMiss(SaveData.Num()));
}
#endif // WITH_EDITOR

TArray<uint8>* FMaterialShaderMap::BackupShadersToMemory()
{
	TArray<uint8>* SavedShaderData = new TArray<uint8>();
#if 0
	FMemoryWriter Ar(*SavedShaderData);
	
	for (FMeshMaterialShaderMap* MeshShaderMap : Content->OrderedMeshShaderMaps)
	{
		if (MeshShaderMap)
		{
			// Serialize data needed to handle shader key changes in between the save and the load of the FShaders
			const bool bHandleShaderKeyChanges = true;
			MeshShaderMap->SerializeInline(Ar, true, bHandleShaderKeyChanges, false);
			MeshShaderMap->Empty();
		}
	}

	Content->SerializeInline(Ar, true, true, false);
	Content->Empty();
#endif
	check(false);
	return SavedShaderData;
}

void FMaterialShaderMap::RestoreShadersFromMemory(const TArray<uint8>& ShaderData)
{
	check(false);
#if 0
	FMemoryReader Ar(ShaderData);

	for(FMeshMaterialShaderMap* MeshShaderMap : Content->OrderedMeshShaderMaps)
	{
		if (MeshShaderMap)
		{
			// Use the serialized shader key data to detect when the saved shader is no longer valid and skip it
			const bool bHandleShaderKeyChanges = true;
			MeshShaderMap->SerializeInline(Ar, true, bHandleShaderKeyChanges, false);
			//MeshShaderMaps[Index].RegisterSerializedShaders(false);
		}
	}

	Content->SerializeInline(Ar, true, true, false);
	//RegisterSerializedShaders(false);
#endif
}

void FMaterialShaderMap::SaveForRemoteRecompile(FArchive& Ar, const TMap<FString, TArray<TRefCountPtr<FMaterialShaderMap> > >& CompiledShaderMaps)
{
	// now we serialize a map (for each material), but without inline the resources, since they are above
	int32 MapSize = CompiledShaderMaps.Num();
	Ar << MapSize;

	for (TMap<FString, TArray<TRefCountPtr<FMaterialShaderMap> > >::TConstIterator It(CompiledShaderMaps); It; ++It)
	{
		const TArray<TRefCountPtr<FMaterialShaderMap> >& ShaderMapArray = It.Value();

		FString MaterialName = It.Key();
		Ar << MaterialName;

		int32 NumShaderMaps = ShaderMapArray.Num();
		Ar << NumShaderMaps;

		for (int32 Index = 0; Index < ShaderMapArray.Num(); Index++)
		{
			FMaterialShaderMap* ShaderMap = ShaderMapArray[Index];
			if (ShaderMap)
			{
				uint8 bIsValid = 1;
				Ar << bIsValid;
				ShaderMap->Serialize(Ar, false);
			}
			else
			{
				uint8 bIsValid = 0;
				Ar << bIsValid;
			}
		}
	}
}

void FMaterialShaderMap::LoadForRemoteRecompile(FArchive& Ar, EShaderPlatform ShaderPlatform, const TArray<FString>& MaterialsForShaderMaps)
{
	int32 MapSize;
	Ar << MapSize;

	for (int32 MaterialIndex = 0; MaterialIndex < MapSize; MaterialIndex++)
	{
		FString MaterialName;
		Ar << MaterialName;

		UMaterialInterface* MatchingMaterial = FindObjectChecked<UMaterialInterface>(NULL, *MaterialName);

		int32 NumShaderMaps = 0;
		Ar << NumShaderMaps;

		TArray<TRefCountPtr<FMaterialShaderMap> > LoadedShaderMaps;

		for (int32 ShaderMapIndex = 0; ShaderMapIndex < NumShaderMaps; ShaderMapIndex++)
		{
			uint8 bIsValid = 0;
			Ar << bIsValid;

			if (bIsValid)
			{
				FMaterialShaderMap* ShaderMap = new FMaterialShaderMap();

				// serialize the id and the material shader map
				ShaderMap->Serialize(Ar, false);

				// Register in the global map
				ShaderMap->Register(ShaderPlatform);

				LoadedShaderMaps.Add(ShaderMap);
			}
		}

		// Assign in two passes: first pass for shader maps with unspecified quality levels,
		// Second pass for shader maps with a specific quality level
		for (int32 PassIndex = 0; PassIndex < 2; PassIndex++)
		{
			for (int32 ShaderMapIndex = 0; ShaderMapIndex < LoadedShaderMaps.Num(); ShaderMapIndex++)
			{
				FMaterialShaderMap* LoadedShaderMap = LoadedShaderMaps[ShaderMapIndex];

				if (LoadedShaderMap->GetShaderPlatform() == ShaderPlatform 
					&& LoadedShaderMap->GetShaderMapId().FeatureLevel == GetMaxSupportedFeatureLevel(ShaderPlatform))
				{
					EMaterialQualityLevel::Type LoadedQualityLevel = LoadedShaderMap->GetShaderMapId().QualityLevel;

					for (int32 QualityLevelIndex = 0; QualityLevelIndex < EMaterialQualityLevel::Num; QualityLevelIndex++)
					{
						// First pass: assign shader maps with unspecified quality levels to all material resources
						if ((PassIndex == 0 && LoadedQualityLevel == EMaterialQualityLevel::Num)
							// Second pass: assign shader maps with a specified quality level to only the appropriate material resource
							|| (PassIndex == 1 && QualityLevelIndex == LoadedQualityLevel))
						{
							FMaterialResource* MaterialResource = MatchingMaterial->GetMaterialResource(GetMaxSupportedFeatureLevel(ShaderPlatform), (EMaterialQualityLevel::Type)QualityLevelIndex);

							MaterialResource->SetGameThreadShaderMap(LoadedShaderMap);
							MaterialResource->RegisterInlineShaderMap(false);
						}
					}
				}
			}
		}
	}
}

void FMaterialShaderMap::FinalizeContent()
{
	FMaterialShaderMapContent* LocalContent = GetMutableContent();
	const FShaderMapResourceCode* LocalCode = GetResourceCode();

	FSHA1 Hasher;
	LocalContent->Finalize(LocalCode);
	LocalContent->UpdateHash(Hasher);

	for (FMeshMaterialShaderMap* MeshShaderMap : LocalContent->OrderedMeshShaderMaps)
	{
		MeshShaderMap->Finalize(LocalCode);
		MeshShaderMap->UpdateHash(Hasher);
	}

	Hasher.Final();
	Hasher.GetHash(LocalContent->ShaderContentHash.Hash);

	FShaderMapBase::FinalizeContent();
}


/**
* Compiles the shaders for a material and caches them in this shader map.
* @param Material - The material to compile shaders for.
* @param InShaderMapId - the set of static parameters to compile for
* @param Platform - The platform to compile to
*/
void FMaterialShaderMap::Compile(
	FMaterial* Material,
	const FMaterialShaderMapId& InShaderMapId, 
	TRefCountPtr<FShaderCompilerEnvironment> MaterialEnvironment,
	const FMaterialCompilationOutput& InMaterialCompilationOutput,
	EShaderPlatform InPlatform,
	bool bSynchronousCompile)
{
	if (FPlatformProperties::RequiresCookedData())
	{
		UE_LOG(LogMaterial, Fatal, TEXT("Trying to compile %s at run-time, which is not supported on consoles!"), *Material->GetFriendlyName() );
	}
	else
	{
		check(!Material->bContainsInlineShaders);
  
		// Make sure we are operating on a referenced shader map or the below Find will cause this shader map to be deleted,
		// Since it creates a temporary ref counted pointer.
		check(NumRefs > 0);
  
		// Add this shader map and material resource to ShaderMapsBeingCompiled
		TArray<FMaterial*>* CorrespondingMaterials = ShaderMapsBeingCompiled.Find(this);
  
		if (CorrespondingMaterials)
		{
			check(!bSynchronousCompile);
			CorrespondingMaterials->AddUnique(Material);
		}
		else
		{
			// Assign a unique identifier so that shaders from this shader map can be associated with it after a deferred compile
			CompilingId = FShaderCommonCompileJob::GetNextJobId();
  
			TArray<FMaterial*> NewCorrespondingMaterials;
			NewCorrespondingMaterials.Add(Material);
			ShaderMapsBeingCompiled.Add(this, NewCorrespondingMaterials);
#if DEBUG_INFINITESHADERCOMPILE
			UE_LOG(LogTemp, Display, TEXT("Added material ShaderMap 0x%08X%08X with Material 0x%08X%08X to ShaderMapsBeingCompiled"), (int)((int64)(this) >> 32), (int)((int64)(this)), (int)((int64)(Material) >> 32), (int)((int64)(Material)));
#endif  
  
			FMaterialShaderMapContent* NewContent = new FMaterialShaderMapContent(InPlatform);
			NewContent->MaterialCompilationOutput = InMaterialCompilationOutput;
			AssignContent(NewContent);

			ShaderMapId = InShaderMapId;
			bIsPersistent = Material->IsPersistent();

			// Setup the material compilation environment.
			{
				FShaderParametersMetadata* UniformBufferStruct = NewContent->MaterialCompilationOutput.UniformExpressionSet.CreateBufferStruct();
				Material->SetupMaterialEnvironment(InPlatform, *UniformBufferStruct, InMaterialCompilationOutput.UniformExpressionSet, *MaterialEnvironment);
				delete UniformBufferStruct;
			}
 
#if ALLOW_SHADERMAP_DEBUG_DATA && WITH_EDITOR
			// Store the material name for debugging purposes.
			// Note: Material instances with static parameters will have the same FriendlyName for their shader maps!
			NewContent->FriendlyName = Material->GetFriendlyName();
			NewContent->MaterialPath = Material->GetBaseMaterialPathName();

			GetMutableContent()->MaterialPath = FMemoryImageString(Material->GetBaseMaterialPathName());

			// Log debug information about the material being compiled.
			const FString MaterialUsage = Material->GetMaterialUsageDescription();
			FString WorkingDebugDescription = FString::Printf(
				TEXT("Compiling %s: Platform=%s, Usage=%s"),
				*NewContent->FriendlyName,
				*LegacyShaderPlatformToShaderFormat(InPlatform).ToString(),
				*MaterialUsage
			);
			for (int32 StaticSwitchIndex = 0; StaticSwitchIndex < ShaderMapId.GetStaticSwitchParameters().Num(); ++StaticSwitchIndex)
			{
				const FStaticSwitchParameter& StaticSwitchParameter = ShaderMapId.GetStaticSwitchParameters()[StaticSwitchIndex];
				WorkingDebugDescription += FString::Printf(
					TEXT(", StaticSwitch'%s'=%s"),
					*StaticSwitchParameter.ParameterInfo.ToString(),
					StaticSwitchParameter.Value ? TEXT("True") : TEXT("False")
				);
			}
			for (int32 StaticMaskIndex = 0; StaticMaskIndex < ShaderMapId.GetStaticComponentMaskParameters().Num(); ++StaticMaskIndex)
			{
				const FStaticComponentMaskParameter& StaticComponentMaskParameter = ShaderMapId.GetStaticComponentMaskParameters()[StaticMaskIndex];
				WorkingDebugDescription += FString::Printf(
					TEXT(", StaticMask'%s'=%s%s%s%s"),
					*StaticComponentMaskParameter.ParameterInfo.ToString(),
					StaticComponentMaskParameter.R ? TEXT("R") : TEXT(""),
					StaticComponentMaskParameter.G ? TEXT("G") : TEXT(""),
					StaticComponentMaskParameter.B ? TEXT("B") : TEXT(""),
					StaticComponentMaskParameter.A ? TEXT("A") : TEXT("")
				);
			}
			for (int32 StaticLayerIndex = 0; StaticLayerIndex < ShaderMapId.GetTerrainLayerWeightParameters().Num(); ++StaticLayerIndex)
			{
				const FStaticTerrainLayerWeightParameter& StaticTerrainLayerWeightParameter = ShaderMapId.GetTerrainLayerWeightParameters()[StaticLayerIndex];
				WorkingDebugDescription += FString::Printf(
					TEXT(", StaticTerrainLayer'%s'=%s"),
					*StaticTerrainLayerWeightParameter.ParameterInfo.ToString(),
					*FString::Printf(TEXT("Weightmap%u"), StaticTerrainLayerWeightParameter.WeightmapIndex)
				);
			}
			for (const auto &LayerParameterID : ShaderMapId.GetMaterialLayersParameterIDs())
			{
				FString UUIDs;
				UUIDs += TEXT("Layers:");
				bool StartWithComma = false;
				for (const auto &Layer : LayerParameterID.Functions.LayerIDs)
				{
					UUIDs += (StartWithComma ? TEXT(", ") : TEXT("")) + Layer.ToString();
					StartWithComma = true;
				}
				UUIDs += TEXT(", Blends:");
				StartWithComma = false;
				for (const auto &Blend : LayerParameterID.Functions.BlendIDs)
				{
					UUIDs += (StartWithComma ? TEXT(", ") : TEXT("")) + Blend.ToString();
					StartWithComma = true;
				}
				UUIDs += TEXT(", LayerStates:");
				StartWithComma = false;
				for (bool State : LayerParameterID.Functions.LayerStates)
				{
					UUIDs += (StartWithComma ? TEXT(", ") : TEXT(""));
					UUIDs += (State ? TEXT("1") : TEXT("0"));
					StartWithComma = true;
				}
				WorkingDebugDescription += FString::Printf(TEXT(", LayersParameter'%s'=[%s]"), *LayerParameterID.ParameterID.ParameterInfo.ToString(), *UUIDs);
			}
  
			UE_LOG(LogShaders, Display, TEXT("	%s"), *WorkingDebugDescription);
			NewContent->DebugDescription = *WorkingDebugDescription;

			FSHA1 IdParameterSetHash;
			IdParameterSetHash.Reset();
			IdParameterSetHash.UpdateWithString(*WorkingDebugDescription, WorkingDebugDescription.Len());
			IdParameterSetHash.Final();
			uint32* Hash = (uint32*)&IdParameterSetHash.m_digest[0];
			FString DebugExtension = FString::Printf( TEXT("_%08x%08x"), Hash[0], Hash[1]);
#else
			FString DebugExtension = "";
			FString WorkingDebugDescription = "";
#endif 
			uint32 NumShaders = 0;
			uint32 NumVertexFactories = 0;
			TArray<TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe>> NewJobs;
  
			const FMaterialShaderParameters MaterialParameters(Material);
			const FMaterialShaderMapLayout& Layout = AcquireMaterialShaderMapLayout(InPlatform, MaterialParameters);

			// Iterate over all vertex factory types.
			for (const FMeshMaterialShaderMapLayout& MeshLayout : Layout.MeshShaderMaps)
			{
				FMeshMaterialShaderMap* MeshShaderMap = GetMeshShaderMap(MeshLayout.VertexFactoryType);
				if (MeshShaderMap == nullptr)
				{
					// Create a new mesh material shader map.
					MeshShaderMap = new FMeshMaterialShaderMap(InPlatform, MeshLayout.VertexFactoryType);
					NewContent->AddMeshShaderMap(MeshLayout.VertexFactoryType, MeshShaderMap);
				}
  
				// Enqueue compilation all mesh material shaders for this material and vertex factory type combo.
				const uint32 MeshShaders = MeshShaderMap->BeginCompile(
					CompilingId,
					InShaderMapId,
					Material,
					MeshLayout,
					MaterialEnvironment,
					InPlatform,
					NewJobs,
					WorkingDebugDescription,
					DebugExtension
				);
				NumShaders += MeshShaders;
				if (MeshShaders > 0)
				{
					NumVertexFactories++;
				}
			}
  
			// Iterate over all material shader types.
			TMap<TShaderTypePermutation<const FShaderType>, FShaderCompileJob*> SharedShaderJobs;
			for (const FShaderLayoutEntry& Shader : Layout.Shaders)
			{
				FMaterialShaderType* ShaderType = static_cast<FMaterialShaderType*>(Shader.ShaderType);
				if (!Material->ShouldCache(InPlatform, ShaderType, nullptr))
				{
					continue;
				}

#if WITH_EDITOR
				// Verify that the shader map Id contains inputs for any shaders that will be put into this shader map
				check(InShaderMapId.ContainsShaderType(ShaderType, kUniqueShaderPermutationId));
#endif
				// Compile this material shader for this material.
				TArray<FString> ShaderErrors;
  
				// Only compile the shader if we don't already have it
				if (!NewContent->HasShader(ShaderType, Shader.PermutationId))
				{
					FShaderCompileJob* Job = ShaderType->BeginCompileShader(
						CompilingId,
						Shader.PermutationId,
						Material,
						MaterialEnvironment,
						nullptr,
						InPlatform,
						NewJobs,
						WorkingDebugDescription,
						DebugExtension
					);

					TShaderTypePermutation<const FShaderType> ShaderTypePermutation(ShaderType, Shader.PermutationId);
					check(!SharedShaderJobs.Find(ShaderTypePermutation));
					SharedShaderJobs.Add(ShaderTypePermutation, Job);
				}
				NumShaders++;
			}
  
			if (RHISupportsShaderPipelines(InPlatform))
			{
				for (FShaderPipelineType* Pipeline : Layout.ShaderPipelines)
				{
					if (!Material->ShouldCachePipeline(InPlatform, Pipeline, nullptr))
					{
						continue;
					}
  
					auto& StageTypes = Pipeline->GetStages();

					// Verify that the shader map Id contains inputs for any shaders that will be put into this shader map
#if WITH_EDITOR
					check(InShaderMapId.ContainsShaderPipelineType(Pipeline));
#endif
					if (Pipeline->ShouldOptimizeUnusedOutputs(InPlatform))
					{
						NumShaders += StageTypes.Num();
						FMaterialShaderType::BeginCompileShaderPipeline(CompilingId, InPlatform, Material, MaterialEnvironment, Pipeline, StageTypes, NewJobs, WorkingDebugDescription, DebugExtension);
					}
					else
					{
						// If sharing shaders amongst pipelines, add this pipeline as a dependency of an existing job
						for (const FShaderType* ShaderType : StageTypes)
						{
							TShaderTypePermutation<const FShaderType> ShaderTypePermutation(ShaderType, kUniqueShaderPermutationId);
							FShaderCompileJob** Job = SharedShaderJobs.Find(ShaderTypePermutation);
							checkf(Job, TEXT("Couldn't find existing shared job for material shader %s on pipeline %s!"), ShaderType->GetName(), Pipeline->GetName());
							auto* SingleJob = (*Job)->GetSingleShaderJob();
							auto& PipelinesToShare = SingleJob->SharingPipelines.FindOrAdd(nullptr);
							check(!PipelinesToShare.Contains(Pipeline));
							PipelinesToShare.Add(Pipeline);
						}
					}
				}
			}

			UE_LOG(LogShaders, Log, TEXT("		%u Shaders among %u VertexFactories"), NumShaders, NumVertexFactories);

			// Register this shader map in the global map with the material's ID.
			Register(InPlatform);

			CompilingTargetPlatform = MaterialEnvironment->TargetPlatform;
  
			// Mark the shader map as not having been finalized with ProcessCompilationResults
			bCompilationFinalized = false;
  
			// Mark as not having been compiled
			bCompiledSuccessfully = false;
  
			// Only cause a global component recreate state for non-preview materials
			const bool bRecreateComponentRenderStateOnCompletion = Material->IsPersistent();

			// Note: using Material->IsPersistent() to detect whether this is a preview material which should have higher priority over background compiling
			GShaderCompilingManager->AddJobs(NewJobs, bSynchronousCompile || !Material->IsPersistent(), bRecreateComponentRenderStateOnCompletion, Material->GetBaseMaterialPathName(), GetDebugDescription());
  
			// Compile the shaders for this shader map now if the material is not deferring and deferred compiles are not enabled globally
			if (bSynchronousCompile)
			{
				TArray<int32> CurrentShaderMapId;
				CurrentShaderMapId.Add(CompilingId);
				GShaderCompilingManager->FinishCompilation(
					GetFriendlyName(),
					CurrentShaderMapId);
			}
		}
	}
}

FShader* FMaterialShaderMap::ProcessCompilationResultsForSingleJob(FShaderCompileJob* SingleJob, const FShaderPipelineType* ShaderPipeline, const FSHAHash& MaterialShaderMapHash)
{
	check(SingleJob);
	const FShaderCompileJob& CurrentJob = *SingleJob;
	check(CurrentJob.Id == CompilingId);

	GetResourceCode()->AddShaderCompilerOutput(CurrentJob.Output);

#if ALLOW_SHADERMAP_DEBUG_DATA
	CompileTime += SingleJob->Output.CompileTime;
#endif
	FShader* Shader = nullptr;
	if (CurrentJob.VFType)
	{
		FVertexFactoryType* VertexFactoryType = CurrentJob.VFType;
		check(VertexFactoryType->IsUsedWithMaterials());
		FMeshMaterialShaderMap* MeshShaderMap = GetMeshShaderMap(VertexFactoryType);

		check(MeshShaderMap);
		FMeshMaterialShaderType* MeshMaterialShaderType = CurrentJob.ShaderType->GetMeshMaterialShaderType();
		check(MeshMaterialShaderType);
		Shader = MeshMaterialShaderType->FinishCompileShader(GetContent()->MaterialCompilationOutput.UniformExpressionSet, MaterialShaderMapHash, CurrentJob, ShaderPipeline, GetFriendlyName());
		check(Shader);
		if (!ShaderPipeline)
		{
			check(!MeshShaderMap->HasShader(MeshMaterialShaderType, CurrentJob.PermutationId));
			Shader = MeshShaderMap->FindOrAddShader(MeshMaterialShaderType->GetHashedName(), CurrentJob.PermutationId, Shader);
		}
	}
	else
	{
		FMaterialShaderType* MaterialShaderType = CurrentJob.ShaderType->GetMaterialShaderType();
		check(MaterialShaderType);
		Shader = MaterialShaderType->FinishCompileShader(GetContent()->MaterialCompilationOutput.UniformExpressionSet, MaterialShaderMapHash, CurrentJob, ShaderPipeline, GetFriendlyName());

		check(Shader);
		if (!ShaderPipeline)
		{
			check(!GetContent()->HasShader(MaterialShaderType, CurrentJob.PermutationId));
			Shader = GetMutableContent()->FindOrAddShader(MaterialShaderType->GetHashedName(), CurrentJob.PermutationId, Shader);
		}
	}

#if WITH_EDITOR
	// add shader source
	{
		// Keep the preprocessed source list sorted by type name
		const int32 Index = Algo::LowerBoundBy(GetMutableContent()->ShaderProcessedSource, CurrentJob.ShaderType->GetHashedName(), [](const FMaterialProcessedSource& Value) { return Value.Name; });
		GetMutableContent()->ShaderProcessedSource.EmplaceAt(Index, CurrentJob.ShaderType->GetHashedName(), *CurrentJob.Output.OptionalFinalShaderSource);
	}
#endif

	return Shader;
}

#if WITH_EDITOR
bool FMaterialShaderMap::ProcessCompilationResults(const TArray<TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe>>& InCompilationResults, int32& InOutJobIndex, float& TimeBudget, TMap<const FVertexFactoryType*, TArray<const FShaderPipelineType*> >& SharedPipelines)
{
	check(InOutJobIndex < InCompilationResults.Num());
	check(!bCompilationFinalized);

	double StartTime = FPlatformTime::Seconds();

	FSHAHash MaterialShaderMapHash;
	ShaderMapId.GetMaterialHash(MaterialShaderMapHash);

	do
	{
		FShaderCompileJob* SingleJob = InCompilationResults[InOutJobIndex]->GetSingleShaderJob();
		if (SingleJob)
		{
			ProcessCompilationResultsForSingleJob(SingleJob, nullptr, MaterialShaderMapHash);
			for (auto Pair : SingleJob->SharingPipelines)
			{
				auto& SharedPipelinesPerVF = SharedPipelines.FindOrAdd(SingleJob->VFType);
				for (auto* Pipeline : Pair.Value)
				{
					SharedPipelinesPerVF.AddUnique(Pipeline);
				}
			}
		}
		else
		{
			auto* PipelineJob = InCompilationResults[InOutJobIndex]->GetShaderPipelineJob();
			check(PipelineJob);

			const FShaderPipelineCompileJob& CurrentJob = *PipelineJob;
			check(CurrentJob.Id == CompilingId);

			FVertexFactoryType* VertexFactoryType = CurrentJob.StageJobs[0]->GetSingleShaderJob()->VFType;
			FShaderPipeline* ShaderPipeline = new FShaderPipeline(CurrentJob.ShaderPipeline);
			for (int32 Index = 0; Index < CurrentJob.StageJobs.Num(); ++Index)
			{
				SingleJob = CurrentJob.StageJobs[Index]->GetSingleShaderJob();
				FShader* Shader = ProcessCompilationResultsForSingleJob(SingleJob, PipelineJob->ShaderPipeline, MaterialShaderMapHash);
				ShaderPipeline->AddShader(Shader, SingleJob->PermutationId);
				check(VertexFactoryType == CurrentJob.StageJobs[Index]->GetSingleShaderJob()->VFType);
			}
			ShaderPipeline->Validate(CurrentJob.ShaderPipeline);

			if (VertexFactoryType)
			{
				check(VertexFactoryType->IsUsedWithMaterials());
				FMeshMaterialShaderMap* MeshShaderMap = GetMeshShaderMap(VertexFactoryType);
				check(MeshShaderMap);
				check(!MeshShaderMap->HasShaderPipeline(ShaderPipeline->TypeName));
				MeshShaderMap->FindOrAddShaderPipeline(ShaderPipeline);
			}
			else
			{
				check(!GetContent()->HasShaderPipeline(ShaderPipeline->TypeName));
				GetMutableContent()->FindOrAddShaderPipeline(ShaderPipeline);
			}

		}

		InOutJobIndex++;
		
		double NewStartTime = FPlatformTime::Seconds();
		TimeBudget -= NewStartTime - StartTime;
		StartTime = NewStartTime;
	}
	while ((TimeBudget > 0.0f) && (InOutJobIndex < InCompilationResults.Num()));

	if (InOutJobIndex == InCompilationResults.Num())
	{
		{
			// Process the mesh shader pipelines that share shaders
			for (int32 ShaderMapIndex = 0; ShaderMapIndex < GetContent()->OrderedMeshShaderMaps.Num(); ShaderMapIndex++)
			{
				FMeshMaterialShaderMap* MeshShaderMap = GetContent()->OrderedMeshShaderMaps[ShaderMapIndex];
				if (MeshShaderMap)
				{
					FVertexFactoryType* VertexFactory = FVertexFactoryType::GetVFByName(MeshShaderMap->GetVertexFactoryTypeName());
					TArray<const FShaderPipelineType*>* FoundSharedPipelines = SharedPipelines.Find(VertexFactory);
					if (VertexFactory && FoundSharedPipelines)
					{
						for (const FShaderPipelineType* ShaderPipelineType : *FoundSharedPipelines)
						{
							if (ShaderPipelineType->IsMeshMaterialTypePipeline() && !MeshShaderMap->HasShaderPipeline(ShaderPipelineType))
							{
								auto& StageTypes = ShaderPipelineType->GetStages();
	
								FShaderPipeline* ShaderPipeline = new FShaderPipeline(ShaderPipelineType);
								for (int32 Index = 0; Index < StageTypes.Num(); ++Index)
								{
									FMeshMaterialShaderType* ShaderType = ((FMeshMaterialShaderType*)(StageTypes[Index]))->GetMeshMaterialShaderType();
									FShader* Shader = MeshShaderMap->GetShader(ShaderType, kUniqueShaderPermutationId);
#if DO_CHECK
									UE_CLOG(Shader == nullptr, LogShaders, Fatal, TEXT("Failed to get ShaderType %s Permutation %d from MeshMaterial ShaderPipeline %s"), ShaderType->GetName(), kUniqueShaderPermutationId, ShaderPipelineType->GetName());
#endif	// DO_CHECK
									ShaderPipeline->AddShader(Shader, kUniqueShaderPermutationId);
								}
								ShaderPipeline->Validate(ShaderPipelineType);
								MeshShaderMap->FindOrAddShaderPipeline(ShaderPipeline);
							}
						}
					}
				}
			}

			// Process the material shader pipelines that share shaders
			auto* FoundSharedPipelines = SharedPipelines.Find(nullptr);
			if (FoundSharedPipelines)
			{
				for (const FShaderPipelineType* ShaderPipelineType : *FoundSharedPipelines)
				{
					if (ShaderPipelineType->IsMaterialTypePipeline() && !GetContent()->HasShaderPipeline(ShaderPipelineType))
					{
						auto& StageTypes = ShaderPipelineType->GetStages();

						FShaderPipeline* ShaderPipeline = new FShaderPipeline(ShaderPipelineType);
						for (int32 Index = 0; Index < StageTypes.Num(); ++Index)
						{
							FMaterialShaderType* ShaderType = ((FMaterialShaderType*)(StageTypes[Index]))->GetMaterialShaderType();
							FShader* Shader = GetContent()->GetShader(ShaderType, kUniqueShaderPermutationId);
							check(Shader);
							ShaderPipeline->AddShader(Shader, kUniqueShaderPermutationId);
						}
						ShaderPipeline->Validate(ShaderPipelineType);
						GetMutableContent()->FindOrAddShaderPipeline(ShaderPipeline);
					}
				}
			}
		}

		FinalizeContent();

		// Add the persistent shaders to the local shader cache.
		if (bIsPersistent)
		{
			SaveToDerivedDataCache(CompilingTargetPlatform);
		}

		// The shader map can now be used on the rendering thread
		bCompilationFinalized = true;

		CompilingTargetPlatform = nullptr;

		return true;
	}

	return false;
}
#endif // WITH_EDITOR

bool FMaterialShaderMap::TryToAddToExistingCompilationTask(FMaterial* Material)
{
	check(NumRefs > 0);
	TArray<FMaterial*>* CorrespondingMaterials = FMaterialShaderMap::ShaderMapsBeingCompiled.Find(this);

	if (CorrespondingMaterials)
	{
		CorrespondingMaterials->AddUnique(Material);
#if DEBUG_INFINITESHADERCOMPILE
		UE_LOG(LogTemp, Display, TEXT("Added shader map 0x%08X%08X from material 0x%08X%08X"), (int)((int64)(this) >> 32), (int)((int64)(this)), (int)((int64)(Material) >> 32), (int)((int64)(Material)));
#endif
		return true;
	}

	return false;
}

class FMaterialShaderMapLayoutCache
{
public:
	static FMaterialShaderMapLayoutCache& Get()
	{
		static FMaterialShaderMapLayoutCache Instance;
		return Instance;
	}

	const FMaterialShaderMapLayout& AcquireLayout(EShaderPlatform Platform, const FMaterialShaderParameters& MaterialParameters)
	{
		const uint64 ParameterHash = CityHash64WithSeed((char*)&MaterialParameters, sizeof(MaterialParameters), (uint64)Platform);
		
		int32 Index = INDEX_NONE;
		for (Index = ShaderMapHashTable.First(ParameterHash); ShaderMapHashTable.IsValid(Index); Index = ShaderMapHashTable.Next(Index))
		{
			if (MaterialParameterHashes[Index] == ParameterHash)
			{
				break;
			}
		}
		if (Index == INDEX_NONE)
		{
			Index = MaterialParameterHashes.Add(ParameterHash);
			MaterialShaderParameters.Add(MaterialParameters);
			ShaderMapHashTable.Add(ParameterHash, Index);
			FMaterialShaderMapLayout& Layout = ShaderMapLayouts.AddDefaulted_GetRef();
			check(MaterialParameterHashes.Num() == ShaderMapLayouts.Num());
			check(MaterialShaderParameters.Num() == ShaderMapLayouts.Num());
			Layout.Platform = Platform;
			CreateLayout(Layout, Platform, MaterialParameters);
		}

		check(ShaderMapLayouts[Index].Platform == Platform);
		return ShaderMapLayouts[Index];
	}

private:
	void CreateLayout(FMaterialShaderMapLayout& Layout, EShaderPlatform Platform, const FMaterialShaderParameters& MaterialParameters)
	{
		SCOPED_LOADTIMER(FMaterialShaderMapLayoutCache_CreateLayout);

		const bool bHasTessellation = MaterialParameters.TessellationMode != MTM_NoTessellation;
		const TArray<FShaderType*>& SortedMaterialShaderTypes = FShaderType::GetSortedTypes(FShaderType::EShaderTypeForDynamicCast::Material);
		const TArray<FShaderType*>& SortedMeshMaterialShaderTypes = FShaderType::GetSortedTypes(FShaderType::EShaderTypeForDynamicCast::MeshMaterial);
		const TArray<FShaderPipelineType*>& SortedMaterialPipelineTypes = FShaderPipelineType::GetSortedTypes(FShaderType::EShaderTypeForDynamicCast::Material);
		const TArray<FShaderPipelineType*>& SortedMeshMaterialPipelineTypes = FShaderPipelineType::GetSortedTypes(FShaderType::EShaderTypeForDynamicCast::MeshMaterial);

		FSHA1 Hasher;

		for (FShaderType* BaseShaderType : SortedMaterialShaderTypes)
		{
			// Find this shader type in the material's shader map.
			FMaterialShaderType* ShaderType = static_cast<FMaterialShaderType*>(BaseShaderType);
			const int32 PermutationCount = ShaderType->GetPermutationCount();
			for (int32 PermutationId = 0; PermutationId < PermutationCount; ++PermutationId)
			{
				if (ShaderType->ShouldCompilePermutation(Platform, MaterialParameters, PermutationId))
				{
					Layout.Shaders.Add(FShaderLayoutEntry(ShaderType, PermutationId));

					const FHashedName& TypeName = ShaderType->GetHashedName();
					Hasher.Update((uint8*)&TypeName, sizeof(TypeName));
					Hasher.Update((uint8*)&PermutationId, sizeof(PermutationId));
				}
			}
		}

		// Iterate over all pipeline types
		for (FShaderPipelineType* ShaderPipelineType : SortedMaterialPipelineTypes)
		{
			if (ShaderPipelineType->HasTessellation() == bHasTessellation &&
				FMaterialShaderType::ShouldCompilePipeline(ShaderPipelineType, Platform, MaterialParameters))
			{
				Layout.ShaderPipelines.Add(ShaderPipelineType);

				const FHashedName& TypeName = ShaderPipelineType->GetHashedName();
				Hasher.Update((uint8*)&TypeName, sizeof(TypeName));
			}
		}

		for (FVertexFactoryType* VertexFactoryType : FVertexFactoryType::GetSortedMaterialTypes())
		{
			if (!FMeshMaterialShaderType::ShouldCompileVertexFactoryPermutation(VertexFactoryType, Platform, MaterialParameters))
			{
				continue;
			}

			FMeshMaterialShaderMapLayout* MeshLayout = nullptr;
			for (FShaderType* BaseShaderType : SortedMeshMaterialShaderTypes)
			{
				FMeshMaterialShaderType* ShaderType = static_cast<FMeshMaterialShaderType*>(BaseShaderType);
				const int32 PermutationCount = ShaderType->GetPermutationCount();
				for (int32 PermutationId = 0; PermutationId < PermutationCount; ++PermutationId)
				{
					if (ShaderType->ShouldCompilePermutation(Platform, MaterialParameters, VertexFactoryType, PermutationId))
					{
						if (!MeshLayout)
						{
							MeshLayout = new(Layout.MeshShaderMaps) FMeshMaterialShaderMapLayout(VertexFactoryType);
						}
						MeshLayout->Shaders.Add(FShaderLayoutEntry(ShaderType, PermutationId));

						const FHashedName& TypeName = ShaderType->GetHashedName();
						Hasher.Update((uint8*)&TypeName, sizeof(TypeName));
						Hasher.Update((uint8*)&PermutationId, sizeof(PermutationId));
					}
				}
			}

			for (FShaderPipelineType* ShaderPipelineType : SortedMeshMaterialPipelineTypes)
			{
				if (ShaderPipelineType->HasTessellation() == bHasTessellation &&
					FMeshMaterialShaderType::ShouldCompilePipeline(ShaderPipelineType, Platform, MaterialParameters, VertexFactoryType))
				{
					// Now check the completeness of the shader map
					if (!MeshLayout)
					{
						MeshLayout = new(Layout.MeshShaderMaps) FMeshMaterialShaderMapLayout(VertexFactoryType);
					}
					MeshLayout->ShaderPipelines.Add(ShaderPipelineType);

					const FHashedName& TypeName = ShaderPipelineType->GetHashedName();
					Hasher.Update((uint8*)&TypeName, sizeof(TypeName));
				}
			}
		}

		Hasher.Final();
		Hasher.GetHash(Layout.ShaderMapHash.Hash);
	}

	TArray<FMaterialShaderMapLayout> ShaderMapLayouts;
	TArray<FMaterialShaderParameters> MaterialShaderParameters;
	TArray<uint64> MaterialParameterHashes;
	FHashTable ShaderMapHashTable;
};

const FMaterialShaderMapLayout& AcquireMaterialShaderMapLayout(EShaderPlatform Platform, const FMaterialShaderParameters& MaterialParameters)
{
	return FMaterialShaderMapLayoutCache::Get().AcquireLayout(Platform, MaterialParameters);
}

bool FMaterialShaderMap::IsComplete(const FMaterial* Material, bool bSilent)
{
	SCOPED_LOADTIMER(FMaterialShaderMap_IsComplete);

	// Make sure we are operating on a referenced shader map or the below Find will cause this shader map to be deleted,
	// Since it creates a temporary ref counted pointer.
	check(NumRefs > 0);
	const TArray<FMaterial*>* CorrespondingMaterials = FMaterialShaderMap::ShaderMapsBeingCompiled.Find(this);

	if (CorrespondingMaterials)
	{
		check(!bCompilationFinalized);
		return false;
	}

	const FMaterialShaderMapContent* LocalContent = GetContent();
	const EShaderPlatform Platform = LocalContent->GetShaderPlatform();
	const FMaterialShaderParameters MaterialParameters(Material);

	const FMaterialShaderMapLayout& Layout = AcquireMaterialShaderMapLayout(Platform, MaterialParameters);
	if (Layout.ShaderMapHash == LocalContent->ShaderContentHash)
	{
		return true;
	}

	// If our hash doesn't match the cached layout hash, shader map may still be complete
	// This can happen if FMaterial::ShouldCache is set to return false for any shaders that are included in the cached layout
	for (const FShaderLayoutEntry& Shader : Layout.Shaders)
	{
		if (!LocalContent->HasShader(Shader.ShaderType, Shader.PermutationId))
		{
			if (Material->ShouldCache(Platform, Shader.ShaderType, nullptr))
			{
				if (!bSilent)
				{
					UE_LOG(LogShaders, Warning, TEXT("Incomplete material %s, missing FMaterialShader (%s, %d)."), *Material->GetFriendlyName(), Shader.ShaderType->GetName(), Shader.PermutationId);
				}
				return false;
			}
		}
	}

	for (const FShaderPipelineType* Pipeline : Layout.ShaderPipelines)
	{
		if (!LocalContent->HasShaderPipeline(Pipeline))
		{
			if (Material->ShouldCachePipeline(Platform, Pipeline, nullptr))
			{
				if (!bSilent)
				{
					UE_LOG(LogShaders, Warning, TEXT("Incomplete material %s, missing pipeline %s."), *Material->GetFriendlyName(), Pipeline->GetName());
				}
				return false;
			}
		}
	}

	for (const FMeshMaterialShaderMapLayout& MeshLayout : Layout.MeshShaderMaps)
	{
		const FMeshMaterialShaderMap* MeshShaderMap = LocalContent->GetMeshShaderMap(MeshLayout.VertexFactoryType->GetHashedName());

		for (const FShaderLayoutEntry& Shader : MeshLayout.Shaders)
		{
			if (Material->ShouldCache(Platform, Shader.ShaderType, MeshLayout.VertexFactoryType))
			{
				if (!MeshShaderMap || !MeshShaderMap->HasShader(Shader.ShaderType, Shader.PermutationId))
				{
					if (!bSilent)
					{
						if (!MeshShaderMap)
						{
							UE_LOG(LogShaders, Warning, TEXT("Incomplete material %s, missing Vertex Factory %s."), *Material->GetFriendlyName(), MeshLayout.VertexFactoryType->GetName());
						}
						else
						{
							UE_LOG(LogShaders, Warning, TEXT("Incomplete material %s, missing (%s, %d) from %s."), *Material->GetFriendlyName(), Shader.ShaderType->GetName(), Shader.PermutationId, MeshLayout.VertexFactoryType->GetName());
						}
					}
					return false;
				}
			}
		}

		for (const FShaderPipelineType* Pipeline : MeshLayout.ShaderPipelines)
		{
			if (!MeshShaderMap || !MeshShaderMap->HasShaderPipeline(Pipeline))
			{
				if (Material->ShouldCachePipeline(Platform, Pipeline, MeshLayout.VertexFactoryType))
				{
					if (!bSilent)
					{
						if (!MeshShaderMap)
						{
							UE_LOG(LogShaders, Warning, TEXT("Incomplete material %s, missing Vertex Factory %s."), *Material->GetFriendlyName(), MeshLayout.VertexFactoryType->GetName());
						}
						else
						{
							UE_LOG(LogShaders, Warning, TEXT("Incomplete material %s, missing pipeline %s from %s."), *Material->GetFriendlyName(), Pipeline->GetName(), MeshLayout.VertexFactoryType->GetName());
						}
					}
					return false;
				}
			}
		}
	}

	// Was missing some shaders from the initial layout, but all of those shaders were explicitly exluced by our FMaterial::ShouldCache implementation
	return true;
}

#if WITH_EDITOR
void FMaterialShaderMap::LoadMissingShadersFromMemory(const FMaterial* Material)
{
#if 0
	// Make sure we are operating on a referenced shader map or the below Find will cause this shader map to be deleted,
	// Since it creates a temporary ref counted pointer.
	check(NumRefs > 0);

	const TArray<FMaterial*>* CorrespondingMaterials = FMaterialShaderMap::ShaderMapsBeingCompiled.Find(this);

	if (CorrespondingMaterials)
	{
		check(!bCompilationFinalized);
		return;
	}

	FSHAHash MaterialShaderMapHash;
	ShaderMapId.GetMaterialHash(MaterialShaderMapHash);

	// Try to find necessary FMaterialShaderType's in memory
	for (TLinkedList<FShaderType*>::TIterator ShaderTypeIt(FShaderType::GetTypeList());ShaderTypeIt;ShaderTypeIt.Next())
	{
		FMaterialShaderType* ShaderType = ShaderTypeIt->GetMaterialShaderType();
		const int32 PermutationCount = ShaderType ? ShaderType->GetPermutationCount() : 0;
		for (int32 PermutationId = 0; PermutationId < PermutationCount; ++PermutationId)
		{
			if (ShouldCacheMaterialShader(ShaderType, GetShaderPlatform(), Material, PermutationId) && !HasShader(ShaderType, PermutationId))
			{
				FShaderKey ShaderKey(MaterialShaderMapHash, nullptr, nullptr, PermutationId, GetShaderPlatform());
				FShader* FoundShader = ShaderType->FindShaderByKey(ShaderKey);
				if (FoundShader)
				{
					AddShader(ShaderType, PermutationId, FoundShader);
				}
			}
		}
	}

	// Try to find necessary FShaderPipelineTypes in memory
	const bool bHasTessellation = Material->GetTessellationMode() != MTM_NoTessellation;
	for (TLinkedList<FShaderPipelineType*>::TIterator ShaderPipelineIt(FShaderPipelineType::GetTypeList());ShaderPipelineIt;ShaderPipelineIt.Next())
	{
		const FShaderPipelineType* PipelineType = *ShaderPipelineIt;
		if (PipelineType && PipelineType->IsMaterialTypePipeline() && !HasShaderPipeline(PipelineType) && PipelineType->HasTessellation() == bHasTessellation)
		{
			auto& Stages = PipelineType->GetStages();
			int32 NumShaders = 0;
			for (const FShaderType* Shader : Stages)
			{
				FMaterialShaderType* ShaderType = (FMaterialShaderType*)Shader->GetMaterialShaderType();
				if (ShaderType && ShouldCacheMaterialShader(ShaderType, GetShaderPlatform(), Material, kUniqueShaderPermutationId))
				{
					++NumShaders;
				}
			}

			if (NumShaders == Stages.Num())
			{
				TArray<FShader*> ShadersForPipeline;
				for (auto* Shader : Stages)
				{
					FMaterialShaderType* ShaderType = (FMaterialShaderType*)Shader->GetMaterialShaderType();
					if (!HasShader(ShaderType, kUniqueShaderPermutationId))
					{
						FShaderKey ShaderKey(MaterialShaderMapHash, PipelineType->ShouldOptimizeUnusedOutputs(GetShaderPlatform()) ? PipelineType : nullptr, nullptr, kUniqueShaderPermutationId, GetShaderPlatform());
						FShader* FoundShader = ShaderType->FindShaderByKey(ShaderKey);
						if (FoundShader)
						{
							AddShader(ShaderType, kUniqueShaderPermutationId, FoundShader);
							ShadersForPipeline.Add(FoundShader);
						}
					}
				}

				if (ShadersForPipeline.Num() == NumShaders && !HasShaderPipeline(PipelineType))
				{
					auto* Pipeline = new FShaderPipeline(PipelineType, ShadersForPipeline);
					AddShaderPipeline(PipelineType, Pipeline);
				}
			}
		}
	}

	// Try to find necessary FMeshMaterialShaderMap's in memory
	for(TLinkedList<FVertexFactoryType*>::TIterator VertexFactoryTypeIt(FVertexFactoryType::GetTypeList());VertexFactoryTypeIt;VertexFactoryTypeIt.Next())
	{
		FVertexFactoryType* VertexFactoryType = *VertexFactoryTypeIt;
		check(VertexFactoryType);

		if (VertexFactoryType->IsUsedWithMaterials())
		{
			FMeshMaterialShaderMap* MeshShaderMap = OrderedMeshShaderMaps[VertexFactoryType->GetId()];

			if (MeshShaderMap)
			{
				MeshShaderMap->LoadMissingShadersFromMemory(MaterialShaderMapHash, Material, GetShaderPlatform());
			}
		}
	}
#endif
}
#endif // WITH_EDITOR

#if WITH_EDITOR
const FMemoryImageString* FMaterialShaderMap::GetShaderSource(const FName ShaderTypeName) const
{
	for (const FMaterialProcessedSource& Source : GetContent()->ShaderProcessedSource)
	{
		if (Source.Name == ShaderTypeName)
		{
			return &Source.Source;
		}
	}

	return nullptr;
}
#endif // WITH_EDITOR

void FMaterialShaderMap::GetShaderList(TMap<FShaderId, TShaderRef<FShader>>& OutShaders) const
{
	FSHAHash MaterialShaderMapHash;
#if WITH_EDITOR
	// TODO
	ShaderMapId.GetMaterialHash(MaterialShaderMapHash);
#endif

	GetContent()->GetShaderList(*this, MaterialShaderMapHash, OutShaders);
	for (FMeshMaterialShaderMap* MeshShaderMap : GetContent()->OrderedMeshShaderMaps)
	{
		if (MeshShaderMap)
		{
			MeshShaderMap->GetShaderList(*this, MaterialShaderMapHash, OutShaders);
		}
	}
}

void FMaterialShaderMap::GetShaderList(TMap<FHashedName, TShaderRef<FShader>>& OutShaders) const
{
	GetContent()->GetShaderList(*this, OutShaders);
	for (FMeshMaterialShaderMap* MeshShaderMap : GetContent()->OrderedMeshShaderMaps)
	{
		if (MeshShaderMap)
		{
			MeshShaderMap->GetShaderList(*this, OutShaders);
		}
	}
}

void FMaterialShaderMap::GetShaderPipelineList(TArray<FShaderPipelineRef>& OutShaderPipelines) const
{
	GetContent()->GetShaderPipelineList(*this, OutShaderPipelines, FShaderPipeline::EAll);
	for(FMeshMaterialShaderMap* MeshShaderMap : GetContent()->OrderedMeshShaderMaps)
	{
		if (MeshShaderMap)
		{
			MeshShaderMap->GetShaderPipelineList(*this, OutShaderPipelines, FShaderPipeline::EAll);
		}
	}
}

uint32 FMaterialShaderMap::GetShaderNum() const
{
	return GetContent()->GetNumShaders();
}

/**
 * Registers a material shader map in the global map so it can be used by materials.
 */
void FMaterialShaderMap::Register(EShaderPlatform InShaderPlatform)
{
	extern int32 GCreateShadersOnLoad;
	if (GCreateShadersOnLoad && GetShaderPlatform() == InShaderPlatform)
	{
		FShaderMapResource* ShaderResource = GetResource();
		if (ShaderResource)
		{
			ShaderResource->BeginCreateAllShaders();
		}
	}

	if (!bRegistered)
	{
		INC_DWORD_STAT(STAT_Shaders_NumShaderMaps);
	}

	{
		FScopeLock ScopeLock(&GIdToMaterialShaderMapCS);

		FMaterialShaderMap *CachedMap = GIdToMaterialShaderMap[GetShaderPlatform()].FindRef(ShaderMapId);

		// Only add new item if there's not already one in the map.
		// Items can possibly already be in the map because the GIdToMaterialShaderMapCS is not being locked between search & register lookups and new shader might be compiled
		if (CachedMap == nullptr)
		{
			GIdToMaterialShaderMap[GetShaderPlatform()].Add(ShaderMapId, this);
			bRegistered = true;
		}
		else
		{
			// Sanity check - We did not register so either bRegistered is false or this item is already in the map
			check((bRegistered == false && CachedMap != this) || (bRegistered == true && CachedMap == this));
		}
	}
}

void FMaterialShaderMap::AddRef()
{
	//#todo-mw: re-enable to try to find potential corruption of the global shader map ID array
	//check(IsInGameThread());
	FScopeLock ScopeLock(&GIdToMaterialShaderMapCS);
	check(!bDeletedThroughDeferredCleanup);
	++NumRefs;
}

void FMaterialShaderMap::Release()
{
	//#todo-mw: re-enable to try to find potential corruption of the global shader map ID array
	//check(IsInGameThread());

	{
		FScopeLock ScopeLock(&GIdToMaterialShaderMapCS);

		check(NumRefs > 0);
		if (--NumRefs == 0)
		{
			if (bRegistered)
			{
				bRegistered = false;
				DEC_DWORD_STAT(STAT_Shaders_NumShaderMaps);

				FMaterialShaderMap *CachedMap = GIdToMaterialShaderMap[GetShaderPlatform()].FindRef(ShaderMapId);

				// Map is marked as registered therefore we do expect it to be in the cache
				// If this does not happen there's bug in code causing ShaderMapID to be the same for two different objects.
				check(CachedMap == this);
				
				if (CachedMap == this)
				{
					GIdToMaterialShaderMap[GetShaderPlatform()].Remove(ShaderMapId);
				}
			}
			else
			{
				//sanity check - the map has not been registered and therefore should not appear in the cache
				check(GetShaderPlatform()>= EShaderPlatform::SP_NumPlatforms || GIdToMaterialShaderMap[GetShaderPlatform()].FindRef(ShaderMapId) != this);
			}

			check(!bDeletedThroughDeferredCleanup);
			bDeletedThroughDeferredCleanup = true;
		}
	}
	if (bDeletedThroughDeferredCleanup)
	{
		BeginCleanup(this);
	}
}

FMaterialShaderMap::FMaterialShaderMap() :
	CompilingTargetPlatform(nullptr),
	CompilingId(1),
	NumRefs(0),
	bDeletedThroughDeferredCleanup(false),
	bRegistered(false),
	bCompilationFinalized(true),
	bCompiledSuccessfully(true),
	bIsPersistent(true)
{
	checkSlow(IsInGameThread() || IsAsyncLoading());
#if ALLOW_SHADERMAP_DEBUG_DATA
	AllMaterialShaderMaps.Add(this);
	CompileTime = 0.f;
#endif
}

FMaterialShaderMap::~FMaterialShaderMap()
{ 
	checkSlow(IsInGameThread() || IsAsyncLoading());
	check(bDeletedThroughDeferredCleanup);
	check(!bRegistered);
#if ALLOW_SHADERMAP_DEBUG_DATA
	if(GShaderCompilerStats != 0 && GetContent())
	{
		FString Path = GetMaterialPath();
		if (Path.IsEmpty())
		{
			Path = GetFriendlyName();
		}
		GShaderCompilerStats->RegisterCookedShaders(GetShaderNum(), CompileTime, GetShaderPlatform(), Path, GetDebugDescription());
	}
	AllMaterialShaderMaps.RemoveSwap(this);
#endif
}

/**
 * Removes all entries in the cache with exceptions based on a shader type
 * @param ShaderType - The shader type to flush
 */
void FMaterialShaderMap::FlushShadersByShaderType(const FShaderType* ShaderType)
{
	// flush from all the vertex factory shader maps
	for (int32 Index = 0; Index < GetContent()->OrderedMeshShaderMaps.Num(); Index++)
	{
		FMeshMaterialShaderMap* MeshShaderMap = GetContent()->OrderedMeshShaderMaps[Index];
		if (MeshShaderMap)
		{
			MeshShaderMap->FlushShadersByShaderType(ShaderType);
		}
	}

	if (ShaderType->GetMaterialShaderType())
	{
		const int32 PermutationCount = ShaderType->GetPermutationCount();
		for (int32 PermutationId = 0; PermutationId < PermutationCount; ++PermutationId)
		{
			GetMutableContent()->RemoveShaderTypePermutaion(ShaderType->GetMaterialShaderType(), PermutationId);
		}
	}
}

void FMaterialShaderMap::FlushShadersByShaderPipelineType(const FShaderPipelineType* ShaderPipelineType)
{
	// flush from all the vertex factory shader maps
	for (int32 Index = 0;Index < GetContent()->OrderedMeshShaderMaps.Num();Index++)
	{
		FMeshMaterialShaderMap* MeshShaderMap = GetContent()->OrderedMeshShaderMaps[Index];
		if (MeshShaderMap)
		{
			MeshShaderMap->FlushShadersByShaderPipelineType(ShaderPipelineType);
		}
	}

	if (ShaderPipelineType->IsMaterialTypePipeline())
	{
		GetMutableContent()->RemoveShaderPipelineType(ShaderPipelineType);
	}
}


/**
 * Removes all entries in the cache with exceptions based on a vertex factory type
 * @param ShaderType - The shader type to flush
 */
void FMaterialShaderMap::FlushShadersByVertexFactoryType(const FVertexFactoryType* VertexFactoryType)
{
	GetMutableContent()->RemoveMeshShaderMap(VertexFactoryType);
}

bool FMaterialShaderMap::Serialize(FArchive& Ar, bool bInlineShaderResources, bool bLoadedByCookedMaterial)
{
	SCOPED_LOADTIMER(FMaterialShaderMap_Serialize);
	// Note: This is saved to the DDC, not into packages (except when cooked)
	// Backwards compatibility therefore will not work based on the version of Ar
	// Instead, just bump MATERIALSHADERMAP_DERIVEDDATA_VER
	ShaderMapId.Serialize(Ar, bLoadedByCookedMaterial);
	return Super::Serialize(Ar, bInlineShaderResources, bLoadedByCookedMaterial);
}

/*void FMaterialShaderMap::RegisterSerializedShaders(bool bLoadedByCookedMaterial)
{
	check(IsInGameThread());

	TShaderMapContent<FMaterialShaderType>::RegisterSerializedShaders(bLoadedByCookedMaterial);
	
	for (FMeshMaterialShaderMap* MeshShaderMap : OrderedMeshShaderMaps)
	{
		if (MeshShaderMap)
		{
			MeshShaderMap->RegisterSerializedShaders(bLoadedByCookedMaterial);
		}
	}

	// Trim the mesh shader maps by removing empty entries
	for (int32 VFIndex = 0; VFIndex < OrderedMeshShaderMaps.Num(); VFIndex++)
	{
		if (OrderedMeshShaderMaps[VFIndex] && OrderedMeshShaderMaps[VFIndex]->IsEmpty())
		{
			OrderedMeshShaderMaps[VFIndex] = nullptr;
		}
	}

	for (int32 Index = MeshShaderMaps.Num() - 1; Index >= 0; Index--)
	{
		if (MeshShaderMaps[Index].IsEmpty())
		{
			MeshShaderMaps.RemoveAt(Index);
		}
	}
}

void FMaterialShaderMap::DiscardSerializedShaders()
{
	TShaderMapContent<FMaterialShaderType>::DiscardSerializedShaders();

	for (int32 VFIndex = 0; VFIndex < OrderedMeshShaderMaps.Num(); VFIndex++)
	{
		OrderedMeshShaderMaps[VFIndex] = nullptr;
	}

	for (int32 Index = MeshShaderMaps.Num() - 1; Index >= 0; Index--)
	{
		MeshShaderMaps[Index].DiscardSerializedShaders();
	}
	MeshShaderMaps.Empty();
}*/

void FMaterialShaderMap::RemovePendingMaterial(FMaterial* Material)
{
	for (TMap<TRefCountPtr<FMaterialShaderMap>, TArray<FMaterial*> >::TIterator It(ShaderMapsBeingCompiled); It; ++It)
	{
		TArray<FMaterial*>& Materials = It.Value();
		int32 Result = Materials.Remove(Material);
#if DEBUG_INFINITESHADERCOMPILE
		if ( Result )
		{
			UE_LOG(LogTemp, Display, TEXT("Removed shader map 0x%08X%08X from material 0x%08X%08X"), (int)((int64)(It.Key().GetReference()) >> 32), (int)((int64)(It.Key().GetReference())), (int)((int64)(Material) >> 32), (int)((int64)(Material)));
		}
#endif
	}
}

const FMaterialShaderMap* FMaterialShaderMap::GetShaderMapBeingCompiled(const FMaterial* Material)
{
	// Inefficient search, but only when compiling a lot of shaders
	for (TMap<TRefCountPtr<FMaterialShaderMap>, TArray<FMaterial*> >::TIterator It(ShaderMapsBeingCompiled); It; ++It)
	{
		TArray<FMaterial*>& Materials = It.Value();
		
		for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); MaterialIndex++)
		{
			if (Materials[MaterialIndex] == Material)
			{
				return It.Key();
			}
		}
	}

	return NULL;
}

#if WITH_EDITOR
uint32 FMaterialShaderMap::GetMaxTextureSamplers() const
{
	uint32 MaxTextureSamplers = GetContent()->GetMaxTextureSamplersShaderMap(*this);

	for (int32 Index = 0;Index < GetContent()->OrderedMeshShaderMaps.Num();Index++)
	{
		const FMeshMaterialShaderMap* MeshShaderMap = GetContent()->OrderedMeshShaderMaps[Index];
		if (MeshShaderMap)
		{
			MaxTextureSamplers = FMath::Max(MaxTextureSamplers, MeshShaderMap->GetMaxTextureSamplersShaderMap(*this));
		}
	}

	return MaxTextureSamplers;
}
#endif // WITH_EDITOR

FMeshMaterialShaderMap* FMaterialShaderMapContent::GetMeshShaderMap(const FHashedName& VertexFactoryTypeName) const
{
	const int32 Index = Algo::BinarySearchBy(OrderedMeshShaderMaps, VertexFactoryTypeName, FProjectMeshShaderMapToKey());
	if (Index != INDEX_NONE)
	{
		FMeshMaterialShaderMap* MeshShaderMap = OrderedMeshShaderMaps[Index].Get();
		checkSlow(MeshShaderMap->GetVertexFactoryTypeName() == VertexFactoryTypeName);
		return MeshShaderMap;
	}
	return nullptr;
}

void FMaterialShaderMapContent::AddMeshShaderMap(const FVertexFactoryType* VertexFactoryType, FMeshMaterialShaderMap* MeshShaderMap)
{
	check(VertexFactoryType->GetHashedName() == MeshShaderMap->GetVertexFactoryTypeName());
	checkSlow(GetMeshShaderMap(VertexFactoryType->GetHashedName()) == nullptr);
	const int32 Index = Algo::LowerBoundBy(OrderedMeshShaderMaps, VertexFactoryType->GetHashedName(), FProjectMeshShaderMapToKey());
	OrderedMeshShaderMaps.Insert(MeshShaderMap, Index);
}

void FMaterialShaderMapContent::RemoveMeshShaderMap(const FVertexFactoryType* VertexFactoryType)
{
	const int32 Index = Algo::BinarySearchBy(OrderedMeshShaderMaps, VertexFactoryType->GetHashedName(), FProjectMeshShaderMapToKey());
	if (Index != INDEX_NONE)
	{
		FMeshMaterialShaderMap* MeshShaderMap = OrderedMeshShaderMaps[Index].Get();
		delete MeshShaderMap;
		OrderedMeshShaderMaps.RemoveAt(Index);
	}
}

void FMaterialShaderMap::DumpDebugInfo()
{
	const FString& FriendlyNameS = GetFriendlyName();
	UE_LOG(LogConsoleResponse, Display, TEXT("FMaterialShaderMap:  FriendlyName %s"), *FriendlyNameS);
	const FString& DebugDescriptionS = GetDebugDescription();
	UE_LOG(LogConsoleResponse, Display, TEXT("  DebugDescription %s"), *DebugDescriptionS);

	TMap<FShaderId, TShaderRef<FShader>> ShadersL;
	GetShaderList(ShadersL);
	UE_LOG(LogConsoleResponse, Display, TEXT("  --- %d shaders"), ShadersL.Num());
	int32 Index = 0;
	for (auto& KeyValue : ShadersL)
	{
		UE_LOG(LogConsoleResponse, Display, TEXT("    --- shader %d"), Index);
		FShader* Shader = KeyValue.Value.GetShader();
		Shader->DumpDebugInfo(GetPointerTable());
		Index++;
	}
	//TArray<FShaderPipeline*> ShaderPipelinesL;
	//GetShaderPipelineList(ShaderPipelinesL);
}

#if WITH_EDITOR
void FMaterialShaderMap::SaveShaderStableKeys(EShaderPlatform TargetShaderPlatform, const FStableShaderKeyAndValue& SaveKeyVal)
{
	FShaderMapBase::SaveShaderStableKeys(TargetShaderPlatform, SaveKeyVal);
	for (FMeshMaterialShaderMap* MeshShaderMap : GetContent()->OrderedMeshShaderMaps)
	{
		if (MeshShaderMap)
		{
			MeshShaderMap->SaveShaderStableKeys(*this, TargetShaderPlatform, SaveKeyVal);
		}
	}
}
#endif // WITH_EDITOR

/**
 * Dump material stats for a given platform.
 * 
 * @param	Platform	Platform to dump stats for.
 */
void DumpMaterialStats(EShaderPlatform Platform)
{
#if ALLOW_DEBUG_FILES && ALLOW_SHADERMAP_DEBUG_DATA
	FDiagnosticTableViewer MaterialViewer(*FDiagnosticTableViewer::GetUniqueTemporaryFilePath(TEXT("MaterialStats")));

	//#todo-rco: Pipelines

	// Mapping from friendly material name to shaders associated with it.
	TMultiMap<FString, TShaderRef<FShader>> MaterialToShaderMap;
	TMultiMap<FString, FShaderPipeline*> MaterialToShaderPipelineMap;

	// Set of material names.
	TSet<FString> MaterialNames;

	// Look at in-memory shader use.
	for (int32 ShaderMapIndex = 0; ShaderMapIndex < FMaterialShaderMap::AllMaterialShaderMaps.Num(); ShaderMapIndex++)
	{
		FMaterialShaderMap* MaterialShaderMap = FMaterialShaderMap::AllMaterialShaderMaps[ShaderMapIndex];
		TMap<FShaderId, TShaderRef<FShader>> Shaders;
		TArray<FShaderPipelineRef> ShaderPipelines;
		MaterialShaderMap->GetShaderList(Shaders);
		MaterialShaderMap->GetShaderPipelineList(ShaderPipelines);

		// Add friendly name to list of materials.
		FString FriendlyName = MaterialShaderMap->GetFriendlyName();
		MaterialNames.Add(FriendlyName);

		// Add shaders to mapping per friendly name as there might be multiple
		for (auto& KeyValue : Shaders)
		{
			MaterialToShaderMap.AddUnique(FriendlyName, KeyValue.Value);
		}

		for (const FShaderPipelineRef& Pipeline : ShaderPipelines)
		{
			for (const TShaderRef<FShader>& Shader : Pipeline.GetShaders())
			{
				MaterialToShaderMap.AddUnique(FriendlyName, Shader);
			}
			MaterialToShaderPipelineMap.AddUnique(FriendlyName, Pipeline.GetPipeline());
		}
	}

	// Write a row of headings for the table's columns.
	MaterialViewer.AddColumn(TEXT("Name"));
	MaterialViewer.AddColumn(TEXT("Shaders"));
	MaterialViewer.AddColumn(TEXT("Code Size"));
	MaterialViewer.AddColumn(TEXT("Pipelines"));
	MaterialViewer.CycleRow();

	// Iterate over all materials, gathering shader stats.
	int32 TotalCodeSize		= 0;
	int32 TotalShaderCount	= 0;
	int32 TotalShaderPipelineCount = 0;
	for( TSet<FString>::TConstIterator It(MaterialNames); It; ++It )
	{
		// Retrieve list of shaders in map.
		TArray<TShaderRef<FShader>> Shaders;
		MaterialToShaderMap.MultiFind( *It, Shaders );
		TArray<FShaderPipeline*> ShaderPipelines;
		MaterialToShaderPipelineMap.MultiFind(*It, ShaderPipelines);
		
		// Iterate over shaders and gather stats.
		int32 CodeSize = 0;
		for( int32 ShaderIndex=0; ShaderIndex<Shaders.Num(); ShaderIndex++ )
		{
			const TShaderRef<FShader>& Shader = Shaders[ShaderIndex];
			CodeSize += Shader->GetCodeSize();
		}

		TotalCodeSize += CodeSize;
		TotalShaderCount += Shaders.Num();
		TotalShaderPipelineCount += ShaderPipelines.Num();

		// Dump stats
		MaterialViewer.AddColumn(**It);
		MaterialViewer.AddColumn(TEXT("%u"),Shaders.Num());
		MaterialViewer.AddColumn(TEXT("%u"),CodeSize);
		MaterialViewer.AddColumn(TEXT("%u"), ShaderPipelines.Num());
		MaterialViewer.CycleRow();
	}

	// Add a total row.
	MaterialViewer.AddColumn(TEXT("Total"));
	MaterialViewer.AddColumn(TEXT("%u"),TotalShaderCount);
	MaterialViewer.AddColumn(TEXT("%u"),TotalCodeSize);
	MaterialViewer.AddColumn(TEXT("%u"), TotalShaderPipelineCount);
	MaterialViewer.CycleRow();
#endif // ALLOW_DEBUG_FILES && ALLOW_SHADERMAP_DEBUG_DATA
}

