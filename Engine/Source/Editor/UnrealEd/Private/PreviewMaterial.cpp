// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "MaterialEditor/PreviewMaterial.h"
#include "Modules/ModuleManager.h"
#include "MaterialEditor/DEditorParameterValue.h"
#include "MaterialEditor/DEditorFontParameterValue.h"
#include "MaterialEditor/DEditorMaterialLayersParameterValue.h"
#include "MaterialEditor/DEditorScalarParameterValue.h"
#include "MaterialEditor/DEditorStaticComponentMaskParameterValue.h"
#include "MaterialEditor/DEditorStaticSwitchParameterValue.h"
#include "MaterialEditor/DEditorTextureParameterValue.h"
#include "MaterialEditor/DEditorVectorParameterValue.h"
#include "AI/Navigation/NavigationSystem.h"
#include "MaterialEditor/MaterialEditorInstanceConstant.h"
#include "MaterialEditor/MaterialEditorMeshComponent.h"
#include "MaterialEditorModule.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialFunctionInstance.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionFontSampleParameter.h"
#include "Materials/MaterialExpressionMaterialAttributeLayers.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionStaticComponentMaskParameter.h"
#include "UObjectIterator.h"

/**
 * Class for rendering the material on the preview mesh in the Material Editor
 */
class FPreviewMaterial : public FMaterialResource, public FMaterialRenderProxy
{
public:
	FPreviewMaterial()
	:	FMaterialResource()
	{
	}
	
	~FPreviewMaterial()
	{
		BeginReleaseResource(this);
		FlushRenderingCommands();
	}

	/**
	 * Should the shader for this material with the given platform, shader type and vertex 
	 * factory type combination be compiled
	 *
	 * @param Platform		The platform currently being compiled for
	 * @param ShaderType	Which shader is being compiled
	 * @param VertexFactory	Which vertex factory is being compiled (can be NULL)
	 *
	 * @return true if the shader should be compiled
	 */
	virtual bool ShouldCache(EShaderPlatform Platform, const FShaderType* ShaderType, const FVertexFactoryType* VertexFactoryType) const
	{
		// only generate the needed shaders (which should be very restrictive for fast recompiling during editing)
		// @todo: Add a FindShaderType by fname or something

		if( Material->IsUIMaterial() )
		{
			if (FCString::Stristr(ShaderType->GetName(), TEXT("TSlateMaterialShaderPS")) ||
				FCString::Stristr(ShaderType->GetName(), TEXT("TSlateMaterialShaderVS")))
			{
				return true;
			}
	
		}

		if (Material->IsPostProcessMaterial())
		{
			if (FCString::Stristr(ShaderType->GetName(), TEXT("PostProcess")))
			{
				return true;
			}
		}

		{
			bool bEditorStatsMaterial = Material->bIsMaterialEditorStatsMaterial;

			// Always allow HitProxy shaders.
			if (FCString::Stristr(ShaderType->GetName(), TEXT("HitProxy")))
			{
				return true;
			}

			// we only need local vertex factory for the preview static mesh
			if (VertexFactoryType != FindVertexFactoryType(FName(TEXT("FLocalVertexFactory"), FNAME_Find)))
			{
				//cache for gpu skinned vertex factory if the material allows it
				//this way we can have a preview skeletal mesh
				if (bEditorStatsMaterial ||
					!IsUsedWithSkeletalMesh())
				{
					return false;
				}

				extern ENGINE_API bool IsGPUSkinCacheAvailable();
				bool bSkinCache = IsGPUSkinCacheAvailable() && (VertexFactoryType == FindVertexFactoryType(FName(TEXT("FGPUSkinPassthroughVertexFactory"), FNAME_Find)));
					
				if (
					VertexFactoryType != FindVertexFactoryType(FName(TEXT("TGPUSkinVertexFactoryfalse"), FNAME_Find)) &&
					VertexFactoryType != FindVertexFactoryType(FName(TEXT("TGPUSkinVertexFactorytrue"), FNAME_Find)) &&
					!bSkinCache
					)
				{
					return false;
				}
			}

			if (bEditorStatsMaterial)
			{
				TMap<FName, FString> ShaderTypeNamesAndDescriptions;
				GetRepresentativeShaderTypesAndDescriptions(ShaderTypeNamesAndDescriptions);

				//Only allow shaders that are used in the stats.
				return ShaderTypeNamesAndDescriptions.Contains(ShaderType->GetFName());
			}

			// look for any of the needed type
			bool bShaderTypeMatches = false;

			// For FMaterialResource::GetRepresentativeInstructionCounts
			if (FCString::Stristr(ShaderType->GetName(), TEXT("BasePassPSTDistanceFieldShadowsAndLightMapPolicyHQ")))
			{
				bShaderTypeMatches = true;
			}
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("Simple")))
			{
				bShaderTypeMatches = true;
			}
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("BasePassPSFNoLightMapPolicy")))
			{
				bShaderTypeMatches = true;
			}
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("CachedPointIndirectLightingPolicy")))
			{
				bShaderTypeMatches = true;
			}
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("PrecomputedVolumetricLightmapLightingPolicy")))
			{
				bShaderTypeMatches = true;
			}
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("BasePassPSFSelfShadowedTranslucencyPolicy")))
			{
				bShaderTypeMatches = true;
			}
			// Pick tessellation shader based on material settings
			else if(FCString::Stristr(ShaderType->GetName(), TEXT("BasePassVSFNoLightMapPolicy")) ||
				FCString::Stristr(ShaderType->GetName(), TEXT("BasePassHSFNoLightMapPolicy")) ||
				FCString::Stristr(ShaderType->GetName(), TEXT("BasePassDSFNoLightMapPolicy")))
			{
				bShaderTypeMatches = true;
			}
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("DepthOnly")))
			{
				bShaderTypeMatches = true;
			}
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("ShadowDepth")))
			{
				bShaderTypeMatches = true;
			}
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("TDistortion")))
			{
				bShaderTypeMatches = true;
			}
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("MeshDecal")))
			{
				bShaderTypeMatches = true;
			}
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("TBasePassForForwardShading")))
			{
				bShaderTypeMatches = true;
			}
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("FDebugViewModeVS")))
			{
				bShaderTypeMatches = true;
			}

			return bShaderTypeMatches;
		}
	
	}

	/**
	 * Should shaders compiled for this material be saved to disk?
	 */
	virtual bool IsPersistent() const { return false; }

	// FMaterialRenderProxy interface
	virtual const FMaterial* GetMaterial(ERHIFeatureLevel::Type FeatureLevel) const
	{
		if(GetRenderingThreadShaderMap())
		{
			return this;
		}
		else
		{
			return UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy(false)->GetMaterial(FeatureLevel);
		}
	}

	virtual bool GetVectorValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const
	{
		return Material->GetRenderProxy(0)->GetVectorValue(ParameterInfo, OutValue, Context);
	}

	virtual bool GetScalarValue(const FMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const
	{
		return Material->GetRenderProxy(0)->GetScalarValue(ParameterInfo, OutValue, Context);
	}

	virtual bool GetTextureValue(const FMaterialParameterInfo& ParameterInfo, const UTexture** OutValue, const FMaterialRenderContext& Context) const
	{
		return Material->GetRenderProxy(0)->GetTextureValue(ParameterInfo,OutValue,Context);
	}
};

