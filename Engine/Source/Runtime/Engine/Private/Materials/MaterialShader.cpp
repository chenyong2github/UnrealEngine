// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialShader.h"
#include "Stats/StatsMisc.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialFunctionInterface.h"
#include "ProfilingDebugging/DiagnosticTable.h"
#include "MeshMaterialShaderType.h"
#include "ShaderCompiler.h"
#include "DerivedDataCacheInterface.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "ShaderDerivedDataVersion.h"
#include "ProfilingDebugging/CookStats.h"
#include "UObject/ReleaseObjectVersion.h"
#include "UObject/EditorObjectVersion.h"

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
// The Id of 0 is reserved for global shaders
uint32 FMaterialShaderMap::NextCompilingId = 2;
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
static FString GetMaterialShaderMapKeyString(const FMaterialShaderMapId& ShaderMapId, EShaderPlatform Platform)
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
	else if (ShadingModels.HasAnyShadingModel({ MSM_DefaultLit, MSM_Subsurface, MSM_PreintegratedSkin, MSM_ClearCoat, MSM_Cloth, MSM_SubsurfaceProfile, MSM_TwoSidedFoliage, MSM_SingleLayerWater }))
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


UMaterialFunctionInterface* FStaticMaterialLayersParameter::GetParameterAssociatedFunction(const FMaterialParameterInfo& InParameterInfo) const
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

void FStaticMaterialLayersParameter::GetParameterAssociatedFunctions(const FMaterialParameterInfo& InParameterInfo, TArray<UMaterialFunctionInterface*>& AssociatedFunctions) const
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

	if (Function)
	{
		Function->GetDependentFunctions(AssociatedFunctions);
	}
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
		check(!bResult || (*this) == ReferenceSet); // if this never fires, then we really didn't need to sort did we?
		return bResult;
	}
	return false;
}

void FMaterialShaderMapId::Serialize(FArchive& Ar, bool bLoadedByCookedMaterial)
{
	// Note: FMaterialShaderMapId is saved both in packages (legacy UMaterialInstance) and the DDC (FMaterialShaderMap)
	// Backwards compatibility only works with FMaterialShaderMapId's stored in packages.  
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
		KeyString += TEXT("_");
		KeyString += ShaderTypeDependency.ShaderType->GetName();
		KeyString += ShaderTypeDependency.SourceHash.ToString();
		ShaderTypeDependency.ShaderType->GetSerializationHistory().AppendKeyString(KeyString);

		const TMap<const TCHAR*,FCachedUniformBufferDeclaration>& ReferencedUniformBufferStructsCache = ShaderTypeDependency.ShaderType->GetReferencedUniformBufferStructsCache();

		for (TMap<const TCHAR*,FCachedUniformBufferDeclaration>::TConstIterator It(ReferencedUniformBufferStructsCache); It; ++It)
		{
			ReferencedUniformBuffers.Add(It.Key(), It.Value());
		}
	}

	// Add the inputs for any shader pipelines that are stored inline in the shader map
	for (int32 TypeIndex = 0; TypeIndex < ShaderPipelineTypeDependencies.Num(); TypeIndex++)
	{
		const FShaderPipelineTypeDependency& Dependency = ShaderPipelineTypeDependencies[TypeIndex];
		KeyString += TEXT("_");
		KeyString += Dependency.ShaderPipelineType->GetName();
		KeyString += Dependency.StagesSourceHash.ToString();

		for (const FShaderType* ShaderType : Dependency.ShaderPipelineType->GetStages())
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
		KeyString += VFDependency.VertexFactoryType->GetName();
		KeyString += VFDependency.VFSourceHash.ToString();

		for (int32 Frequency = 0; Frequency < SF_NumFrequencies; Frequency++)
		{
			VFDependency.VertexFactoryType->GetSerializationHistory((EShaderFrequency)Frequency)->AppendKeyString(KeyString);
		}

		const TMap<const TCHAR*,FCachedUniformBufferDeclaration>& ReferencedUniformBufferStructsCache = VFDependency.VertexFactoryType->GetReferencedUniformBufferStructsCache();

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
			Dependency.ShaderType = ShaderTypes[ShaderTypeIndex];
			Dependency.SourceHash = ShaderTypes[ShaderTypeIndex]->GetSourceHash(ShaderPlatform);
			ShaderTypeDependencies.Add(Dependency);
		}

		for (int32 VFTypeIndex = 0; VFTypeIndex < VFTypes.Num(); VFTypeIndex++)
		{
			FVertexFactoryTypeDependency Dependency;
			Dependency.VertexFactoryType = VFTypes[VFTypeIndex];
			Dependency.VFSourceHash = VFTypes[VFTypeIndex]->GetSourceHash(ShaderPlatform);
			VertexFactoryTypeDependencies.Add(Dependency);
		}

		for (int32 TypeIndex = 0; TypeIndex < ShaderPipelineTypes.Num(); TypeIndex++)
		{
			const FShaderPipelineType* Pipeline = ShaderPipelineTypes[TypeIndex];
			FShaderPipelineTypeDependency Dependency;
			Dependency.ShaderPipelineType = Pipeline;
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
	TArray<FShaderCommonCompileJob*>& NewJobs,
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
		NewJob,
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
	const TArray<FMaterialShaderType*>& ShaderStages,
	TArray<FShaderCommonCompileJob*>& NewJobs,
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
		auto* ShaderStage = ShaderStages[Index];
		ShaderStage->BeginCompileShader(ShaderMapId, kUniqueShaderPermutationId, Material, MaterialEnvironment, ShaderPipeline, Platform, NewPipelineJob->StageJobs, DebugDescription, DebugExtension);
	}

	NewJobs.Add(NewPipelineJob);
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

	// Reuse an existing resource with the same key or create a new one based on the compile output
	// This allows FShaders to share compiled bytecode and RHI shader references
	TRefCountPtr<FShaderResource> Resource = FShaderResource::FindOrCreate(CurrentJob.Output, 0);

	if (ShaderPipelineType && !ShaderPipelineType->ShouldOptimizeUnusedOutputs(CurrentJob.Input.Target.GetPlatform()))
	{
		// If sharing shaders in this pipeline, remove it from the type/id so it uses the one in the shared shadermap list
		ShaderPipelineType = nullptr;
	}

	// Find a shader with the same key in memory
	FShader* Shader = CurrentJob.ShaderType->FindShaderByKey(FShaderKey(MaterialShaderMapHash, ShaderPipelineType, CurrentJob.VFType, CurrentJob.PermutationId, CurrentJob.Input.Target.GetPlatform()));

	// There was no shader with the same key so create a new one with the compile output, which will bind shader parameters
	if (!Shader)
	{
		Shader = (*ConstructCompiledRef)(CompiledShaderInitializerType(this, CurrentJob.PermutationId, CurrentJob.Output, MoveTemp(Resource), UniformExpressionSet, MaterialShaderMapHash, ShaderPipelineType, nullptr, InDebugDescription));
		CurrentJob.Output.ParameterMap.VerifyBindingsAreComplete(GetName(), CurrentJob.Output.Target, CurrentJob.VFType);
	}

	return Shader;
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
void FMaterialShaderMap::FlushShaderTypes(TArray<FShaderType*>& ShaderTypesToFlush, TArray<const FShaderPipelineType*>& ShaderPipelineTypesToFlush, TArray<const FVertexFactoryType*>& VFTypesToFlush)
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