/** Implementation of Preview Material functions*/
UPreviewMaterial::UPreviewMaterial(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FMaterialResource* UPreviewMaterial::AllocateResource()
{
	return new FPreviewMaterial();
}

FName UMaterialEditorInstanceConstant::GlobalGroupPrefix = FName("Global ");

UMaterialEditorInstanceConstant::UMaterialEditorInstanceConstant(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsFunctionPreviewMaterial = false;
}

void UMaterialEditorInstanceConstant::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (SourceInstance)
	{
		UProperty* PropertyThatChanged = PropertyChangedEvent.Property;
		bool bLayersParameterChanged = false;

		FNavigationLockContext NavUpdateLock(ENavigationLockReason::MaterialUpdate);

		if(PropertyThatChanged && PropertyThatChanged->GetName()==TEXT("Parent") )
		{
			FMaterialUpdateContext Context;

			UpdateSourceInstanceParent();

			Context.AddMaterialInstance(SourceInstance);

			// Fully update static parameters before recreating render state for all components
			SetSourceInstance(SourceInstance);
		}
		else if (!bIsFunctionPreviewMaterial)
		{
			// If a material layers parameter changed we need to update it on the source instance
			// immediately so parameters contained within the new functions can be collected
			for (FEditorParameterGroup& Group : ParameterGroups)
			{
				for (UDEditorParameterValue* Parameter : Group.Parameters)
				{
					if (UDEditorMaterialLayersParameterValue* LayersParam = Cast<UDEditorMaterialLayersParameterValue>(Parameter))
					{
						if (SourceInstance->UpdateMaterialLayersParameterValue(LayersParam->ParameterInfo, LayersParam->ParameterValue, LayersParam->bOverride, LayersParam->ExpressionId))
						{
							bLayersParameterChanged = true;
						}	
					}
				}
			}

			if (bLayersParameterChanged)
			{
				RegenerateArrays();
			}
		}

		CopyToSourceInstance(bLayersParameterChanged);

		// Tell our source instance to update itself so the preview updates.
		SourceInstance->PostEditChangeProperty(PropertyChangedEvent);

		// Invalidate the streaming data so that it gets rebuilt.
		SourceInstance->TextureStreamingData.Empty();
	}
}

void  UMaterialEditorInstanceConstant::AssignParameterToGroup(UMaterial* ParentMaterial, UDEditorParameterValue* ParameterValue)
{
	check(ParentMaterial);
	check(ParameterValue);

	FName ParameterGroupName;
	SourceInstance->GetGroupName(ParameterValue->ParameterInfo, ParameterGroupName);

	if (ParameterGroupName == TEXT("") || ParameterGroupName == TEXT("None"))
	{
		if (bUseOldStyleMICEditorGroups == true)
		{
			if (Cast<UDEditorVectorParameterValue>(ParameterValue))
			{
				ParameterGroupName = TEXT("Vector Parameter Values");
			}
			else if (Cast<UDEditorTextureParameterValue>(ParameterValue))
			{
				ParameterGroupName = TEXT("Texture Parameter Values");
			}
			else if (Cast<UDEditorScalarParameterValue>(ParameterValue))
			{
				ParameterGroupName = TEXT("Scalar Parameter Values");
			}
			else if (Cast<UDEditorStaticSwitchParameterValue>(ParameterValue))
			{
				ParameterGroupName = TEXT("Static Switch Parameter Values");
			}
			else if (Cast<UDEditorStaticComponentMaskParameterValue>(ParameterValue))
			{
				ParameterGroupName = TEXT("Static Component Mask Parameter Values");
			}
			else if (Cast<UDEditorFontParameterValue>(ParameterValue))
			{
				ParameterGroupName = TEXT("Font Parameter Values");
			}
			else if (Cast<UDEditorMaterialLayersParameterValue>(ParameterValue))
			{
				ParameterGroupName = TEXT("Material Layers Parameter Values");
			}
			else
			{
				ParameterGroupName = TEXT("None");
			}
		}
		else
		{
			ParameterGroupName = TEXT("None");
		}

		IMaterialEditorModule* MaterialEditorModule = &FModuleManager::LoadModuleChecked<IMaterialEditorModule>("MaterialEditor");
		if (MaterialEditorModule->MaterialLayersEnabled())
		{
			if (ParameterValue->ParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter)
			{
				FString AppendedGroupName = GlobalGroupPrefix.ToString();
				if (ParameterGroupName != TEXT("None"))
				{
					ParameterGroupName.AppendString(AppendedGroupName);
					ParameterGroupName = FName(*AppendedGroupName);
				}
				else
				{
					ParameterGroupName = TEXT("Global");
				}
			}
		}
	}

	FEditorParameterGroup& CurrentGroup = GetParameterGroup(ParameterGroupName);
	CurrentGroup.GroupAssociation = ParameterValue->ParameterInfo.Association;
	ParameterValue->SetFlags(RF_Transactional);
	CurrentGroup.Parameters.Add(ParameterValue);
}

FEditorParameterGroup&  UMaterialEditorInstanceConstant::GetParameterGroup(FName& ParameterGroup)
{	
	if (ParameterGroup == TEXT(""))
	{
		ParameterGroup = TEXT("None");
	}
	for (int32 i = 0; i < ParameterGroups.Num(); i ++)
	{
		FEditorParameterGroup& Group= ParameterGroups[i];
		if (Group.GroupName == ParameterGroup)
		{
			return Group;
		}
	}
	int32 ind = ParameterGroups.AddZeroed(1);
	FEditorParameterGroup& Group= ParameterGroups[ind];
	Group.GroupName = ParameterGroup;
	UMaterial* ParentMaterial = Parent->GetMaterial();
	int32 NewSortPriority;
	if (ParentMaterial->GetGroupSortPriority(ParameterGroup.ToString(), NewSortPriority))
	{
		Group.GroupSortPriority = NewSortPriority;
	}
	else
	{
		Group.GroupSortPriority = 0;
	}
	Group.GroupAssociation = EMaterialParameterAssociation::GlobalParameter;

	return Group;
}