void FMaterialShaderMap::FixupShaderTypes(EShaderPlatform Platform, const TMap<FShaderType*, FString>& ShaderTypeNames, const TMap<const FShaderPipelineType*, FString>& ShaderPipelineTypeNames, const TMap<FVertexFactoryType*, FString>& VertexFactoryTypeNames)
{
#if WITH_EDITOR
	FScopeLock ScopeLock(&GIdToMaterialShaderMapCS);
	
	TArray<FMaterialShaderMapId> Keys;
	FMaterialShaderMap::GIdToMaterialShaderMap[Platform].GenerateKeyArray(Keys);

	TArray<FMaterialShaderMap*> Values;
	FMaterialShaderMap::GIdToMaterialShaderMap[Platform].GenerateValueArray(Values);

	//@todo - what about the shader maps in AllMaterialShaderMaps that are not in GIdToMaterialShaderMap?
	FMaterialShaderMap::GIdToMaterialShaderMap[Platform].Empty();

	for (int32 PairIndex = 0; PairIndex < Keys.Num(); PairIndex++)
	{
		FMaterialShaderMapId& Key = Keys[PairIndex];

		for (int32 ShaderTypeIndex = 0; ShaderTypeIndex < Key.ShaderTypeDependencies.Num(); ShaderTypeIndex++)
		{
			const FString& ShaderTypeName = ShaderTypeNames.FindChecked(Key.ShaderTypeDependencies[ShaderTypeIndex].ShaderType);
			FShaderType* FoundShaderType = FShaderType::GetShaderTypeByName(*ShaderTypeName);
			Key.ShaderTypeDependencies[ShaderTypeIndex].ShaderType = FoundShaderType;
		}

		for (int32 ShaderPipelineIndex = 0; ShaderPipelineIndex < Key.ShaderPipelineTypeDependencies.Num(); ShaderPipelineIndex++)
		{
			const FString& ShaderPipelineTypeName = ShaderPipelineTypeNames.FindChecked(Key.ShaderPipelineTypeDependencies[ShaderPipelineIndex].ShaderPipelineType);
			const FShaderPipelineType* FoundShaderPipelineType = FShaderPipelineType::GetShaderPipelineTypeByName(*ShaderPipelineTypeName);
			Key.ShaderPipelineTypeDependencies[ShaderPipelineIndex].ShaderPipelineType = FoundShaderPipelineType;
		}

		for (int32 VFTypeIndex = 0; VFTypeIndex < Key.VertexFactoryTypeDependencies.Num(); VFTypeIndex++)
		{
			const FString& VFTypeName = VertexFactoryTypeNames.FindChecked(Key.VertexFactoryTypeDependencies[VFTypeIndex].VertexFactoryType);
			FVertexFactoryType* FoundVFType = FVertexFactoryType::GetVFByName(VFTypeName);
			Key.VertexFactoryTypeDependencies[VFTypeIndex].VertexFactoryType = FoundVFType;
		}

		FMaterialShaderMap::GIdToMaterialShaderMap[Platform].Add(Key, Values[PairIndex]);
	}
#endif // WITH_EDITOR
}