void UMaterialEditorInstanceConstant::RegenerateArrays()
{
	VisibleExpressions.Empty();
	ParameterGroups.Empty();

	if (Parent)
	{	
		// Only operate on base materials
		UMaterial* ParentMaterial = Parent->GetMaterial();
		SourceInstance->UpdateParameterNames();	// Update any parameter names that may have changed.

		// Get all static parameters from the source instance.  This will handle inheriting parent values.	
		FStaticParameterSet SourceStaticParameters;
 		SourceInstance->GetStaticParameterValues(SourceStaticParameters);

		// Loop through all types of parameters for this material and add them to the parameter arrays.
		TArray<FMaterialParameterInfo> OutParameterInfo;
		TArray<FGuid> Guids;

		// Scalar Parameters.
		SourceInstance->GetAllScalarParameterInfo(OutParameterInfo, Guids);
		for (int32 ParameterIdx=0; ParameterIdx<OutParameterInfo.Num(); ParameterIdx++)
		{			
			UDEditorScalarParameterValue& ParameterValue = *(NewObject<UDEditorScalarParameterValue>());
			const FMaterialParameterInfo& ParameterInfo = OutParameterInfo[ParameterIdx];

			ParameterValue.bOverride = false;
			ParameterValue.ParameterInfo = ParameterInfo;
			ParameterValue.ExpressionId = Guids[ParameterIdx];

			if (SourceInstance->GetScalarParameterValue(ParameterInfo, ParameterValue.ParameterValue))
			{
				SourceInstance->GetScalarParameterSliderMinMax(ParameterInfo, ParameterValue.SliderMin, ParameterValue.SliderMax);		
			}

			// @todo: This is kind of slow, maybe store these in a map for lookup?
			// See if this keyname exists in the source instance.
			for(int32 ScalarParameterIdx=0; ScalarParameterIdx<SourceInstance->ScalarParameterValues.Num(); ScalarParameterIdx++)
			{
				FScalarParameterValue& SourceParam = SourceInstance->ScalarParameterValues[ScalarParameterIdx];
				if(ParameterInfo == SourceParam.ParameterInfo)
				{
					ParameterValue.bOverride = true;
					ParameterValue.ParameterValue = SourceParam.ParameterValue;
				}
			}
			
			ParameterValue.SortPriority = 0;
			SourceInstance->GetParameterSortPriority(ParameterInfo, ParameterValue.SortPriority);
			
			AssignParameterToGroup(ParentMaterial, &ParameterValue);
		}

		// Vector Parameters.
		SourceInstance->GetAllVectorParameterInfo(OutParameterInfo, Guids);
		for(int32 ParameterIdx=0; ParameterIdx<OutParameterInfo.Num(); ParameterIdx++)
		{
			UDEditorVectorParameterValue& ParameterValue = *(NewObject<UDEditorVectorParameterValue>());
			const FMaterialParameterInfo& ParameterInfo = OutParameterInfo[ParameterIdx];
			
			ParameterValue.bOverride = false;
			ParameterValue.ParameterInfo = ParameterInfo;
			ParameterValue.ExpressionId = Guids[ParameterIdx];

			SourceInstance->GetVectorParameterValue(ParameterInfo, ParameterValue.ParameterValue);

			// @todo: This is kind of slow, maybe store these in a map for lookup?
			// See if this keyname exists in the source instance.
			for(int32 VectorParameterIdx=0; VectorParameterIdx<SourceInstance->VectorParameterValues.Num(); VectorParameterIdx++)
			{
				FVectorParameterValue& SourceParam = SourceInstance->VectorParameterValues[VectorParameterIdx];
				if(ParameterInfo == SourceParam.ParameterInfo)
				{
					ParameterValue.bOverride = true;
					ParameterValue.ParameterValue = SourceParam.ParameterValue;
				}
			}
			
			ParameterValue.SortPriority = 0;
			SourceInstance->GetParameterSortPriority(ParameterInfo, ParameterValue.SortPriority);

			AssignParameterToGroup(ParentMaterial, &ParameterValue);
		}

		// Texture Parameters.
		SourceInstance->GetAllTextureParameterInfo(OutParameterInfo, Guids);
		for(int32 ParameterIdx=0; ParameterIdx<OutParameterInfo.Num(); ParameterIdx++)
		{			
			UDEditorTextureParameterValue& ParameterValue = *(NewObject<UDEditorTextureParameterValue>());
			const FMaterialParameterInfo& ParameterInfo = OutParameterInfo[ParameterIdx];

			ParameterValue.bOverride = false;
			ParameterValue.ParameterInfo = ParameterInfo;
			ParameterValue.ExpressionId = Guids[ParameterIdx];

			ParameterValue.ParameterValue = nullptr;
			SourceInstance->GetTextureParameterValue(ParameterInfo, ParameterValue.ParameterValue);

			// @todo: This is kind of slow, maybe store these in a map for lookup?
			// See if this keyname exists in the source instance.
			for(int32 TextureParameterIdx=0; TextureParameterIdx<SourceInstance->TextureParameterValues.Num(); TextureParameterIdx++)
			{
				FTextureParameterValue& SourceParam = SourceInstance->TextureParameterValues[TextureParameterIdx];
				if(ParameterInfo == SourceParam.ParameterInfo)
				{
					ParameterValue.bOverride = true;
					ParameterValue.ParameterValue = SourceParam.ParameterValue;
				}
				if(ParameterInfo.Name.IsEqual(SourceParam.ParameterInfo.Name) && ParameterInfo.Association == SourceParam.ParameterInfo.Association && ParameterInfo.Index == SourceParam.ParameterInfo.Index)
				{
					ParameterValue.bOverride = true;
					ParameterValue.ParameterValue = SourceParam.ParameterValue;
				}
			}
			
			ParameterValue.SortPriority = 0;
			SourceInstance->GetParameterSortPriority(ParameterInfo, ParameterValue.SortPriority);

			AssignParameterToGroup(ParentMaterial, &ParameterValue);
		}

		// Font Parameters.
		SourceInstance->GetAllFontParameterInfo(OutParameterInfo, Guids);
		for(int32 ParameterIdx=0; ParameterIdx<OutParameterInfo.Num(); ParameterIdx++)
		{
			UDEditorFontParameterValue& ParameterValue = *(NewObject<UDEditorFontParameterValue>());
			const FMaterialParameterInfo& ParameterInfo = OutParameterInfo[ParameterIdx];

			ParameterValue.bOverride = false;
			ParameterValue.ParameterInfo = ParameterInfo;
			ParameterValue.ExpressionId = Guids[ParameterIdx];

			ParameterValue.ParameterValue.FontValue = nullptr;
			ParameterValue.ParameterValue.FontPage = 0;
			SourceInstance->GetFontParameterValue(ParameterInfo, ParameterValue.ParameterValue.FontValue, ParameterValue.ParameterValue.FontPage);

			// @todo: This is kind of slow, maybe store these in a map for lookup?
			// See if this keyname exists in the source instance.
			for(int32 FontParameterIdx=0; FontParameterIdx<SourceInstance->FontParameterValues.Num(); FontParameterIdx++)
			{
				FFontParameterValue& SourceParam = SourceInstance->FontParameterValues[FontParameterIdx];
				if(ParameterInfo == SourceParam.ParameterInfo)
				{
					ParameterValue.bOverride = true;
					ParameterValue.ParameterValue.FontValue = SourceParam.FontValue;
					ParameterValue.ParameterValue.FontPage = SourceParam.FontPage;
				}
			}
			
			ParameterValue.SortPriority = 0;
			SourceInstance->GetParameterSortPriority(ParameterInfo, ParameterValue.SortPriority);
			
			AssignParameterToGroup(ParentMaterial, Cast<UDEditorParameterValue>(&ParameterValue));
		}

		// Copy Static Material Layers Parameters
		for(int32 ParameterIdx=0; ParameterIdx<SourceStaticParameters.MaterialLayersParameters.Num(); ParameterIdx++)
		{
			FStaticMaterialLayersParameter MaterialLayersParameterParameterValue = FStaticMaterialLayersParameter(SourceStaticParameters.MaterialLayersParameters[ParameterIdx]);
			UDEditorMaterialLayersParameterValue& ParameterValue = *(NewObject<UDEditorMaterialLayersParameterValue>());

			ParameterValue.ParameterValue = MaterialLayersParameterParameterValue.Value;
			ParameterValue.bOverride = MaterialLayersParameterParameterValue.bOverride;
			ParameterValue.ParameterInfo = MaterialLayersParameterParameterValue.ParameterInfo;
			ParameterValue.ExpressionId = MaterialLayersParameterParameterValue.ExpressionGUID;

			ParameterValue.SortPriority = 0; // Has custom interface so not a supported feature

			AssignParameterToGroup(ParentMaterial, &ParameterValue);
		}

		// Copy Static Switch Parameters
		SourceInstance->GetAllStaticSwitchParameterInfo(OutParameterInfo, Guids);
		for(int32 ParameterIdx=0; ParameterIdx<SourceStaticParameters.StaticSwitchParameters.Num(); ParameterIdx++)
		{			
			FStaticSwitchParameter StaticSwitchParameterValue = FStaticSwitchParameter(SourceStaticParameters.StaticSwitchParameters[ParameterIdx]);
			UDEditorStaticSwitchParameterValue& ParameterValue = *(NewObject<UDEditorStaticSwitchParameterValue>());

			ParameterValue.ParameterValue = StaticSwitchParameterValue.Value;
			ParameterValue.bOverride = StaticSwitchParameterValue.bOverride;
			ParameterValue.ParameterInfo = StaticSwitchParameterValue.ParameterInfo;
			ParameterValue.ExpressionId = StaticSwitchParameterValue.ExpressionGUID;

			ParameterValue.SortPriority = 0;
			SourceInstance->GetParameterSortPriority(StaticSwitchParameterValue.ParameterInfo, ParameterValue.SortPriority);

			AssignParameterToGroup(ParentMaterial, &ParameterValue);
		}

		// Copy Static Component Mask Parameters
		SourceInstance->GetAllStaticComponentMaskParameterInfo(OutParameterInfo, Guids);
		for(int32 ParameterIdx=0; ParameterIdx<SourceStaticParameters.StaticComponentMaskParameters.Num(); ParameterIdx++)
		{
			FStaticComponentMaskParameter StaticComponentMaskParameterValue = FStaticComponentMaskParameter(SourceStaticParameters.StaticComponentMaskParameters[ParameterIdx]);
			UDEditorStaticComponentMaskParameterValue& ParameterValue = *(NewObject<UDEditorStaticComponentMaskParameterValue>());

			ParameterValue.ParameterValue.R = StaticComponentMaskParameterValue.R;
			ParameterValue.ParameterValue.G = StaticComponentMaskParameterValue.G;
			ParameterValue.ParameterValue.B = StaticComponentMaskParameterValue.B;
			ParameterValue.ParameterValue.A = StaticComponentMaskParameterValue.A;
			ParameterValue.bOverride = StaticComponentMaskParameterValue.bOverride;
			ParameterValue.ParameterInfo = StaticComponentMaskParameterValue.ParameterInfo;
			ParameterValue.ExpressionId = StaticComponentMaskParameterValue.ExpressionGUID;

			ParameterValue.SortPriority = 0;
			SourceInstance->GetParameterSortPriority(StaticComponentMaskParameterValue.ParameterInfo, ParameterValue.SortPriority);

			AssignParameterToGroup(ParentMaterial, &ParameterValue);
		}

		IMaterialEditorModule* MaterialEditorModule = &FModuleManager::LoadModuleChecked<IMaterialEditorModule>( "MaterialEditor" );
		MaterialEditorModule->GetVisibleMaterialParameters(ParentMaterial, SourceInstance, VisibleExpressions);
	}

	// sort contents of groups
	for(int32 ParameterIdx=0; ParameterIdx<ParameterGroups.Num(); ParameterIdx++)
	{
		FEditorParameterGroup & ParamGroup = ParameterGroups[ParameterIdx];
		struct FCompareUDEditorParameterValueByParameterName
		{
			FORCEINLINE bool operator()(const UDEditorParameterValue& A, const UDEditorParameterValue& B) const
			{
				FString AName = A.ParameterInfo.Name.ToString();
				FString BName = B.ParameterInfo.Name.ToString();
				return A.SortPriority != B.SortPriority ? A.SortPriority < B.SortPriority : AName < BName;
			}
		};
		ParamGroup.Parameters.Sort( FCompareUDEditorParameterValueByParameterName() );
	}	
	
	// sort groups itself pushing defaults to end
	struct FCompareFEditorParameterGroupByName
	{
		FORCEINLINE bool operator()(const FEditorParameterGroup& A, const FEditorParameterGroup& B) const
		{
			FString AName = A.GroupName.ToString();
			FString BName = B.GroupName.ToString();
			if (AName == TEXT("none"))
			{
				return false;
			}
			if (BName == TEXT("none"))
			{
				return false;
			}
			return A.GroupSortPriority != B.GroupSortPriority ? A.GroupSortPriority < B.GroupSortPriority : AName < BName;
		}
	};
	ParameterGroups.Sort( FCompareFEditorParameterGroupByName() );

	TArray<struct FEditorParameterGroup> ParameterDefaultGroups;
	for(int32 ParameterIdx=0; ParameterIdx<ParameterGroups.Num(); ParameterIdx++)
	{
		FEditorParameterGroup & ParamGroup = ParameterGroups[ParameterIdx];
		if (bUseOldStyleMICEditorGroups == false)
		{			
			if (ParamGroup.GroupName == TEXT("None"))
			{
				ParameterDefaultGroups.Add(ParamGroup);
				ParameterGroups.RemoveAt(ParameterIdx);
				break;
			}
		}
		else
		{
			if (ParamGroup.GroupName == TEXT("Vector Parameter Values") || 
				ParamGroup.GroupName == TEXT("Scalar Parameter Values") ||
				ParamGroup.GroupName == TEXT("Texture Parameter Values") ||
				ParamGroup.GroupName == TEXT("Static Switch Parameter Values") ||
				ParamGroup.GroupName == TEXT("Static Component Mask Parameter Values") ||
				ParamGroup.GroupName == TEXT("Font Parameter Values") ||
				ParamGroup.GroupName == TEXT("Material Layers Parameter Values"))
			{
				ParameterDefaultGroups.Add(ParamGroup);
				ParameterGroups.RemoveAt(ParameterIdx);
			}
		}
	}

	if (ParameterDefaultGroups.Num() >0)
	{
		ParameterGroups.Append(ParameterDefaultGroups);
	}
}

void UMaterialEditorInstanceConstant::CopyToSourceInstance(const bool bForceStaticPermutationUpdate)
{
	if (!SourceInstance->IsTemplate(RF_ClassDefaultObject))
	{
		if (bIsFunctionPreviewMaterial)
		{
			bIsFunctionInstanceDirty = true;
		}
		else
		{
			SourceInstance->MarkPackageDirty();
		}

		SourceInstance->ClearParameterValuesEditorOnly();

		for (int32 GroupIdx=0; GroupIdx<ParameterGroups.Num(); GroupIdx++)
		{
			FEditorParameterGroup & Group = ParameterGroups[GroupIdx];
			for (int32 ParameterIdx=0; ParameterIdx<Group.Parameters.Num(); ParameterIdx++)
			{
				if (Group.Parameters[ParameterIdx] == NULL)
				{
					continue;
				}

				UDEditorScalarParameterValue* ScalarParameterValue = Cast<UDEditorScalarParameterValue>(Group.Parameters[ParameterIdx]);
				UDEditorVectorParameterValue* VectorParameterValue = Cast<UDEditorVectorParameterValue>(Group.Parameters[ParameterIdx]);
				UDEditorTextureParameterValue* TextureParameterValue = Cast<UDEditorTextureParameterValue>(Group.Parameters[ParameterIdx]);
				UDEditorFontParameterValue* FontParameterValue = Cast<UDEditorFontParameterValue>(Group.Parameters[ParameterIdx]);

				if (ScalarParameterValue && ScalarParameterValue->bOverride)
				{
					SourceInstance->SetScalarParameterValueEditorOnly(ScalarParameterValue->ParameterInfo, ScalarParameterValue->ParameterValue);
				}
				else if (VectorParameterValue && VectorParameterValue->bOverride)
				{
					SourceInstance->SetVectorParameterValueEditorOnly(VectorParameterValue->ParameterInfo, VectorParameterValue->ParameterValue);
				}
				else if (TextureParameterValue && TextureParameterValue->bOverride)
				{
					SourceInstance->SetTextureParameterValueEditorOnly(TextureParameterValue->ParameterInfo, TextureParameterValue->ParameterValue);
				}
				else if (FontParameterValue && FontParameterValue->bOverride)
				{
					SourceInstance->SetFontParameterValueEditorOnly(FontParameterValue->ParameterInfo, FontParameterValue->ParameterValue.FontValue, FontParameterValue->ParameterValue.FontPage);
				}
			}
		}

		FStaticParameterSet NewStaticParameters;
		BuildStaticParametersForSourceInstance(NewStaticParameters);
		SourceInstance->UpdateStaticPermutation(NewStaticParameters, BasePropertyOverrides, bForceStaticPermutationUpdate);

		// Copy phys material back to source instance
		SourceInstance->PhysMaterial = PhysMaterial;

		// Copy the Lightmass settings...
		SourceInstance->SetOverrideCastShadowAsMasked(LightmassSettings.CastShadowAsMasked.bOverride);
		SourceInstance->SetCastShadowAsMasked(LightmassSettings.CastShadowAsMasked.ParameterValue);
		SourceInstance->SetOverrideEmissiveBoost(LightmassSettings.EmissiveBoost.bOverride);
		SourceInstance->SetEmissiveBoost(LightmassSettings.EmissiveBoost.ParameterValue);
		SourceInstance->SetOverrideDiffuseBoost(LightmassSettings.DiffuseBoost.bOverride);
		SourceInstance->SetDiffuseBoost(LightmassSettings.DiffuseBoost.ParameterValue);
		SourceInstance->SetOverrideExportResolutionScale(LightmassSettings.ExportResolutionScale.bOverride);
		SourceInstance->SetExportResolutionScale(LightmassSettings.ExportResolutionScale.ParameterValue);

		// Copy Refraction bias setting
		FMaterialParameterInfo RefractionInfo(TEXT("RefractionDepthBias"));
		SourceInstance->SetScalarParameterValueEditorOnly(RefractionInfo, RefractionDepthBias);

		SourceInstance->bOverrideSubsurfaceProfile = bOverrideSubsurfaceProfile;
		SourceInstance->SubsurfaceProfile = SubsurfaceProfile;

		// Update object references and parameter names.
		SourceInstance->UpdateParameterNames();
		VisibleExpressions.Empty();
		
		// force refresh of visibility of properties
		if (Parent)
		{
			UMaterial* ParentMaterial = Parent->GetMaterial();
			IMaterialEditorModule* MaterialEditorModule = &FModuleManager::LoadModuleChecked<IMaterialEditorModule>("MaterialEditor");
			MaterialEditorModule->GetVisibleMaterialParameters(ParentMaterial, SourceInstance, VisibleExpressions);
		}
	}
}