#if WITH_EDITOR
void FMaterialShaderMap::LoadFromDerivedDataCache(const FMaterial* Material, const FMaterialShaderMapId& ShaderMapId, EShaderPlatform InPlatform, TRefCountPtr<FMaterialShaderMap>& InOutShaderMap)
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
			const FString DataKey = GetMaterialShaderMapKeyString(ShaderMapId, InPlatform);

			if (GetDerivedDataCacheRef().GetSynchronous(*DataKey, CachedData))
			{
				COOK_STAT(Timer.AddHit(CachedData.Num()));
				InOutShaderMap = new FMaterialShaderMap(InPlatform);
				FMemoryReader Ar(CachedData, true);

				// Deserialize from the cached data
				InOutShaderMap->Serialize(Ar);
				InOutShaderMap->RegisterSerializedShaders(false);

				const FString InDataKey = GetMaterialShaderMapKeyString(InOutShaderMap->GetShaderMapId(), InPlatform);
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

void FMaterialShaderMap::SaveToDerivedDataCache()
{
	COOK_STAT(auto Timer = MaterialShaderCookStats::UsageStats.TimeSyncWork());
	TArray<uint8> SaveData;
	FMemoryWriter Ar(SaveData, true);
	Serialize(Ar);

	GetDerivedDataCacheRef().Put(*GetMaterialShaderMapKeyString(ShaderMapId, GetShaderPlatform()), SaveData);
	COOK_STAT(Timer.AddMiss(SaveData.Num()));
}
#endif // WITH_EDITOR

TArray<uint8>* FMaterialShaderMap::BackupShadersToMemory()
{
	TArray<uint8>* SavedShaderData = new TArray<uint8>();
	FMemoryWriter Ar(*SavedShaderData);
	
	for (int32 Index = 0; Index < MeshShaderMaps.Num(); Index++)
	{
		// Serialize data needed to handle shader key changes in between the save and the load of the FShaders
		const bool bHandleShaderKeyChanges = true;
		MeshShaderMaps[Index].SerializeInline(Ar, true, bHandleShaderKeyChanges, false);
		MeshShaderMaps[Index].Empty();
	}

	SerializeInline(Ar, true, true, false);
	Empty();

	return SavedShaderData;
}

void FMaterialShaderMap::RestoreShadersFromMemory(const TArray<uint8>& ShaderData)
{
	FMemoryReader Ar(ShaderData);

	for (int32 Index = 0; Index < MeshShaderMaps.Num(); Index++)
	{
		// Use the serialized shader key data to detect when the saved shader is no longer valid and skip it
		const bool bHandleShaderKeyChanges = true;
		MeshShaderMaps[Index].SerializeInline(Ar, true, bHandleShaderKeyChanges, false);
		MeshShaderMaps[Index].RegisterSerializedShaders(false);
	}

	SerializeInline(Ar, true, true, false);
	RegisterSerializedShaders(false);
}

void FMaterialShaderMap::SaveForRemoteRecompile(FArchive& Ar, const TMap<FString, TArray<TRefCountPtr<FMaterialShaderMap> > >& CompiledShaderMaps, const TArray<FShaderResourceId>& ClientResourceIds)
{
	UE_LOG(LogMaterial, Display, TEXT("Looking for unique resources, %d were on client"), ClientResourceIds.Num());	

	// first, we look for the unique shader resources
	TArray<TRefCountPtr<FShaderResource>> UniqueResources;
	int32 NumSkippedResources = 0;

	for (TMap<FString, TArray<TRefCountPtr<FMaterialShaderMap> > >::TConstIterator It(CompiledShaderMaps); It; ++It)
	{
		const TArray<TRefCountPtr<FMaterialShaderMap> >& ShaderMapArray = It.Value();

		for (int32 Index = 0; Index < ShaderMapArray.Num(); Index++)
		{
			FMaterialShaderMap* ShaderMap = ShaderMapArray[Index];

			if (ShaderMap)
			{
				// get all shaders in the shader map
				TMap<FShaderId, FShader*> ShaderList;
				ShaderMap->GetShaderList(ShaderList);

				// get shaders from shader pipelines
				TArray<FShaderPipeline*> ShaderPipelineList;
				ShaderMap->GetShaderPipelineList(ShaderPipelineList);

				for (FShaderPipeline* ShaderPipeline : ShaderPipelineList)
				{
					for (FShader* Shader : ShaderPipeline->GetShaders())
					{
						FShaderId ShaderId = Shader->GetId();
						ShaderList.Add(ShaderId, Shader);
					}
				}

				// get the resources from the shaders
				for (auto& KeyValue : ShaderList)
				{
					FShader* Shader = KeyValue.Value;
					FShaderResourceId ShaderId = Shader->GetResourceId();

					// skip this shader if the Id was already on the client (ie, it didn't change)
					if (ClientResourceIds.Contains(ShaderId) == false)
					{
						// lookup the resource by ID and add it if it's unique
						UniqueResources.AddUnique(FShaderResource::FindById(ShaderId));
					}
					else
					{
						NumSkippedResources++;
					}
				}
			}
		}
	}

	UE_LOG(LogMaterial, Display, TEXT("Sending %d new shader resources, skipped %d existing"), UniqueResources.Num(), NumSkippedResources);

	// now serialize them
	int32 NumUniqueResources = UniqueResources.Num();
	Ar << NumUniqueResources;

	for (int32 Index = 0; Index < NumUniqueResources; Index++)
	{
		UniqueResources[Index]->Serialize(Ar, false);
	}

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

			if (ShaderMap && NumUniqueResources > 0)
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
#if WITH_EDITOR
	int32 NumResources;
	Ar << NumResources;

	// KeepAliveReferences keeps resources alive until we are finished serializing in this function
	TArray<TRefCountPtr<FShaderResource>> KeepAliveReferences;
	KeepAliveReferences.SetNum(NumResources);

	// load and register the resources
	for (int32 Index = 0; Index < NumResources; Index++)
	{
		// Load the inlined shader resource
		FShaderResource ResourceTemp;
		ResourceTemp.Serialize(Ar, false);

		KeepAliveReferences[Index] = FShaderResource::FindOrClone(MoveTemp(ResourceTemp));
	}

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
				FMaterialShaderMap* ShaderMap = new FMaterialShaderMap(ShaderPlatform);

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
#endif // WITH_EDITOR
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
			CompilingId = NextCompilingId;
			check(NextCompilingId < UINT_MAX);
			NextCompilingId++;
  
			TArray<FMaterial*> NewCorrespondingMaterials;
			NewCorrespondingMaterials.Add(Material);
			ShaderMapsBeingCompiled.Add(this, NewCorrespondingMaterials);
#if DEBUG_INFINITESHADERCOMPILE
			UE_LOG(LogTemp, Display, TEXT("Added material ShaderMap 0x%08X%08X with Material 0x%08X%08X to ShaderMapsBeingCompiled"), (int)((int64)(this) >> 32), (int)((int64)(this)), (int)((int64)(Material) >> 32), (int)((int64)(Material)));
#endif  
			// Setup the material compilation environment.
			Material->SetupMaterialEnvironment(InPlatform, InMaterialCompilationOutput.UniformExpressionSet, *MaterialEnvironment);
  
			MaterialCompilationOutput = InMaterialCompilationOutput;
			ShaderMapId = InShaderMapId;
			Platform = InPlatform;
			bIsPersistent = Material->IsPersistent();

#if ALLOW_SHADERMAP_DEBUG_DATA && WITH_EDITOR
			// Store the material name for debugging purposes.
			// Note: Material instances with static parameters will have the same FriendlyName for their shader maps!
			FriendlyName = Material->GetFriendlyName();

			// Log debug information about the material being compiled.
			const FString MaterialUsage = Material->GetMaterialUsageDescription();
			DebugDescription = FString::Printf(
				TEXT("Compiling %s: Platform=%s, Usage=%s"),
				*FriendlyName,
				*LegacyShaderPlatformToShaderFormat(InPlatform).ToString(),
				*MaterialUsage
				);
			for(int32 StaticSwitchIndex = 0;StaticSwitchIndex < ShaderMapId.GetStaticSwitchParameters().Num();++StaticSwitchIndex)
			{
				const FStaticSwitchParameter& StaticSwitchParameter = ShaderMapId.GetStaticSwitchParameters()[StaticSwitchIndex];
				DebugDescription += FString::Printf(
					TEXT(", StaticSwitch'%s'=%s"),
					*StaticSwitchParameter.ParameterInfo.ToString(),
					StaticSwitchParameter.Value ? TEXT("True") : TEXT("False")
					);
			}
			for(int32 StaticMaskIndex = 0;StaticMaskIndex < ShaderMapId.GetStaticComponentMaskParameters().Num();++StaticMaskIndex)
			{
				const FStaticComponentMaskParameter& StaticComponentMaskParameter = ShaderMapId.GetStaticComponentMaskParameters()[StaticMaskIndex];
				DebugDescription += FString::Printf(
					TEXT(", StaticMask'%s'=%s%s%s%s"),
					*StaticComponentMaskParameter.ParameterInfo.ToString(),
					StaticComponentMaskParameter.R ? TEXT("R") : TEXT(""),
					StaticComponentMaskParameter.G ? TEXT("G") : TEXT(""),
					StaticComponentMaskParameter.B ? TEXT("B") : TEXT(""),
					StaticComponentMaskParameter.A ? TEXT("A") : TEXT("")
					);
			}
			for(int32 StaticLayerIndex = 0;StaticLayerIndex < ShaderMapId.GetTerrainLayerWeightParameters().Num();++StaticLayerIndex)
			{
				const FStaticTerrainLayerWeightParameter& StaticTerrainLayerWeightParameter = ShaderMapId.GetTerrainLayerWeightParameters()[StaticLayerIndex];
				DebugDescription += FString::Printf(
					TEXT(", StaticTerrainLayer'%s'=%s"),
					*StaticTerrainLayerWeightParameter.ParameterInfo.ToString(),
					*FString::Printf(TEXT("Weightmap%u"),StaticTerrainLayerWeightParameter.WeightmapIndex)
					);
			}
			for (const auto &LayerParameterID :ShaderMapId.GetMaterialLayersParameterIDs())
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
				DebugDescription += FString::Printf(TEXT(", LayersParameter'%s'=[%s]"), *LayerParameterID.ParameterID.ParameterInfo.ToString(), *UUIDs);
			}
  
			UE_LOG(LogShaders, Display, TEXT("	%s"), *DebugDescription);

			FString DebugExtension = FString::Printf( TEXT("_%08x%08x"), ShaderMapId.BaseMaterialId.A, ShaderMapId.BaseMaterialId.B);
#elif ALLOW_SHADERMAP_DEBUG_DATA && !WITH_EDITOR
			FString DebugExtension = "";
			DebugDescription = "";
#else
			FString DebugExtension = "";
			FString DebugDescription = "";
#endif // ALLOW_SHADERMAP_DEBUG_DATA && WITH_EDITOR

			uint32 NumShaders = 0;
			uint32 NumVertexFactories = 0;
			TArray<FShaderCommonCompileJob*> NewJobs;
  
			// Iterate over all vertex factory types.
			for(TLinkedList<FVertexFactoryType*>::TIterator VertexFactoryTypeIt(FVertexFactoryType::GetTypeList());VertexFactoryTypeIt;VertexFactoryTypeIt.Next())
			{
				FVertexFactoryType* VertexFactoryType = *VertexFactoryTypeIt;
				check(VertexFactoryType);
  
				if(VertexFactoryType->IsUsedWithMaterials())
				{
					FMeshMaterialShaderMap* MeshShaderMap = nullptr;
  
					// look for existing map for this vertex factory type
					int32 MeshShaderMapIndex = INDEX_NONE;
					for (int32 ShaderMapIndex = 0; ShaderMapIndex < MeshShaderMaps.Num(); ShaderMapIndex++)
					{
						if (MeshShaderMaps[ShaderMapIndex].GetVertexFactoryType() == VertexFactoryType)
						{
							MeshShaderMap = &MeshShaderMaps[ShaderMapIndex];
							MeshShaderMapIndex = ShaderMapIndex;
							break;
						}
					}
  
					if (MeshShaderMap == nullptr)
					{
						// Create a new mesh material shader map.
						MeshShaderMapIndex = MeshShaderMaps.Num();
						MeshShaderMap = new FMeshMaterialShaderMap(InPlatform, VertexFactoryType);
						MeshShaderMaps.Add(MeshShaderMap);
					}
  
					// Enqueue compilation all mesh material shaders for this material and vertex factory type combo.
					const uint32 MeshShaders = MeshShaderMap->BeginCompile(
						CompilingId,
						InShaderMapId,
						Material,
						MaterialEnvironment,
						InPlatform,
						NewJobs,
						DebugDescription, 
						DebugExtension
						);
					NumShaders += MeshShaders;
					if (MeshShaders > 0)
					{
						NumVertexFactories++;
					}
				}
			}
  
			// Iterate over all material shader types.
			TMap<TShaderTypePermutation<const FShaderType>, FShaderCompileJob*> SharedShaderJobs;
			for(TLinkedList<FShaderType*>::TIterator ShaderTypeIt(FShaderType::GetTypeList());ShaderTypeIt;ShaderTypeIt.Next())
			{
				FMaterialShaderType* ShaderType = ShaderTypeIt->GetMaterialShaderType();
				const int32 PermutationCount = ShaderType ? ShaderType->GetPermutationCount() : 0;
				for (int32 PermutationId = 0; PermutationId < PermutationCount; ++PermutationId)
				{
					if (ShouldCacheMaterialShader(ShaderType, InPlatform, Material, PermutationId))
					{
#if WITH_EDITOR
						// Verify that the shader map Id contains inputs for any shaders that will be put into this shader map
						check(InShaderMapId.ContainsShaderType(ShaderType, kUniqueShaderPermutationId));
#endif
						// Compile this material shader for this material.
						TArray<FString> ShaderErrors;

						// Only compile the shader if we don't already have it
						if (!HasShader(ShaderType, PermutationId))
						{
							auto* Job = ShaderType->BeginCompileShader(
								CompilingId,
								PermutationId,
								Material,
								MaterialEnvironment,
								nullptr,
								InPlatform,
								NewJobs,
								DebugDescription, DebugExtension
							);

							TShaderTypePermutation<const FShaderType> ShaderTypePermutation(ShaderType, PermutationId);
							check(!SharedShaderJobs.Find(ShaderTypePermutation));
							SharedShaderJobs.Add(ShaderTypePermutation, Job);
						}
						NumShaders++;
					}
				}
			}
  
			const bool bHasTessellation = Material->GetTessellationMode() != MTM_NoTessellation;
			for (TLinkedList<FShaderPipelineType*>::TIterator ShaderPipelineIt(FShaderPipelineType::GetTypeList());ShaderPipelineIt;ShaderPipelineIt.Next())
			{
				const FShaderPipelineType* Pipeline = *ShaderPipelineIt;
				if (Pipeline->IsMaterialTypePipeline() && Pipeline->HasTessellation() == bHasTessellation && RHISupportsShaderPipelines(InPlatform))
				{
					auto& StageTypes = Pipeline->GetStages();
					TArray<FMaterialShaderType*> ShaderStagesToCompile;
					for (int32 Index = 0; Index < StageTypes.Num(); ++Index)
					{
						FMaterialShaderType* ShaderType = (FMaterialShaderType*)(StageTypes[Index]->GetMaterialShaderType());
						if (ShaderType && ShouldCacheMaterialShader(ShaderType, InPlatform, Material, kUniqueShaderPermutationId))
						{
#if WITH_EDITOR
							// Verify that the shader map Id contains inputs for any shaders that will be put into this shader map
							check(InShaderMapId.ContainsShaderType(ShaderType, kUniqueShaderPermutationId));
#endif
							ShaderStagesToCompile.Add(ShaderType);
						}
						else
						{
							break;
						}
					}

					if (ShaderStagesToCompile.Num() == StageTypes.Num())
					{
						// Verify that the shader map Id contains inputs for any shaders that will be put into this shader map
#if WITH_EDITOR
						check(InShaderMapId.ContainsShaderPipelineType(Pipeline));
#endif
						if (Pipeline->ShouldOptimizeUnusedOutputs(InPlatform))
						{
							NumShaders += ShaderStagesToCompile.Num();
							FMaterialShaderType::BeginCompileShaderPipeline(CompilingId, InPlatform, Material, MaterialEnvironment, Pipeline, ShaderStagesToCompile, NewJobs, DebugDescription, DebugExtension);
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
			}

			UE_LOG(LogShaders, Log, TEXT("		%u Shaders among %u VertexFactories"), NumShaders, NumVertexFactories);

			// Register this shader map in the global map with the material's ID.
			Register(InPlatform);
  
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
#if ALLOW_SHADERMAP_DEBUG_DATA
				   *FriendlyName,
#else
				   nullptr,
#endif
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

#if ALLOW_SHADERMAP_DEBUG_DATA
	CompileTime += SingleJob->Output.CompileTime;
#endif
	FShader* Shader = nullptr;
	if (CurrentJob.VFType)
	{
		FVertexFactoryType* VertexFactoryType = CurrentJob.VFType;
		check(VertexFactoryType->IsUsedWithMaterials());
		FMeshMaterialShaderMap* MeshShaderMap = nullptr;
		int32 MeshShaderMapIndex = INDEX_NONE;

		// look for existing map for this vertex factory type
		for (int32 ShaderMapIndex = 0; ShaderMapIndex < MeshShaderMaps.Num(); ShaderMapIndex++)
		{
			if (MeshShaderMaps[ShaderMapIndex].GetVertexFactoryType() == VertexFactoryType)
			{
				MeshShaderMap = &MeshShaderMaps[ShaderMapIndex];
				MeshShaderMapIndex = ShaderMapIndex;
				break;
			}
		}

		check(MeshShaderMap);
		FMeshMaterialShaderType* MeshMaterialShaderType = CurrentJob.ShaderType->GetMeshMaterialShaderType();
		check(MeshMaterialShaderType);
		Shader = MeshMaterialShaderType->FinishCompileShader(MaterialCompilationOutput.UniformExpressionSet, MaterialShaderMapHash, CurrentJob, ShaderPipeline,
#if ALLOW_SHADERMAP_DEBUG_DATA
			FriendlyName);
#else
			FString());
#endif
		check(Shader);
		if (!ShaderPipeline)
		{
			check(!MeshShaderMap->HasShader(MeshMaterialShaderType, CurrentJob.PermutationId));
			MeshShaderMap->AddShader(MeshMaterialShaderType, CurrentJob.PermutationId, Shader);
		}
	}
	else
	{
		FMaterialShaderType* MaterialShaderType = CurrentJob.ShaderType->GetMaterialShaderType();
		check(MaterialShaderType);
		Shader = MaterialShaderType->FinishCompileShader(MaterialCompilationOutput.UniformExpressionSet, MaterialShaderMapHash, CurrentJob, ShaderPipeline,
#if ALLOW_SHADERMAP_DEBUG_DATA
			FriendlyName);
#else
			FString());
#endif

		check(Shader);
		if (!ShaderPipeline)
		{
			check(!HasShader(MaterialShaderType, CurrentJob.PermutationId));
			AddShader(MaterialShaderType, CurrentJob.PermutationId, Shader);
		}
	}

#if WITH_EDITOR
	// add shader source to 
	ShaderProcessedSource.Add(CurrentJob.ShaderType->GetFName(), CurrentJob.Output.OptionalFinalShaderSource);
#endif

	return Shader;
}

#if WITH_EDITOR
bool FMaterialShaderMap::ProcessCompilationResults(const TArray<FShaderCommonCompileJob*>& InCompilationResults, int32& InOutJobIndex, float& TimeBudget, TMap<const FVertexFactoryType*, TArray<const FShaderPipelineType*> >& SharedPipelines)
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

			TArray<FShader*> ShaderStages;
			FVertexFactoryType* VertexFactoryType = CurrentJob.StageJobs[0]->GetSingleShaderJob()->VFType;
			for (int32 Index = 0; Index < CurrentJob.StageJobs.Num(); ++Index)
			{
				SingleJob = CurrentJob.StageJobs[Index]->GetSingleShaderJob();
				FShader* Shader = ProcessCompilationResultsForSingleJob(SingleJob, PipelineJob->ShaderPipeline, MaterialShaderMapHash);
				ShaderStages.Add(Shader);
				check(VertexFactoryType == CurrentJob.StageJobs[Index]->GetSingleShaderJob()->VFType);
			}

			FShaderPipeline* ShaderPipeline = new FShaderPipeline(PipelineJob->ShaderPipeline, ShaderStages);
			if (ShaderPipeline)
			{
				if (VertexFactoryType)
				{
					check(VertexFactoryType->IsUsedWithMaterials());
					FMeshMaterialShaderMap* MeshShaderMap = nullptr;
					int32 MeshShaderMapIndex = INDEX_NONE;

					// look for existing map for this vertex factory type
					for (int32 ShaderMapIndex = 0; ShaderMapIndex < MeshShaderMaps.Num(); ShaderMapIndex++)
					{
						if (MeshShaderMaps[ShaderMapIndex].GetVertexFactoryType() == VertexFactoryType)
						{
							MeshShaderMap = &MeshShaderMaps[ShaderMapIndex];
							MeshShaderMapIndex = ShaderMapIndex;
							break;
						}
					}

					check(MeshShaderMap);
					check(!MeshShaderMap->HasShaderPipeline(ShaderPipeline->PipelineType));
					MeshShaderMap->AddShaderPipeline(PipelineJob->ShaderPipeline, ShaderPipeline);
				}
				else
				{
					check(!HasShaderPipeline(ShaderPipeline->PipelineType));
					AddShaderPipeline(PipelineJob->ShaderPipeline, ShaderPipeline);
				}
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
			for (int32 ShaderMapIndex = 0; ShaderMapIndex < MeshShaderMaps.Num(); ShaderMapIndex++)
			{
				auto* MeshShaderMap = &MeshShaderMaps[ShaderMapIndex];
				auto* VertexFactory = MeshShaderMap->GetVertexFactoryType();
				auto* FoundSharedPipelines = SharedPipelines.Find(VertexFactory);
				if (VertexFactory && FoundSharedPipelines)
				{
					for (const FShaderPipelineType* ShaderPipelineType : *FoundSharedPipelines)
					{
						if (ShaderPipelineType->IsMeshMaterialTypePipeline() && !MeshShaderMap->HasShaderPipeline(ShaderPipelineType))
						{
							auto& StageTypes = ShaderPipelineType->GetStages();
							TArray<FShader*> ShaderStages;
							for (int32 Index = 0; Index < StageTypes.Num(); ++Index)
							{
								FMeshMaterialShaderType* ShaderType = ((FMeshMaterialShaderType*)(StageTypes[Index]))->GetMeshMaterialShaderType();
								FShader* Shader = MeshShaderMap->GetShader(ShaderType, kUniqueShaderPermutationId);
								check(Shader);
								ShaderStages.Add(Shader);
							}

							checkf(StageTypes.Num() == ShaderStages.Num(), TEXT("Internal Error adding MeshMaterial ShaderPipeline %s"), ShaderPipelineType->GetName());
							FShaderPipeline* ShaderPipeline = new FShaderPipeline(ShaderPipelineType, ShaderStages);
							MeshShaderMap->AddShaderPipeline(ShaderPipelineType, ShaderPipeline);
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
					if (ShaderPipelineType->IsMaterialTypePipeline() && !HasShaderPipeline(ShaderPipelineType))
					{
						auto& StageTypes = ShaderPipelineType->GetStages();
						TArray<FShader*> ShaderStages;
						for (int32 Index = 0; Index < StageTypes.Num(); ++Index)
						{
							FMaterialShaderType* ShaderType = ((FMaterialShaderType*)(StageTypes[Index]))->GetMaterialShaderType();
							FShader* Shader = GetShader(ShaderType, kUniqueShaderPermutationId);
							check(Shader);
							ShaderStages.Add(Shader);
						}

						checkf(StageTypes.Num() == ShaderStages.Num(), TEXT("Internal Error adding Material ShaderPipeline %s"), ShaderPipelineType->GetName());
						FShaderPipeline* ShaderPipeline = new FShaderPipeline(ShaderPipelineType, ShaderStages);
						AddShaderPipeline(ShaderPipelineType, ShaderPipeline);
					}
				}
			}
		}

		for (int32 ShaderMapIndex = MeshShaderMaps.Num() - 1; ShaderMapIndex >= 0; ShaderMapIndex--)
		{
			if (MeshShaderMaps[ShaderMapIndex].GetNumShaders() == 0 && MeshShaderMaps[ShaderMapIndex].GetNumShaderPipelines() == 0)
			{
				// If the mesh material shader map is complete and empty, discard it.
				MeshShaderMaps.RemoveAt(ShaderMapIndex);
			}
		}

		// Reinitialize the ordered mesh shader maps
		InitOrderedMeshShaderMaps();

		// Add the persistent shaders to the local shader cache.
		if (bIsPersistent)
		{
			SaveToDerivedDataCache();
		}

		// The shader map can now be used on the rendering thread
		bCompilationFinalized = true;

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

bool FMaterialShaderMap::IsMaterialShaderComplete(
	const FMaterial* Material,
	const FMaterialShaderType* ShaderType,
	const FShaderPipelineType* Pipeline,
	int32 PermutationId,
	bool bSilent) const
{
	const bool bShouldCacheMaterialShader = ShouldCacheMaterialShader(ShaderType, Platform, Material, PermutationId);
	const bool bMissingShaderPipeline = (Pipeline && !HasShaderPipeline(Pipeline));
	const bool bMissingShader = (!Pipeline && !HasShader((FShaderType*)ShaderType, PermutationId));

	if (bShouldCacheMaterialShader && (bMissingShaderPipeline || bMissingShader))
	{
		if (!bSilent)
		{
			if (Pipeline)
			{
				UE_LOG(LogShaders, Warning, TEXT("Incomplete material %s, missing pipeline %s."), *Material->GetFriendlyName(), Pipeline->GetName());
			}
			else
			{
				UE_LOG(LogShaders, Warning, TEXT("Incomplete material %s, missing FMaterialShader (%s, %d)."), *Material->GetFriendlyName(), ShaderType->GetName(), PermutationId);
			}
		}
		return false;
	}

	return true;
}

bool FMaterialShaderMap::IsComplete(const FMaterial* Material, bool bSilent)
{
	// Make sure we are operating on a referenced shader map or the below Find will cause this shader map to be deleted,
	// Since it creates a temporary ref counted pointer.
	check(NumRefs > 0);
	const TArray<FMaterial*>* CorrespondingMaterials = FMaterialShaderMap::ShaderMapsBeingCompiled.Find(this);

	if (CorrespondingMaterials)
	{
		check(!bCompilationFinalized);
		return false;
	}

	// Iterate over all vertex factory types.
	for(TLinkedList<FVertexFactoryType*>::TIterator VertexFactoryTypeIt(FVertexFactoryType::GetTypeList());VertexFactoryTypeIt;VertexFactoryTypeIt.Next())
	{
		FVertexFactoryType* VertexFactoryType = *VertexFactoryTypeIt;

		if(VertexFactoryType->IsUsedWithMaterials())
		{
			// Find the shaders for this vertex factory type.
			const FMeshMaterialShaderMap* MeshShaderMap = GetMeshShaderMap(VertexFactoryType);
			if (!FMeshMaterialShaderMap::IsComplete(MeshShaderMap,GetShaderPlatform(),Material,VertexFactoryType,bSilent))
			{
				if (!MeshShaderMap && !bSilent)
				{
					UE_LOG(LogShaders, Warning, TEXT("Incomplete material %s, missing Vertex Factory %s."), *Material->GetFriendlyName(), VertexFactoryType->GetName());
				}
				return false;
			}
		}
	}

	// Iterate over all material shader types.
	for(TLinkedList<FShaderType*>::TIterator ShaderTypeIt(FShaderType::GetTypeList());ShaderTypeIt;ShaderTypeIt.Next())
	{
		// Find this shader type in the material's shader map.
		FMaterialShaderType* ShaderType = ShaderTypeIt->GetMaterialShaderType();
		const int32 PermutationCount = ShaderType ? ShaderType->GetPermutationCount() : 0;
		for (int32 PermutationId = 0; PermutationId < PermutationCount; ++PermutationId)
		{
			if (!IsMaterialShaderComplete(Material, ShaderType, nullptr, PermutationId, bSilent))
			{
				return false;
			}
		}
	}

	// Iterate over all pipeline types
	const bool bHasTessellation = Material->GetTessellationMode() != MTM_NoTessellation;
	for (TLinkedList<FShaderPipelineType*>::TIterator ShaderPipelineIt(FShaderPipelineType::GetTypeList());ShaderPipelineIt;ShaderPipelineIt.Next())
	{
		const FShaderPipelineType* Pipeline = *ShaderPipelineIt;
		if (Pipeline->IsMaterialTypePipeline() && Pipeline->HasTessellation() == bHasTessellation)
		{
			auto& StageTypes = Pipeline->GetStages();

			int32 NumShouldCache = 0;
			for (int32 Index = 0; Index < StageTypes.Num(); ++Index)
			{
				auto* ShaderType = StageTypes[Index]->GetMaterialShaderType();
				if (ShouldCacheMaterialShader(ShaderType, GetShaderPlatform(), Material, kUniqueShaderPermutationId))
				{
					++NumShouldCache;
				}
				else
				{
					break;
				}
			}

			if (NumShouldCache == StageTypes.Num())
			{
				for (int32 Index = 0; Index < StageTypes.Num(); ++Index)
				{
					auto* ShaderType = StageTypes[Index]->GetMaterialShaderType();
					if (!IsMaterialShaderComplete(Material, ShaderType, Pipeline, kUniqueShaderPermutationId, bSilent))
					{
						return false;
					}
				}
			}
		}
	}

	return true;
}

#if WITH_EDITOR
void FMaterialShaderMap::LoadMissingShadersFromMemory(const FMaterial* Material)
{
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
}
#endif // WITH_EDITOR

#if WITH_EDITOR
const FString* FMaterialShaderMap::GetShaderSource(const FName ShaderTypeName) const
{
	const FString* Source = ShaderProcessedSource.Find(ShaderTypeName);
	return Source;
}
#endif

void FMaterialShaderMap::GetShaderList(TMap<FShaderId, FShader*>& OutShaders) const
{
	TShaderMap<FMaterialShaderType>::GetShaderList(OutShaders);
	for(int32 Index = 0;Index < MeshShaderMaps.Num();Index++)
	{
		MeshShaderMaps[Index].GetShaderList(OutShaders);
	}
}

void FMaterialShaderMap::GetShaderList(TMap<FName, FShader*>& OutShaders) const
{
	TShaderMap<FMaterialShaderType>::GetShaderList(OutShaders);
	for (int32 Index = 0; Index < MeshShaderMaps.Num(); Index++)
	{
		MeshShaderMaps[Index].GetShaderList(OutShaders);
	}
}

void FMaterialShaderMap::GetShaderPipelineList(TArray<FShaderPipeline*>& OutShaderPipelines) const
{
	TShaderMap<FMaterialShaderType>::GetShaderPipelineList(OutShaderPipelines, FShaderPipeline::EAll);
	for (int32 Index = 0;Index < MeshShaderMaps.Num();Index++)
	{
		MeshShaderMaps[Index].GetShaderPipelineList(OutShaderPipelines, FShaderPipeline::EAll);
	}
}

uint32 FMaterialShaderMap::GetShaderNum() const
{
	uint32 Count = Shaders.Num();
	for (int32 Index = 0; Index < MeshShaderMaps.Num(); Index++)
	{
		Count += MeshShaderMaps[Index].GetShaderNum();
	}
	return Count;
}

/**
 * Registers a material shader map in the global map so it can be used by materials.
 */
void FMaterialShaderMap::Register(EShaderPlatform InShaderPlatform)
{
	extern int32 GCreateShadersOnLoad;
	if (GCreateShadersOnLoad && GetShaderPlatform() == InShaderPlatform)
	{
		for (auto KeyValue : GetShaders())
		{
			FShader* Shader = KeyValue.Value;
			if (Shader)
			{
				Shader->BeginInitializeResources();
			}
		}

		for (int32 Index = 0; Index < MeshShaderMaps.Num(); Index++)
		{
			for (TMap<FMeshMaterialShaderMap::FShaderPrimaryKey, TRefCountPtr<FShader> >::TConstIterator It(MeshShaderMaps[Index].GetShaders()); It; ++It)
			{
				FShader* Shader = It.Value();
				if (Shader)
				{
					Shader->BeginInitializeResources();
				}
			}
		}
	}

	if (!bRegistered)
	{
		INC_DWORD_STAT(STAT_Shaders_NumShaderMaps);
		INC_DWORD_STAT_BY(STAT_Shaders_ShaderMapMemory, GetSizeBytes());
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
				DEC_DWORD_STAT_BY(STAT_Shaders_ShaderMapMemory, GetSizeBytes());

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

FMaterialShaderMap::FMaterialShaderMap(EShaderPlatform InPlatform) :
	TShaderMap<FMaterialShaderType>(InPlatform),
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
	if(GShaderCompilerStats != 0)
	{
		FString Path = !MaterialPath.IsEmpty() ? MaterialPath : GetFriendlyName();
		GShaderCompilerStats->RegisterCookedShaders(GetShaderNum(), CompileTime, Platform, Path, GetDebugDescription());
	}
	AllMaterialShaderMaps.RemoveSwap(this);
#endif
}

/**
 * Removes all entries in the cache with exceptions based on a shader type
 * @param ShaderType - The shader type to flush
 */
void FMaterialShaderMap::FlushShadersByShaderType(FShaderType* ShaderType)
{
	// flush from all the vertex factory shader maps
	for(int32 Index = 0;Index < MeshShaderMaps.Num();Index++)
	{
		MeshShaderMaps[Index].FlushShadersByShaderType(ShaderType);
	}

	if (ShaderType->GetMaterialShaderType())
	{
		const int32 PermutationCount = ShaderType->GetPermutationCount();
		for (int32 PermutationId = 0; PermutationId < PermutationCount; ++PermutationId)
		{
			RemoveShaderTypePermutaion(ShaderType->GetMaterialShaderType(), PermutationId);
		}
	}
}

void FMaterialShaderMap::FlushShadersByShaderPipelineType(const FShaderPipelineType* ShaderPipelineType)
{
	// flush from all the vertex factory shader maps
	for (int32 Index = 0;Index < MeshShaderMaps.Num();Index++)
	{
		MeshShaderMaps[Index].FlushShadersByShaderPipelineType(ShaderPipelineType);
	}

	if (ShaderPipelineType->IsMaterialTypePipeline())
	{
		RemoveShaderPipelineType(ShaderPipelineType);
	}
}


/**
 * Removes all entries in the cache with exceptions based on a vertex factory type
 * @param ShaderType - The shader type to flush
 */
void FMaterialShaderMap::FlushShadersByVertexFactoryType(const FVertexFactoryType* VertexFactoryType)
{
	for (int32 Index = 0; Index < MeshShaderMaps.Num(); Index++)
	{
		FVertexFactoryType* VFType = MeshShaderMaps[Index].GetVertexFactoryType();
		// determine if this shaders vertex factory type should be flushed
		if (VFType == VertexFactoryType)
		{
			// remove the shader map
			MeshShaderMaps.RemoveAt(Index);
			// fix up the counter
			Index--;
		}
	}

	// reset the OrderedMeshShaderMap to remove references to the removed maps
	InitOrderedMeshShaderMaps();
}

struct FCompareMeshShaderMaps
{
	FORCEINLINE bool operator()(const FMeshMaterialShaderMap& A, const FMeshMaterialShaderMap& B ) const
	{
		return FCString::Strncmp(
			A.GetVertexFactoryType()->GetName(), 
			B.GetVertexFactoryType()->GetName(), 
			FMath::Min(FCString::Strlen(A.GetVertexFactoryType()->GetName()), FCString::Strlen(B.GetVertexFactoryType()->GetName()))) > 0;
	}
};

void FMaterialShaderMap::Serialize(FArchive& Ar, bool bInlineShaderResources, bool bLoadedByCookedMaterial)
{
	LLM_SCOPE(ELLMTag::Shaders);
	// Note: This is saved to the DDC, not into packages (except when cooked)
	// Backwards compatibility therefore will not work based on the version of Ar
	// Instead, just bump MATERIALSHADERMAP_DERIVEDDATA_VER

	ShaderMapId.Serialize(Ar, bLoadedByCookedMaterial);

	// serialize the platform enum as a uint8
	int32 TempPlatform = (int32)GetShaderPlatform();
	Ar << TempPlatform;
	Platform = (EShaderPlatform)TempPlatform;

#if ALLOW_SHADERMAP_DEBUG_DATA
	Ar << FriendlyName;
	Ar << MaterialPath;
#else
	FString TempString;
	Ar << TempString;
	Ar << TempString;
#endif

	MaterialCompilationOutput.Serialize(Ar);

#if ALLOW_SHADERMAP_DEBUG_DATA
	Ar << DebugDescription;
#else
	Ar << TempString;
#endif

	if (Ar.IsSaving())
	{
		// Material shaders
		TShaderMap<FMaterialShaderType>::SerializeInline(Ar, bInlineShaderResources, false, false);

		// Mesh material shaders
		int32 NumMeshShaderMaps = 0;

		for (int32 VFIndex = 0; VFIndex < OrderedMeshShaderMaps.Num(); VFIndex++)
		{
			FMeshMaterialShaderMap* MeshShaderMap = OrderedMeshShaderMaps[VFIndex];

			if (MeshShaderMap)
			{
				// Count the number of non-empty mesh shader maps
				NumMeshShaderMaps++;
			}
		}

		Ar << NumMeshShaderMaps;

		TArray<FMeshMaterialShaderMap*> SortedMeshShaderMaps;
		SortedMeshShaderMaps.Empty(MeshShaderMaps.Num());

		for (int32 MapIndex = 0; MapIndex < MeshShaderMaps.Num(); MapIndex++)
		{
			SortedMeshShaderMaps.Add(&MeshShaderMaps[MapIndex]);
		}

		// Sort mesh shader maps by VF name so that the DDC entry always has the same binary result for a given key
		SortedMeshShaderMaps.Sort(FCompareMeshShaderMaps());

		for (int32 MapIndex = 0; MapIndex < SortedMeshShaderMaps.Num(); MapIndex++)
		{
			FMeshMaterialShaderMap* MeshShaderMap = SortedMeshShaderMaps[MapIndex];

			if (MeshShaderMap)
			{
				FVertexFactoryType* VFType = MeshShaderMap->GetVertexFactoryType();
				check(VFType);

				Ar << VFType;

				MeshShaderMap->SerializeInline(Ar, bInlineShaderResources, false, false);
			}
		}

		bool bCooking = Ar.IsCooking();
		Ar << bCooking;

#if WITH_EDITOR
		if (!bCooking)
		{
			int32 NrShaderSources = ShaderProcessedSource.Num();
			Ar << NrShaderSources;
			for (auto SourceEntry : ShaderProcessedSource)
			{
				Ar << SourceEntry.Key;
				Ar << SourceEntry.Value;
			}
		}
#endif
	}

	if (Ar.IsLoading())
	{
		MeshShaderMaps.Empty();

		// Material shaders
		TShaderMap<FMaterialShaderType>::SerializeInline(Ar, bInlineShaderResources, false, bLoadedByCookedMaterial);

		// Mesh material shaders
		int32 NumMeshShaderMaps = 0;
		Ar << NumMeshShaderMaps;

		MeshShaderMaps.Empty(NumMeshShaderMaps);
		OrderedMeshShaderMaps.Empty(FVertexFactoryType::GetNumVertexFactoryTypes());
		OrderedMeshShaderMaps.AddZeroed(FVertexFactoryType::GetNumVertexFactoryTypes());

		for (int32 VFIndex = 0; VFIndex < NumMeshShaderMaps; VFIndex++)
		{
			FVertexFactoryType* VFType = nullptr;
			Ar << VFType;
			// Not handling missing vertex factory types on cooked data
			// The cooker binary and running binary are assumed to be on the same code version
			check(VFType);

			FMeshMaterialShaderMap* MeshShaderMap = new FMeshMaterialShaderMap(GetShaderPlatform(), VFType);
			MeshShaderMap->SerializeInline(Ar, bInlineShaderResources, false, bLoadedByCookedMaterial);
			MeshShaderMaps.Add(MeshShaderMap);
			OrderedMeshShaderMaps[VFType->GetId()] = MeshShaderMap;
		}

		bool bCooked;
		Ar << bCooked;

#if WITH_EDITOR
		if (!bCooked)
		{
			int32 NrShaderSources = 0;
			Ar << NrShaderSources;

			ShaderProcessedSource.Empty();
			FString ShaderSourceSrc;
			FName ShaderName;
			for (int32 i = 0; i < NrShaderSources; ++i)
			{
				Ar << ShaderName;
				Ar << ShaderSourceSrc;

				ShaderProcessedSource.Add(ShaderName, ShaderSourceSrc);
			}
		}
#endif
	}
}

void FMaterialShaderMap::RegisterSerializedShaders(bool bLoadedByCookedMaterial)
{
	check(IsInGameThread());

	TShaderMap<FMaterialShaderType>::RegisterSerializedShaders(bLoadedByCookedMaterial);
	
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
	TShaderMap<FMaterialShaderType>::DiscardSerializedShaders();

	for (int32 VFIndex = 0; VFIndex < OrderedMeshShaderMaps.Num(); VFIndex++)
	{
		OrderedMeshShaderMaps[VFIndex] = nullptr;
	}

	for (int32 Index = MeshShaderMaps.Num() - 1; Index >= 0; Index--)
	{
		MeshShaderMaps[Index].DiscardSerializedShaders();
	}
	MeshShaderMaps.Empty();
}

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
	uint32 MaxTextureSamplers = GetMaxTextureSamplersShaderMap();

	for (int32 Index = 0;Index < MeshShaderMaps.Num();Index++)
	{
		MaxTextureSamplers = FMath::Max(MaxTextureSamplers, MeshShaderMaps[Index].GetMaxTextureSamplersShaderMap());
	}

	return MaxTextureSamplers;
}
#endif // WITH_EDITOR

const FMeshMaterialShaderMap* FMaterialShaderMap::GetMeshShaderMap(FVertexFactoryType* VertexFactoryType) const
{
	checkSlow(bCompilationFinalized);
#if WITH_EDITOR 
	// Attempt to get some more info for a rare crash (UE-35937)
	checkf(OrderedMeshShaderMaps.Num() > 0 && bCompilationFinalized, TEXT("OrderedMeshShaderMaps.Num() is %d. bCompilationFinalized is %d. This may relate to bug UE-35937"), OrderedMeshShaderMaps.Num(), (int)bCompilationFinalized);
#endif
	const FMeshMaterialShaderMap* MeshShaderMap = OrderedMeshShaderMaps[VertexFactoryType->GetId()];
	checkSlow(!MeshShaderMap || MeshShaderMap->GetVertexFactoryType() == VertexFactoryType);
	return MeshShaderMap;
}

void FMaterialShaderMap::InitOrderedMeshShaderMaps()
{
	OrderedMeshShaderMaps.Empty(FVertexFactoryType::GetNumVertexFactoryTypes());
	OrderedMeshShaderMaps.AddZeroed(FVertexFactoryType::GetNumVertexFactoryTypes());

	for (int32 Index = 0;Index < MeshShaderMaps.Num();Index++)
	{
		check(MeshShaderMaps[Index].GetVertexFactoryType());
		const int32 VFIndex = MeshShaderMaps[Index].GetVertexFactoryType()->GetId();
		OrderedMeshShaderMaps[VFIndex] = &MeshShaderMaps[Index];
	}
}

void FMaterialShaderMap::DumpDebugInfo()
{
	const FString& FriendlyNameS = GetFriendlyName();
	UE_LOG(LogConsoleResponse, Display, TEXT("FMaterialShaderMap:  FriendlyName %s"), *FriendlyNameS);
	const FString& DebugDescriptionS = GetDebugDescription();
	UE_LOG(LogConsoleResponse, Display, TEXT("  DebugDescription %s"), *DebugDescriptionS);

	TMap<FShaderId, FShader*> ShadersL;
	GetShaderList(ShadersL);
	UE_LOG(LogConsoleResponse, Display, TEXT("  --- %d shaders"), ShadersL.Num());
	int32 Index = 0;
	for (auto& KeyValue : ShadersL)
	{
		UE_LOG(LogConsoleResponse, Display, TEXT("    --- shader %d"), Index);
		FShader* Shader = KeyValue.Value;
		Shader->DumpDebugInfo();
		Index++;
	}
	//TArray<FShaderPipeline*> ShaderPipelinesL;
	//GetShaderPipelineList(ShaderPipelinesL);
}

void FMaterialShaderMap::SaveShaderStableKeys(EShaderPlatform TargetShaderPlatform, const FStableShaderKeyAndValue& SaveKeyVal)
{
#if WITH_EDITOR
	TMap<FShaderId, FShader*> ShadersL;
	GetShaderList(ShadersL);
	for (auto& KeyValue : ShadersL)
	{
		FShader* Shader = KeyValue.Value;
		Shader->SaveShaderStableKeys(TargetShaderPlatform, SaveKeyVal);
	}

	TArray<FShaderPipeline*> ShadersPipelinesL;
	GetShaderPipelineList(ShadersPipelinesL);
	for (FShaderPipeline* Pipeline : ShadersPipelinesL)
	{
		Pipeline->SaveShaderStableKeys(TargetShaderPlatform, SaveKeyVal);
	}
#endif
}


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
	TMultiMap<FString,FShader*> MaterialToShaderMap;
	TMultiMap<FString, FShaderPipeline*> MaterialToShaderPipelineMap;

	// Set of material names.
	TSet<FString> MaterialNames;

	// Look at in-memory shader use.
	for (int32 ShaderMapIndex = 0; ShaderMapIndex < FMaterialShaderMap::AllMaterialShaderMaps.Num(); ShaderMapIndex++)
	{
		FMaterialShaderMap* MaterialShaderMap = FMaterialShaderMap::AllMaterialShaderMaps[ShaderMapIndex];
		TMap<FShaderId, FShader*> Shaders;
		TArray<FShaderPipeline*> ShaderPipelines;
		MaterialShaderMap->GetShaderList(Shaders);
		MaterialShaderMap->GetShaderPipelineList(ShaderPipelines);

		// Add friendly name to list of materials.
		FString FriendlyName = MaterialShaderMap->GetFriendlyName();
		MaterialNames.Add(FriendlyName);

		// Add shaders to mapping per friendly name as there might be multiple
		for (auto& KeyValue : Shaders)
		{
			FShader* Shader = KeyValue.Value;
			MaterialToShaderMap.AddUnique(FriendlyName, Shader);
		}

		for (FShaderPipeline* Pipeline : ShaderPipelines)
		{
			for (FShader* Shader : Pipeline->GetShaders())
			{
				MaterialToShaderMap.AddUnique(FriendlyName, Shader);
			}
			MaterialToShaderPipelineMap.AddUnique(FriendlyName, Pipeline);
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
		TArray<FShader*> Shaders;
		MaterialToShaderMap.MultiFind( *It, Shaders );
		TArray<FShaderPipeline*> ShaderPipelines;
		MaterialToShaderPipelineMap.MultiFind(*It, ShaderPipelines);
		
		// Iterate over shaders and gather stats.
		int32 CodeSize = 0;
		for( int32 ShaderIndex=0; ShaderIndex<Shaders.Num(); ShaderIndex++ )
		{
			FShader* Shader = Shaders[ShaderIndex];
			CodeSize += Shader->GetCode().Num();
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