void UMaterialEditorInstanceConstant::ApplySourceFunctionChanges()
{
	if (bIsFunctionPreviewMaterial && bIsFunctionInstanceDirty)
	{
		CopyToSourceInstance();

		// Copy updated function parameter values	
		SourceFunction->ScalarParameterValues = SourceInstance->ScalarParameterValues;
		SourceFunction->VectorParameterValues = SourceInstance->VectorParameterValues;
		SourceFunction->TextureParameterValues = SourceInstance->TextureParameterValues;
		SourceFunction->FontParameterValues = SourceInstance->FontParameterValues;

		const FStaticParameterSet& StaticParameters = SourceInstance->GetStaticParameters();
		SourceFunction->StaticSwitchParameterValues = StaticParameters.StaticSwitchParameters;
		SourceFunction->StaticComponentMaskParameterValues = StaticParameters.StaticComponentMaskParameters;

		SourceFunction->MarkPackageDirty();
		bIsFunctionInstanceDirty = false;

		// Create a material update context so we can safely update materials using this function.
		FMaterialUpdateContext UpdateContext;

		for (TObjectIterator<UMaterial> It; It; ++It)
		{
			UMaterial* CurrentMaterial = *It;
			if (CurrentMaterial != SourceInstance->Parent)
			{
				bool bRecompile = false;
				if (CurrentMaterial->bIsPreviewMaterial)
				{
					bRecompile = true;
				}
				else
				{
					for (const FMaterialFunctionInfo& FunctionInfo : CurrentMaterial->MaterialFunctionInfos)
					{
						if (FunctionInfo.Function == SourceFunction)
						{
							bRecompile = true;
							break;
						}
					}
				}

				if (bRecompile)
				{
					UpdateContext.AddMaterial(CurrentMaterial);
					CurrentMaterial->PreEditChange(nullptr);
					CurrentMaterial->PostEditChange();
				}
			}
		}

		for (TObjectIterator<UMaterialInstance> It; It; ++It)
		{
			UMaterialInstance* CurrentInstance = *It;
			if (CurrentInstance->GetBaseMaterial() && CurrentInstance != SourceInstance)
			{
				// TODO: This likely forces updates even if a base param has been overridden which
				// should be ignored. Only needs to check the top-level override of each parameter
				TArray<UMaterialFunctionInterface*> Functions;
				CurrentInstance->GetDependentFunctions(Functions);
				if (Functions.Contains(SourceFunction))
				{
					UpdateContext.AddMaterialInstance(CurrentInstance);
					CurrentInstance->PreEditChange(nullptr);
					CurrentInstance->PostEditChange();
					break;
				}
			}
		}
	}
}

void UMaterialEditorInstanceConstant::BuildStaticParametersForSourceInstance(FStaticParameterSet& OutStaticParameters)
{
	for(int32 GroupIdx=0; GroupIdx<ParameterGroups.Num(); GroupIdx++)
	{
		FEditorParameterGroup& Group = ParameterGroups[GroupIdx];

		for(int32 ParameterIdx=0; ParameterIdx<Group.Parameters.Num(); ParameterIdx++)
		{
			if (Group.Parameters[ParameterIdx] == NULL)
			{
				continue;
			}

			// Static switch
			UDEditorStaticSwitchParameterValue* StaticSwitchParameterValue = Cast<UDEditorStaticSwitchParameterValue>(Group.Parameters[ParameterIdx]);
			if (StaticSwitchParameterValue && StaticSwitchParameterValue->bOverride)
			{
				bool SwitchValue = StaticSwitchParameterValue->ParameterValue;
				FGuid ExpressionIdValue = StaticSwitchParameterValue->ExpressionId;

				FStaticSwitchParameter* NewParameter = new(OutStaticParameters.StaticSwitchParameters)
					FStaticSwitchParameter(StaticSwitchParameterValue->ParameterInfo, SwitchValue, StaticSwitchParameterValue->bOverride, ExpressionIdValue);
			}

			// Static component mask
			UDEditorStaticComponentMaskParameterValue* StaticComponentMaskParameterValue = Cast<UDEditorStaticComponentMaskParameterValue>(Group.Parameters[ParameterIdx]);
			if (StaticComponentMaskParameterValue && StaticComponentMaskParameterValue->bOverride)
			{
				bool MaskR = StaticComponentMaskParameterValue->ParameterValue.R;
				bool MaskG = StaticComponentMaskParameterValue->ParameterValue.G;
				bool MaskB = StaticComponentMaskParameterValue->ParameterValue.B;
				bool MaskA = StaticComponentMaskParameterValue->ParameterValue.A;
				FGuid ExpressionIdValue = StaticComponentMaskParameterValue->ExpressionId;

				FStaticComponentMaskParameter* NewParameter = new(OutStaticParameters.StaticComponentMaskParameters) 
					FStaticComponentMaskParameter(StaticComponentMaskParameterValue->ParameterInfo, MaskR, MaskG, MaskB, MaskA, StaticComponentMaskParameterValue->bOverride, ExpressionIdValue);
			}

			// Material layers param
			UDEditorMaterialLayersParameterValue* MaterialLayersParameterValue = Cast<UDEditorMaterialLayersParameterValue>(Group.Parameters[ParameterIdx]);
			if (MaterialLayersParameterValue && MaterialLayersParameterValue->bOverride)
			{
				const FMaterialLayersFunctions& MaterialLayers = MaterialLayersParameterValue->ParameterValue;
				FGuid ExpressionIdValue = MaterialLayersParameterValue->ExpressionId;

				FStaticMaterialLayersParameter* NewParameter = new(OutStaticParameters.MaterialLayersParameters)
					FStaticMaterialLayersParameter(MaterialLayersParameterValue->ParameterInfo, MaterialLayers, MaterialLayersParameterValue->bOverride, ExpressionIdValue);
			}
		}
	}
}


void UMaterialEditorInstanceConstant::SetSourceInstance(UMaterialInstanceConstant* MaterialInterface)
{
	check(MaterialInterface);
	SourceInstance = MaterialInterface;
	Parent = SourceInstance->Parent;
	PhysMaterial = SourceInstance->PhysMaterial;

	BasePropertyOverrides = SourceInstance->BasePropertyOverrides;

	// Copy the Lightmass settings...
	LightmassSettings.CastShadowAsMasked.bOverride = SourceInstance->GetOverrideCastShadowAsMasked();
	LightmassSettings.CastShadowAsMasked.ParameterValue = SourceInstance->GetCastShadowAsMasked();
	LightmassSettings.EmissiveBoost.bOverride = SourceInstance->GetOverrideEmissiveBoost();
	LightmassSettings.EmissiveBoost.ParameterValue = SourceInstance->GetEmissiveBoost();
	LightmassSettings.DiffuseBoost.bOverride = SourceInstance->GetOverrideDiffuseBoost();
	LightmassSettings.DiffuseBoost.ParameterValue = SourceInstance->GetDiffuseBoost();
	LightmassSettings.ExportResolutionScale.bOverride = SourceInstance->GetOverrideExportResolutionScale();
	LightmassSettings.ExportResolutionScale.ParameterValue = SourceInstance->GetExportResolutionScale();

	//Copy refraction settings
	SourceInstance->GetRefractionSettings(RefractionDepthBias);

	bOverrideSubsurfaceProfile = SourceInstance->bOverrideSubsurfaceProfile;
	SubsurfaceProfile = SourceInstance->SubsurfaceProfile;

	RegenerateArrays();

	//propagate changes to the base material so the instance will be updated if it has a static permutation resource
	FStaticParameterSet NewStaticParameters;
	BuildStaticParametersForSourceInstance(NewStaticParameters);
	SourceInstance->UpdateStaticPermutation(NewStaticParameters);
}

void UMaterialEditorInstanceConstant::SetSourceFunction(UMaterialFunctionInstance* MaterialFunction)
{
	SourceFunction = MaterialFunction;
	bIsFunctionPreviewMaterial = !!(SourceFunction);
}

void UMaterialEditorInstanceConstant::UpdateSourceInstanceParent()
{
	// If the parent was changed to the source instance, set it to NULL
	if( Parent == SourceInstance )
	{
		Parent = NULL;
	}

	SourceInstance->SetParentEditorOnly( Parent );
}

#if WITH_EDITOR
void UMaterialEditorInstanceConstant::PostEditUndo()
{
	FMaterialUpdateContext UpdateContext;

	UpdateSourceInstanceParent();

	UpdateContext.AddMaterialInstance(SourceInstance);

	Super::PostEditUndo();
}
#endif

UMaterialEditorMeshComponent::UMaterialEditorMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}
