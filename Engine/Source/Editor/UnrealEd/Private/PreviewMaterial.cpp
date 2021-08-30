// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialEditor/PreviewMaterial.h"
#include "Modules/ModuleManager.h"
#include "MaterialEditor/DEditorParameterValue.h"
#include "MaterialEditor/DEditorFontParameterValue.h"
#include "MaterialEditor/DEditorMaterialLayersParameterValue.h"
#include "MaterialEditor/DEditorRuntimeVirtualTextureParameterValue.h"
#include "MaterialEditor/DEditorScalarParameterValue.h"
#include "MaterialEditor/DEditorStaticComponentMaskParameterValue.h"
#include "MaterialEditor/DEditorStaticSwitchParameterValue.h"
#include "MaterialEditor/DEditorTextureParameterValue.h"
#include "MaterialEditor/DEditorVectorParameterValue.h"
#include "AI/NavigationSystemBase.h"
#include "MaterialEditor/MaterialEditorInstanceConstant.h"
#include "MaterialEditor/MaterialEditorPreviewParameters.h"
#include "MaterialEditor/MaterialEditorMeshComponent.h"
#include "MaterialEditorModule.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialFunctionInstance.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureSampleParameter.h"
#include "Materials/MaterialExpressionFontSampleParameter.h"
#include "Materials/MaterialExpressionMaterialAttributeLayers.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionStaticComponentMaskParameter.h"
#include "UObject/UObjectIterator.h"
#include "PropertyEditorDelegates.h"
#include "IDetailsView.h"
#include "MaterialEditingLibrary.h"
#include "MaterialPropertyHelpers.h"
#include "MaterialStatsCommon.h"

/**
 * Class for rendering the material on the preview mesh in the Material Editor
 */
class FPreviewMaterial : public FMaterialResource
{
public:
	virtual ~FPreviewMaterial()
	{
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
	virtual bool ShouldCache(EShaderPlatform Platform, const FShaderType* ShaderType, const FVertexFactoryType* VertexFactoryType) const override
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

				extern ENGINE_API bool IsGPUSkinCacheAvailable(EShaderPlatform Platform);
				bool bSkinCache = IsGPUSkinCacheAvailable(Platform) && (VertexFactoryType == FindVertexFactoryType(FName(TEXT("FGPUSkinPassthroughVertexFactory"), FNAME_Find)));
					
				if (
					VertexFactoryType != FindVertexFactoryType(FName(TEXT("TGPUSkinVertexFactoryDefault"), FNAME_Find)) &&
					VertexFactoryType != FindVertexFactoryType(FName(TEXT("TGPUSkinVertexFactoryUnlimited"), FNAME_Find)) &&
					!bSkinCache
					)
				{
					return false;
				}
			}

			// Only allow shaders that are used in the stats.
			if (bEditorStatsMaterial)
			{
				TMap<FName, TArray<FMaterialStatsUtils::FRepresentativeShaderInfo>> ShaderTypeNamesAndDescriptions;
				FMaterialStatsUtils::GetRepresentativeShaderTypesAndDescriptions(ShaderTypeNamesAndDescriptions, this);

				for (auto DescriptionPair : ShaderTypeNamesAndDescriptions)
				{
					auto &DescriptionArray = DescriptionPair.Value;
					if (DescriptionArray.FindByPredicate([ShaderType = ShaderType](auto& Info) { return Info.ShaderName == ShaderType->GetFName(); }))
					{
						return true;
					}
				}

				return false;
			}

			// look for any of the needed type
			bool bShaderTypeMatches = false;

			// For FMaterialResource::GetRepresentativeInstructionCounts
			if (FCString::Stristr(ShaderType->GetName(), TEXT("MaterialCHSFNoLightMapPolicy")))
			{
				bShaderTypeMatches = true;
			}
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("MobileDirectionalLight")))
			{
				bShaderTypeMatches = true;
			}
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("MobileMovableDirectionalLight")))
			{
				bShaderTypeMatches = true;
			}
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("BasePassPSTDistanceFieldShadowsAndLightMapPolicyHQ")))
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
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("Distortion")))
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
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("FVelocity")))
			{
				bShaderTypeMatches = true;
			}
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("FAnisotropy")))
			{
				bShaderTypeMatches = true;
			}
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("RayTracingDynamicGeometryConverter")))
			{
				bShaderTypeMatches = true;
			}
			else if (FCString::Stristr(ShaderType->GetName(), TEXT("FLumenCard")))
			{
				bShaderTypeMatches = true;
			}

			return bShaderTypeMatches;
		}
	
	}

	/**
	 * Should shaders compiled for this material be saved to disk?
	 */
	virtual bool IsPersistent() const override { return false; }
	virtual FString GetAssetName() const override { return FString::Printf(TEXT("Preview:%s"), *FMaterialResource::GetAssetName()); }
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

// Helper struct to cache data for UMaterialEditorInstanceConstant/UMaterialEditorPreviewParameters::RegenerateArrays()
struct FMaterialParamExpressionData
{
	FName Name = NAME_None;
	FName Group = NAME_None;
	UClass* ParamType = nullptr;
	int32 SortPriority = 32;
};

// Helper struct to cache data for UMaterialEditorInstanceConstant/UMaterialEditorPreviewParameters::RegenerateArrays()
struct FMaterialExpressionParameterDataCache
{
	TMap<FName, FMaterialParamExpressionData> GlobalParameters;
	FName LayerParameterName = NAME_None;
	TArray<TMap<FName, FMaterialParamExpressionData>> LayerParameters;
	TArray<TMap<FName, FMaterialParamExpressionData>> BlendParameters;
};

// Helper function for UMaterialEditorInstanceConstant/UMaterialEditorPreviewParameters::RegenerateArrays()
// Cache material expression parameter for group and sort priority for quick lookup while creating UDEditorParameterValue
FMaterialExpressionParameterDataCache CacheMaterialExpressionParameterData(const UMaterial* InBaseMaterial, const FStaticParameterSet& InStaticParameters)
{
	FMaterialExpressionParameterDataCache ParamCache;
	ParamCache.GlobalParameters.Reserve(InBaseMaterial->Expressions.Num());

	// Function replicating UMaterialFunctionInterface::GetParameterGroupName & UMaterialFunctionInterface::GetParameterSortPriority behavior 
	// but caching all the data in one pass
	auto CacheMaterialFunctionParameterData = [](UMaterialFunctionInterface* ParameterFunction, TMap<FName, FMaterialParamExpressionData>& ParamDatas)
	{
		if (ParameterFunction)
		{
			TArray<UMaterialFunctionInterface*> Functions;
			ParameterFunction->GetDependentFunctions(Functions);
			Functions.AddUnique(ParameterFunction);

			for (UMaterialFunctionInterface* Function : Functions)
			{
				for (UMaterialExpression* FunctionExpression : *Function->GetFunctionExpressions())
				{
					if (const UMaterialExpressionParameter* Parameter = Cast<const UMaterialExpressionParameter>(FunctionExpression))
					{
						FMaterialParamExpressionData ParamData;
						ParamData.ParamType = UMaterialExpressionParameter::StaticClass();

						ParamData.Name = Parameter->ParameterName;
						ParamData.SortPriority = Parameter->SortPriority;
						ParamData.Group = Parameter->Group;

						//ensure(!ParamDatas.Contains(ParamData.Name));
						ParamDatas.Add(ParamData.Name, ParamData);
					}
					else if (const UMaterialExpressionTextureSampleParameter* TexParameter = Cast<const UMaterialExpressionTextureSampleParameter>(FunctionExpression))
					{
						FMaterialParamExpressionData ParamData;
						ParamData.ParamType = UMaterialExpressionTextureSampleParameter::StaticClass();

						ParamData.Name = TexParameter->ParameterName;
						ParamData.SortPriority = TexParameter->SortPriority;
						ParamData.Group = TexParameter->Group;

						//ensure(!ParamDatas.Contains(ParamData.Name));
						ParamDatas.Add(ParamData.Name, ParamData);
					}
					else if (const UMaterialExpressionFontSampleParameter* FontParameter = Cast<const UMaterialExpressionFontSampleParameter>(FunctionExpression))
					{
						FMaterialParamExpressionData ParamData;
						ParamData.ParamType = UMaterialExpressionFontSampleParameter::StaticClass();

						ParamData.Name = FontParameter->ParameterName;
						ParamData.SortPriority = FontParameter->SortPriority;
						ParamData.Group = FontParameter->Group;

						//ensure(!ParamDatas.Contains(ParamData.Name));
						ParamDatas.Add(ParamData.Name, ParamData);
					}
				}
			}
		}
	};

	for (int32 Index = 0; Index < InBaseMaterial->Expressions.Num(); ++Index)
	{
		UMaterialExpression* Expression = InBaseMaterial->Expressions[Index];

		if (UMaterialExpressionParameter* Parameter = Cast<UMaterialExpressionParameter>(Expression))
		{
			FMaterialParamExpressionData ParamData;
			ParamData.ParamType = UMaterialExpressionParameter::StaticClass();

			ParamData.Name = Parameter->GetParameterName();
			ParamData.SortPriority = Parameter->SortPriority;
			ParamData.Group = Parameter->Group;

			//ensure(ParamCache.GlobalParameters.Contains(ParamData.Name));
			ParamCache.GlobalParameters.Add(ParamData.Name, ParamData);
		}
		else if (UMaterialExpressionTextureSampleParameter* TexParameter = Cast<UMaterialExpressionTextureSampleParameter>(Expression))
		{
			FMaterialParamExpressionData ParamData;
			ParamData.ParamType = UMaterialExpressionTextureSampleParameter::StaticClass();

			ParamData.Name = TexParameter->GetParameterName();
			ParamData.SortPriority = TexParameter->SortPriority;
			ParamData.Group = TexParameter->Group;

			//ensure(!ParamCache.GlobalParameters.Contains(ParamData.Name));
			ParamCache.GlobalParameters.Add(ParamData.Name, ParamData);
		}
		else if (UMaterialExpressionRuntimeVirtualTextureSampleParameter* VTTexParameter = Cast<UMaterialExpressionRuntimeVirtualTextureSampleParameter>(Expression))
		{
			FMaterialParamExpressionData ParamData;
			ParamData.ParamType = UMaterialExpressionRuntimeVirtualTextureSampleParameter::StaticClass();

			ParamData.Name = VTTexParameter->GetParameterName();
			ParamData.SortPriority = VTTexParameter->SortPriority;
			ParamData.Group = VTTexParameter->Group;

			//ensure(!ParamCache.GlobalParameters.Contains(ParamData.Name));
			ParamCache.GlobalParameters.Add(ParamData.Name, ParamData);
		}
		else if (UMaterialExpressionFontSampleParameter* FontParameter = Cast<UMaterialExpressionFontSampleParameter>(Expression))
		{
			FMaterialParamExpressionData ParamData;
			ParamData.ParamType = UMaterialExpressionFontSampleParameter::StaticClass();

			ParamData.Name = FontParameter->GetParameterName();
			ParamData.SortPriority = FontParameter->SortPriority;
			ParamData.Group = FontParameter->Group;

			//ensure(!ParamCache.GlobalParameters.Contains(ParamData.Name));
			ParamCache.GlobalParameters.Add(ParamData.Name, ParamData);
		}
		else if (UMaterialExpressionMaterialFunctionCall* FuncParameter = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
		{
			if (FuncParameter->MaterialFunction)
			{
				if (UMaterialFunctionInterface* ParameterFunction = FuncParameter->MaterialFunction->GetBaseFunction())
				{
					CacheMaterialFunctionParameterData(ParameterFunction, ParamCache.GlobalParameters);
				}
			}
		}
		else if (UMaterialExpressionMaterialAttributeLayers* LayerParameter = Cast<UMaterialExpressionMaterialAttributeLayers>(Expression))
		{
			// there should only be one Material attribute layer expression per material
			check(ParamCache.LayerParameterName == NAME_None);
			ParamCache.LayerParameterName = LayerParameter->ParameterName;

			// look into the instance static parameters first for overrides
			UMaterialFunctionInterface* Function = nullptr;
			const FStaticMaterialLayersParameter* StaticLayers = InStaticParameters.MaterialLayersParameters.FindByPredicate([LayerParameterName = LayerParameter->ParameterName](const FStaticMaterialLayersParameter& Layers)
			{
				return LayerParameterName == Layers.ParameterInfo.Name;
			});

			// If we found one cache those instead of what is on the material itself since they take precedence
			if (StaticLayers)
			{
				// Replicate FStaticMaterialLayersParameter::GetParameterAssociatedFunction behavior while caching all function needed info

				// Cache layer parameters
				for (UMaterialFunctionInterface* Layer : StaticLayers->Value.Layers)
				{
					TMap<FName, FMaterialParamExpressionData>& LayerCache = ParamCache.LayerParameters.AddDefaulted_GetRef();
					CacheMaterialFunctionParameterData(Layer, LayerCache);
				}

				// Cache blend parameters
				for (UMaterialFunctionInterface* Blend : StaticLayers->Value.Blends)
				{
					TMap<FName, FMaterialParamExpressionData>& BlendCache = ParamCache.LayerParameters.AddDefaulted_GetRef();
					CacheMaterialFunctionParameterData(Blend, BlendCache);
				}
			}
			else
			{
				// Cache layer parameters
				for (UMaterialFunctionInterface* Layer : LayerParameter->GetLayers())
				{
					TMap<FName, FMaterialParamExpressionData>& LayerCache = ParamCache.LayerParameters.AddDefaulted_GetRef();
					CacheMaterialFunctionParameterData(Layer, LayerCache);
				}

				// Cache blend parameters
				for (UMaterialFunctionInterface* Blend : LayerParameter->GetBlends())
				{
					TMap<FName, FMaterialParamExpressionData>& BlendCache = ParamCache.LayerParameters.AddDefaulted_GetRef();
					CacheMaterialFunctionParameterData(Blend, BlendCache);
				}
			}
		}
	}
	return ParamCache;
}

void UMaterialEditorPreviewParameters::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PreviewMaterial && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
		if (OriginalFunction == nullptr)
		{
			CopyToSourceInstance();
			PreviewMaterial->PostEditChangeProperty(PropertyChangedEvent);
		}
		else
		{
			ApplySourceFunctionChanges();
			if (OriginalFunction->PreviewMaterial)
			{
				OriginalFunction->PreviewMaterial->PostEditChangeProperty(PropertyChangedEvent);
			}
		}
	}
}

void UMaterialEditorPreviewParameters::AssignParameterToGroup(UMaterial* ParentMaterial, UDEditorParameterValue* ParameterValue, FName* OptionalGroupName)
{
	check(ParentMaterial);
	check(ParameterValue);

	FName ParameterGroupName;
	if (OptionalGroupName)
	{
		ParameterGroupName = *OptionalGroupName;
	}
	else
	{
		ParentMaterial->GetGroupName(ParameterValue->ParameterInfo, ParameterGroupName);
	}
	
	if (ParameterGroupName == TEXT("") || ParameterGroupName == TEXT("None"))
	{
		ParameterGroupName = TEXT("None");
	}
	IMaterialEditorModule* MaterialEditorModule = &FModuleManager::LoadModuleChecked<IMaterialEditorModule>("MaterialEditor");
	
	// Material layers
	UDEditorMaterialLayersParameterValue* MaterialLayerParam = Cast<UDEditorMaterialLayersParameterValue>(ParameterValue);
	if (ParameterValue->ParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter)
	{
		if (MaterialLayerParam)
		{
			ParameterGroupName = FMaterialPropertyHelpers::LayerParamName;
		}
		else
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

	FEditorParameterGroup& CurrentGroup = FMaterialPropertyHelpers::GetParameterGroup(PreviewMaterial, ParameterGroupName, ParameterGroups);
	CurrentGroup.GroupAssociation = ParameterValue->ParameterInfo.Association;
	ParameterValue->SetFlags(RF_Transactional);
	CurrentGroup.Parameters.Add(ParameterValue);
}

static UDEditorParameterValue* CreateParameter_Scalar(UObject* Owner, const FMaterialParameterMetadata& Meta)
{
	UDEditorScalarParameterValue* Parameter = NewObject<UDEditorScalarParameterValue>(Owner);
	if (Meta.Value.Type == EMaterialParameterType::Scalar)
	{
		Parameter->ParameterValue = Meta.Value.AsScalar();
		Parameter->SliderMin = Meta.ScalarMin;
		Parameter->SliderMax = Meta.ScalarMax;
		Parameter->AtlasData.bIsUsedAsAtlasPosition = Meta.bUsedAsAtlasPosition;
		Parameter->AtlasData.Atlas = Meta.ScalarAtlas;
		Parameter->AtlasData.Curve = Meta.ScalarCurve;
	}
	return Parameter;
}

static UDEditorParameterValue* CreateParameter_Vector(UObject* Owner, const FMaterialParameterMetadata& Meta)
{
	UDEditorVectorParameterValue* Parameter = NewObject<UDEditorVectorParameterValue>(Owner);
	if (Meta.Value.Type == EMaterialParameterType::Vector)
	{
		Parameter->ParameterValue = Meta.Value.AsLinearColor();
		Parameter->bIsUsedAsChannelMask = Meta.bUsedAsChannelMask;
		Parameter->ChannelNames.R = Meta.ChannelName[0];
		Parameter->ChannelNames.G = Meta.ChannelName[1];
		Parameter->ChannelNames.B = Meta.ChannelName[2];
		Parameter->ChannelNames.A = Meta.ChannelName[3];
	}
	return Parameter;
}

static UDEditorParameterValue* CreateParameter_Texture(UObject* Owner, const FMaterialParameterMetadata& Meta)
{
	UDEditorTextureParameterValue* Parameter = NewObject<UDEditorTextureParameterValue>(Owner);
	if (Meta.Value.Type == EMaterialParameterType::Texture)
	{
		Parameter->ParameterValue = Meta.Value.Texture;
		Parameter->ChannelNames.R = Meta.ChannelName[0];
		Parameter->ChannelNames.G = Meta.ChannelName[1];
		Parameter->ChannelNames.B = Meta.ChannelName[2];
		Parameter->ChannelNames.A = Meta.ChannelName[3];
	}
	return Parameter;
}

static UDEditorParameterValue* CreateParameter_RuntimeVirtualTexture(UObject* Owner, const FMaterialParameterMetadata& Meta)
{
	UDEditorRuntimeVirtualTextureParameterValue* Parameter = NewObject<UDEditorRuntimeVirtualTextureParameterValue>(Owner);
	if (Meta.Value.Type == EMaterialParameterType::RuntimeVirtualTexture)
	{
		Parameter->ParameterValue = Meta.Value.RuntimeVirtualTexture;
	}
	return Parameter;
}

static UDEditorParameterValue* CreateParameter_Font(UObject* Owner, const FMaterialParameterMetadata& Meta)
{
	UDEditorFontParameterValue* Parameter = NewObject<UDEditorFontParameterValue>(Owner);
	if (Meta.Value.Type == EMaterialParameterType::Font)
	{
		Parameter->ParameterValue.FontValue = Meta.Value.Font.Value;
		Parameter->ParameterValue.FontPage = Meta.Value.Font.Page;
	}
	return Parameter;
}

static UDEditorParameterValue* CreateParameter_StaticSwitch(UObject* Owner, const FMaterialParameterMetadata& Meta)
{
	UDEditorStaticSwitchParameterValue* Parameter = NewObject<UDEditorStaticSwitchParameterValue>(Owner);
	if (Meta.Value.Type == EMaterialParameterType::StaticSwitch)
	{
		Parameter->ParameterValue = Meta.Value.AsStaticSwitch();
	}
	return Parameter;
}

static UDEditorParameterValue* CreateParameter_StaticComponentMask(UObject* Owner, const FMaterialParameterMetadata& Meta)
{
	UDEditorStaticComponentMaskParameterValue* Parameter = NewObject<UDEditorStaticComponentMaskParameterValue>(Owner);
	if (Meta.Value.Type == EMaterialParameterType::StaticComponentMask)
	{
		Parameter->ParameterValue.R = Meta.Value.Bool[0];
		Parameter->ParameterValue.G = Meta.Value.Bool[1];
		Parameter->ParameterValue.B = Meta.Value.Bool[2];
		Parameter->ParameterValue.A = Meta.Value.Bool[3];
	}
	return Parameter;
}

UDEditorParameterValue* UDEditorParameterValue::Create(UObject* Owner,
	EMaterialParameterType Type,
	const FMaterialParameterInfo& ParameterInfo,
	const FMaterialParameterMetadata& Meta)
{
	UDEditorParameterValue* Parameter = nullptr;
	switch (Type)
	{
	case EMaterialParameterType::Scalar: Parameter = CreateParameter_Scalar(Owner, Meta); break;
	case EMaterialParameterType::Vector: Parameter = CreateParameter_Vector(Owner, Meta); break;
	case EMaterialParameterType::Texture: Parameter = CreateParameter_Texture(Owner, Meta); break;
	case EMaterialParameterType::RuntimeVirtualTexture: Parameter = CreateParameter_RuntimeVirtualTexture(Owner, Meta); break;
	case EMaterialParameterType::Font: Parameter = CreateParameter_Font(Owner, Meta); break;
	case EMaterialParameterType::StaticSwitch: Parameter = CreateParameter_StaticSwitch(Owner, Meta); break;
	case EMaterialParameterType::StaticComponentMask: Parameter = CreateParameter_StaticComponentMask(Owner, Meta); break;
	default: break;
	}

	check(Parameter);
	Parameter->ParameterInfo = ParameterInfo;
	Parameter->bOverride = Meta.bOverride;
	return Parameter;
}

void UMaterialEditorPreviewParameters::RegenerateArrays()
{
	ParameterGroups.Empty();
	if (PreviewMaterial)
	{
		// Only operate on base materials
		UMaterial* ParentMaterial = PreviewMaterial;

		// Use param cache to lookup group and sort priority
		auto AssignGroupAndSortPriority = [this, ParentMaterial](UDEditorParameterValue* InEditorParamValue, const FMaterialExpressionParameterDataCache& InCachedExpressionData)
		{
			const FMaterialParamExpressionData* ParamData = nullptr;
			if (InEditorParamValue->ParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter)
			{
				ParamData = InCachedExpressionData.GlobalParameters.Find(InEditorParamValue->ParameterInfo.Name);
			}
			// if the association is not 'global parameter', look into attribute layers if we have a potentially valid index
			else if (InEditorParamValue->ParameterInfo.Index >= 0)
			{
				if (InEditorParamValue->ParameterInfo.Association == EMaterialParameterAssociation::LayerParameter
					&& InCachedExpressionData.LayerParameters.IsValidIndex(InEditorParamValue->ParameterInfo.Index))
				{
					ParamData = InCachedExpressionData.LayerParameters[InEditorParamValue->ParameterInfo.Index].Find(InEditorParamValue->ParameterInfo.Name);
				}
				else if (InEditorParamValue->ParameterInfo.Association == EMaterialParameterAssociation::BlendParameter
					&& InCachedExpressionData.BlendParameters.IsValidIndex(InEditorParamValue->ParameterInfo.Index))
				{
					ParamData = InCachedExpressionData.BlendParameters[InEditorParamValue->ParameterInfo.Index].Find(InEditorParamValue->ParameterInfo.Name);
				}
			}
			FName GroupName = NAME_None;
			if (ParamData)
			{
				InEditorParamValue->SortPriority = ParamData->SortPriority;
				GroupName = ParamData->Group;
			}
			AssignParameterToGroup(ParentMaterial, InEditorParamValue, &GroupName);
		};

		// This can run before UMaterial::PostEditChangeProperty has a chance to run, so explicitly call UpdateCachedExpressionData here
		PreviewMaterial->UpdateCachedExpressionData();

		// Cache relevant material expression data used to resolve editor param value info in RegenerateArrays
		//@todo FH: can this be/should be part of `UpdateCachedExpressionData`?
		FMaterialExpressionParameterDataCache ExpressionParameterDataCache = CacheMaterialExpressionParameterData(PreviewMaterial, FStaticParameterSet());

		// Loop through all types of parameters for this material and add them to the parameter arrays.
		TMap<FMaterialParameterInfo, FMaterialParameterMetadata> ParameterValues;
		for (int32 TypeIndex = 0; TypeIndex < NumMaterialParameterTypes; ++TypeIndex)
		{
			const EMaterialParameterType ParameterType = (EMaterialParameterType)TypeIndex;

			ParentMaterial->GetAllParametersOfType(ParameterType, ParameterValues);
			for (const auto& It : ParameterValues)
			{
				UDEditorParameterValue* Parameter = UDEditorParameterValue::Create(this, ParameterType, It.Key, It.Value);
				Parameter->bOverride = true;
				AssignGroupAndSortPriority(Parameter, ExpressionParameterDataCache);
			}
		}

		// Static Material Layers Parameters
		TArray<FMaterialParameterInfo> ParameterInfo;
		TArray<FGuid> Guids;
		ParentMaterial->GetAllMaterialLayersParameterInfo(ParameterInfo, Guids);
		for (int32 ParameterIdx = 0; ParameterIdx < ParameterInfo.Num(); ParameterIdx++)
		{
			UDEditorMaterialLayersParameterValue* ParameterValue = NewObject<UDEditorMaterialLayersParameterValue>(this);
			ParameterValue->bOverride = true;
			ParameterValue->ParameterInfo = ParameterInfo[ParameterIdx];
			ParameterValue->ExpressionId = Guids[ParameterIdx];

			//get the settings from the parent in the MIC chain
			FMaterialLayersFunctions Value;
			if (PreviewMaterial->GetMaterialLayersParameterValue(ParameterValue->ParameterInfo, Value, ParameterValue->ExpressionId))
			{
				ParameterValue->ParameterValue = Value;
			}

			AssignGroupAndSortPriority(ParameterValue, ExpressionParameterDataCache);
		}
	}
	// sort contents of groups
	for (int32 ParameterIdx = 0; ParameterIdx < ParameterGroups.Num(); ParameterIdx++)
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
		ParamGroup.Parameters.Sort(FCompareUDEditorParameterValueByParameterName());
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
	ParameterGroups.Sort(FCompareFEditorParameterGroupByName());
	TArray<struct FEditorParameterGroup> ParameterDefaultGroups;
	for (int32 ParameterIdx = 0; ParameterIdx < ParameterGroups.Num(); ParameterIdx++)
	{
		FEditorParameterGroup & ParamGroup = ParameterGroups[ParameterIdx];

		if (ParamGroup.GroupName == TEXT("None"))
		{
			ParameterDefaultGroups.Add(ParamGroup);
			ParameterGroups.RemoveAt(ParameterIdx);
			break;
		}
	}
	if (ParameterDefaultGroups.Num() > 0)
	{
		ParameterGroups.Append(ParameterDefaultGroups);
	}

}

void UMaterialEditorPreviewParameters::CopyToSourceInstance()
{
	if (PreviewMaterial->IsTemplate(RF_ClassDefaultObject) == false && OriginalMaterial != nullptr)
	{
		OriginalMaterial->MarkPackageDirty();
		// Scalar Parameters
		for (int32 GroupIdx = 0; GroupIdx < ParameterGroups.Num(); GroupIdx++)
		{
			FEditorParameterGroup & Group = ParameterGroups[GroupIdx];
			for (int32 ParameterIdx = 0; ParameterIdx < Group.Parameters.Num(); ParameterIdx++)
			{
				UDEditorParameterValue* Parameter = Group.Parameters[ParameterIdx];
				if (Parameter)
				{
					FMaterialParameterMetadata EditorValue;
					if (Parameter->GetValue(EditorValue))
					{
						PreviewMaterial->SetParameterValueEditorOnly(Parameter->ParameterInfo.Name, EditorValue);
					}
				}
			}
		}
	}
}

FName UMaterialEditorPreviewParameters::GlobalGroupPrefix = FName("Global ");

void UMaterialEditorPreviewParameters::ApplySourceFunctionChanges()
{
	if (OriginalFunction != nullptr)
	{
		CopyToSourceInstance();

		OriginalFunction->MarkPackageDirty();
		// Scalar Parameters
		for (int32 GroupIdx = 0; GroupIdx < ParameterGroups.Num(); GroupIdx++)
		{
			FEditorParameterGroup & Group = ParameterGroups[GroupIdx];
			for (int32 ParameterIdx = 0; ParameterIdx < Group.Parameters.Num(); ParameterIdx++)
			{
				UDEditorParameterValue* Parameter = Group.Parameters[ParameterIdx];
				if (Parameter)
				{
					FMaterialParameterMetadata EditorValue;
					if (Parameter->GetValue(EditorValue))
					{
						OriginalFunction->SetParameterValueEditorOnly(Parameter->ParameterInfo.Name, EditorValue);
					}
				}
			}
		}
		UMaterialEditingLibrary::UpdateMaterialFunction(OriginalFunction, PreviewMaterial);
	}
}


#if WITH_EDITOR
void UMaterialEditorPreviewParameters::PostEditUndo()
{
	Super::PostEditUndo();
}
#endif


FName UMaterialEditorInstanceConstant::GlobalGroupPrefix = FName("Global ");

UMaterialEditorInstanceConstant::UMaterialEditorInstanceConstant(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsFunctionPreviewMaterial = false;
	bShowOnlyOverrides = false;
}

void UMaterialEditorInstanceConstant::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (SourceInstance)
	{
		FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
		bool bLayersParameterChanged = false;

		FNavigationLockContext NavUpdateLock(ENavigationLockReason::MaterialUpdate);

		if(PropertyThatChanged && PropertyThatChanged->GetName()==TEXT("Parent") )
		{
			if(bIsFunctionPreviewMaterial)
			{
				bIsFunctionInstanceDirty = true;
				ApplySourceFunctionChanges();
			}
			else
			{
				FMaterialUpdateContext Context;

				UpdateSourceInstanceParent();

				Context.AddMaterialInstance(SourceInstance);

				// Fully update static parameters before recreating render state for all components
				SetSourceInstance(SourceInstance);
			}
		
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

void  UMaterialEditorInstanceConstant::AssignParameterToGroup(UMaterial*, UDEditorParameterValue* ParameterValue, const FName* OptionalGroupName)
{
	check(ParameterValue);

	FName ParameterGroupName;
	if (OptionalGroupName)
	{
		ParameterGroupName = *OptionalGroupName;
	}
	else
	{
		SourceInstance->GetGroupName(ParameterValue->ParameterInfo, ParameterGroupName);
	}

	if (ParameterGroupName == TEXT("") || ParameterGroupName == TEXT("None"))
	{
		if (bUseOldStyleMICEditorGroups == true)
		{
			ParameterGroupName = ParameterValue->GetDefaultGroupName();
		}
		else
		{
			ParameterGroupName = TEXT("None");
		}

		IMaterialEditorModule* MaterialEditorModule = &FModuleManager::LoadModuleChecked<IMaterialEditorModule>("MaterialEditor");

		// Material layers
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

	FEditorParameterGroup& CurrentGroup = FMaterialPropertyHelpers::GetParameterGroup(Parent->GetMaterial(), ParameterGroupName, ParameterGroups);
	CurrentGroup.GroupAssociation = ParameterValue->ParameterInfo.Association;
	ParameterValue->SetFlags(RF_Transactional);
	CurrentGroup.Parameters.Add(ParameterValue);
}

void UMaterialEditorInstanceConstant::RegenerateArrays()
{
	VisibleExpressions.Empty();
	ParameterGroups.Empty();

	if (Parent)
	{	
		// Use param cache to lookup group and sort priority
		auto AssignGroupAndSortPriority = [this](UDEditorParameterValue* InEditorParamValue, const FMaterialExpressionParameterDataCache& InCachedExpressionData)
		{
			const FMaterialParamExpressionData* ParamData = nullptr;
			if (InEditorParamValue->ParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter)
			{
				ParamData = InCachedExpressionData.GlobalParameters.Find(InEditorParamValue->ParameterInfo.Name);
			}
			// if the association is not 'global parameter', look into attribute layers if we have a potentially valid index
			else if (InEditorParamValue->ParameterInfo.Index >= 0)
			{
				if (InEditorParamValue->ParameterInfo.Association == EMaterialParameterAssociation::LayerParameter
					&& InCachedExpressionData.LayerParameters.IsValidIndex(InEditorParamValue->ParameterInfo.Index))
				{
					ParamData = InCachedExpressionData.LayerParameters[InEditorParamValue->ParameterInfo.Index].Find(InEditorParamValue->ParameterInfo.Name);
				}
				else if (InEditorParamValue->ParameterInfo.Association == EMaterialParameterAssociation::BlendParameter
					&& InCachedExpressionData.BlendParameters.IsValidIndex(InEditorParamValue->ParameterInfo.Index))
				{
					ParamData = InCachedExpressionData.BlendParameters[InEditorParamValue->ParameterInfo.Index].Find(InEditorParamValue->ParameterInfo.Name);
				}
			}
			if (ParamData)
			{
				InEditorParamValue->SortPriority = ParamData->SortPriority;
			}
			AssignParameterToGroup(nullptr/*useless param: Parent->GetMaterial()*/, InEditorParamValue, ParamData ? &ParamData->Group : nullptr);
		};

		// Only operate on base materials
		UMaterial* ParentMaterial = Parent->GetMaterial();
		SourceInstance->UpdateParameterNames();	// Update any parameter names that may have changed.
		SourceInstance->UpdateCachedLayerParameters();

		// Get all static parameters from the source instance.  This will handle inheriting parent values.	
		FStaticParameterSet SourceStaticParameters;
 		SourceInstance->GetStaticParameterValues(SourceStaticParameters);

		// Loop through all types of parameters for this material and add them to the parameter arrays.
		
		// Need to get layer info first as other params are collected from layers
		{
			TArray<FMaterialParameterInfo> OutParameterInfo;
			TArray<FGuid> Guids;
			SourceInstance->GetAllMaterialLayersParameterInfo(OutParameterInfo, Guids);
			// Copy Static Material Layers Parameters
			for (int32 ParameterIdx = 0; ParameterIdx < SourceStaticParameters.MaterialLayersParameters.Num(); ParameterIdx++)
			{
				FStaticMaterialLayersParameter MaterialLayersParameterParameterValue = FStaticMaterialLayersParameter(SourceStaticParameters.MaterialLayersParameters[ParameterIdx]);
				UDEditorMaterialLayersParameterValue& ParameterValue = *(NewObject<UDEditorMaterialLayersParameterValue>(this));

				ParameterValue.ParameterValue = MaterialLayersParameterParameterValue.Value;
				ParameterValue.bOverride = MaterialLayersParameterParameterValue.bOverride;
				ParameterValue.ParameterInfo = MaterialLayersParameterParameterValue.ParameterInfo;
				ParameterValue.ExpressionId = MaterialLayersParameterParameterValue.ExpressionGUID;

				AssignParameterToGroup(ParentMaterial, &ParameterValue);
			}
		}

		// Cache relevant material expression data to resolve editor param value info
		FMaterialExpressionParameterDataCache ExpressionParameterDataCache = CacheMaterialExpressionParameterData(SourceInstance->GetMaterial(), SourceInstance->GetStaticParameters());

		TMap<FMaterialParameterInfo, FMaterialParameterMetadata> ParameterValues;
		for (int32 TypeIndex = 0; TypeIndex < NumMaterialParameterTypes; ++TypeIndex)
		{
			const EMaterialParameterType ParameterType = (EMaterialParameterType)TypeIndex;
			if (IsStaticMaterialParameter(ParameterType))
			{
				// Logic for static parameters is slightly different, handled both by GetStaticParameterValues above, and code below
				continue;
			}

			SourceInstance->GetAllParametersOfType(ParameterType, ParameterValues);
			for (const auto& It : ParameterValues)
			{
				UDEditorParameterValue* Parameter = UDEditorParameterValue::Create(this, ParameterType, It.Key, It.Value);
				AssignGroupAndSortPriority(Parameter, ExpressionParameterDataCache);
			}
		}

		// Copy Static Switch Parameters
		for(int32 ParameterIdx = 0; ParameterIdx < SourceStaticParameters.StaticSwitchParameters.Num(); ParameterIdx++)
		{	
			const FStaticSwitchParameter& StaticSwitchParameterValue = SourceStaticParameters.StaticSwitchParameters[ParameterIdx];
			UDEditorStaticSwitchParameterValue* ParamValue = NewObject<UDEditorStaticSwitchParameterValue>(this);

			ParamValue->ParameterValue = StaticSwitchParameterValue.Value;
			ParamValue->bOverride = StaticSwitchParameterValue.bOverride;
			ParamValue->ParameterInfo = StaticSwitchParameterValue.ParameterInfo;
			ParamValue->ExpressionId = StaticSwitchParameterValue.ExpressionGUID;
			AssignGroupAndSortPriority(ParamValue, ExpressionParameterDataCache);
		}

		// Copy Static Component Mask Parameters
		for(int32 ParameterIdx=0; ParameterIdx<SourceStaticParameters.StaticComponentMaskParameters.Num(); ParameterIdx++)
		{
			const FStaticComponentMaskParameter& StaticComponentMaskParameterValue = SourceStaticParameters.StaticComponentMaskParameters[ParameterIdx];
			UDEditorStaticComponentMaskParameterValue* ParamValue = NewObject<UDEditorStaticComponentMaskParameterValue>(this);

			ParamValue->ParameterValue.R = StaticComponentMaskParameterValue.R;
			ParamValue->ParameterValue.G = StaticComponentMaskParameterValue.G;
			ParamValue->ParameterValue.B = StaticComponentMaskParameterValue.B;
			ParamValue->ParameterValue.A = StaticComponentMaskParameterValue.A;
			ParamValue->bOverride = StaticComponentMaskParameterValue.bOverride;
			ParamValue->ParameterInfo = StaticComponentMaskParameterValue.ParameterInfo;
			ParamValue->ExpressionId = StaticComponentMaskParameterValue.ExpressionGUID;
			AssignGroupAndSortPriority(ParamValue, ExpressionParameterDataCache);
		}

		IMaterialEditorModule* MaterialEditorModule = &FModuleManager::LoadModuleChecked<IMaterialEditorModule>( "MaterialEditor" );
		MaterialEditorModule->GetVisibleMaterialParameters(ParentMaterial, SourceInstance, VisibleExpressions);
	}

	// sort contents of groups
	for(int32 ParameterIdx = 0; ParameterIdx < ParameterGroups.Num(); ParameterIdx++)
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

	if (DetailsView.IsValid())
	{
		// Tell our source instance to update itself so the preview updates.
		DetailsView.Pin()->ForceRefresh();
	}
}

#if WITH_EDITOR
void UMaterialEditorInstanceConstant::CleanParameterStack(int32 Index, EMaterialParameterAssociation MaterialType)
{
	check(GIsEditor);
	TArray<FEditorParameterGroup> CleanedGroups;
	for (FEditorParameterGroup Group : ParameterGroups)
	{
		FEditorParameterGroup DuplicatedGroup = FEditorParameterGroup();
		DuplicatedGroup.GroupAssociation = Group.GroupAssociation;
		DuplicatedGroup.GroupName = Group.GroupName;
		DuplicatedGroup.GroupSortPriority = Group.GroupSortPriority;
		for (UDEditorParameterValue* Parameter : Group.Parameters)
		{
			if (Parameter->ParameterInfo.Association != MaterialType
				|| Parameter->ParameterInfo.Index != Index)
			{
				DuplicatedGroup.Parameters.Add(Parameter);
			}
		}
		CleanedGroups.Add(DuplicatedGroup);
	}

	ParameterGroups = CleanedGroups;
	CopyToSourceInstance(true);
}
void UMaterialEditorInstanceConstant::ResetOverrides(int32 Index, EMaterialParameterAssociation MaterialType)
{
	check(GIsEditor);

	for (const FEditorParameterGroup& Group : ParameterGroups)
	{
		for (UDEditorParameterValue* Parameter : Group.Parameters)
		{
			if (Parameter->ParameterInfo.Association == MaterialType
				&& Parameter->ParameterInfo.Index == Index)
			{
				const EMaterialParameterType ParameterType = Parameter->GetParameterType();
				if (ParameterType != EMaterialParameterType::None)
				{
					FMaterialParameterMetadata SourceValue;
					if (SourceInstance->GetParameterValue(ParameterType, Parameter->ParameterInfo, SourceValue, EMaterialGetParameterValueFlags::CheckInstanceOverrides))
					{
						Parameter->bOverride = SourceValue.bOverride;
					}
				}
			}
		}
	}
	CopyToSourceInstance(true);

}
#endif

void UMaterialEditorInstanceConstant::CopyToSourceInstance(const bool bForceStaticPermutationUpdate)
{
	if (SourceInstance && !SourceInstance->IsTemplate(RF_ClassDefaultObject))
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

		{
			FMaterialInstanceParameterUpdateContext UpdateContext(SourceInstance);
			UpdateContext.SetBasePropertyOverrides(BasePropertyOverrides);
			UpdateContext.SetForceStaticPermutationUpdate(bForceStaticPermutationUpdate);

			for (int32 GroupIdx = 0; GroupIdx < ParameterGroups.Num(); GroupIdx++)
			{
				FEditorParameterGroup& Group = ParameterGroups[GroupIdx];
				for (int32 ParameterIdx = 0; ParameterIdx < Group.Parameters.Num(); ParameterIdx++)
				{
					UDEditorParameterValue* Parameter = Group.Parameters[ParameterIdx];
					if (Parameter && Parameter->bOverride)
					{
						FMaterialParameterMetadata EditorValue;
						if (Parameter->GetValue(EditorValue))
						{
							UpdateContext.SetParameterValueEditorOnly(Parameter->ParameterInfo, EditorValue, EMaterialSetParameterValueFlags::SetCurveAtlas);
						}
						else
						{
							// Material layers param
							UDEditorMaterialLayersParameterValue* MaterialLayersParameterValue = Cast<UDEditorMaterialLayersParameterValue>(Parameter);
							if (MaterialLayersParameterValue)
							{
								const FMaterialLayersFunctions& MaterialLayers = MaterialLayersParameterValue->ParameterValue;
								FGuid ExpressionIdValue = MaterialLayersParameterValue->ExpressionId;

								FStaticMaterialLayersParameter* NewParameter = new(UpdateContext.GetStaticParameters().MaterialLayersParameters)
									FStaticMaterialLayersParameter(MaterialLayersParameterValue->ParameterInfo, MaterialLayers, MaterialLayersParameterValue->bOverride, ExpressionIdValue);
							}
						}
					}
				}
			}
		}

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
		SourceFunction->RuntimeVirtualTextureParameterValues = SourceInstance->RuntimeVirtualTextureParameterValues;
		SourceFunction->FontParameterValues = SourceInstance->FontParameterValues;

		const FStaticParameterSet& StaticParameters = SourceInstance->GetStaticParameters();
		SourceFunction->StaticSwitchParameterValues = StaticParameters.StaticSwitchParameters;
		SourceFunction->StaticComponentMaskParameterValues = StaticParameters.StaticComponentMaskParameters;

		SourceFunction->MarkPackageDirty();
		bIsFunctionInstanceDirty = false;

		UMaterialEditingLibrary::UpdateMaterialFunction(SourceFunction, nullptr);
	}
}

void UMaterialEditorInstanceConstant::SetSourceInstance(UMaterialInstanceConstant* MaterialInterface)
{
	check(MaterialInterface);
	SourceInstance = MaterialInterface;
	Parent = SourceInstance->Parent;
	PhysMaterial = SourceInstance->PhysMaterial;

	CopyBasePropertiesFromParent();

	RegenerateArrays();

	//propagate changes to the base material so the instance will be updated if it has a static permutation resource
	FMaterialInstanceParameterUpdateContext UpdateContext(SourceInstance);
	for (int32 GroupIdx = 0; GroupIdx < ParameterGroups.Num(); GroupIdx++)
	{
		FEditorParameterGroup& Group = ParameterGroups[GroupIdx];
		for (int32 ParameterIdx = 0; ParameterIdx < Group.Parameters.Num(); ParameterIdx++)
		{
			UDEditorParameterValue* Parameter = Group.Parameters[ParameterIdx];
			if (Parameter && Parameter->bOverride)
			{
				FMaterialParameterMetadata EditorValue;
				if (Parameter->GetValue(EditorValue))
				{
					// Only want to update static parameters here
					if (IsStaticMaterialParameter(EditorValue.Value.Type))
					{
						UpdateContext.SetParameterValueEditorOnly(Parameter->ParameterInfo, EditorValue);
					}
				}
				else
				{
					// Material layers param
					UDEditorMaterialLayersParameterValue* MaterialLayersParameterValue = Cast<UDEditorMaterialLayersParameterValue>(Group.Parameters[ParameterIdx]);
					if (MaterialLayersParameterValue)
					{
						const FMaterialLayersFunctions& MaterialLayers = MaterialLayersParameterValue->ParameterValue;
						FGuid ExpressionIdValue = MaterialLayersParameterValue->ExpressionId;

						FStaticMaterialLayersParameter* NewParameter = new(UpdateContext.GetStaticParameters().MaterialLayersParameters)
							FStaticMaterialLayersParameter(MaterialLayersParameterValue->ParameterInfo, MaterialLayers, MaterialLayersParameterValue->bOverride, ExpressionIdValue);
					}
				}
			}
		}
	}
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
	SourceInstance->PostEditChange();
}


void UMaterialEditorInstanceConstant::CopyBasePropertiesFromParent()
{
	BasePropertyOverrides = SourceInstance->BasePropertyOverrides;
	// Copy the overrides (if not yet overridden), so they match their true values in the UI
	if (!BasePropertyOverrides.bOverride_OpacityMaskClipValue)
	{
		BasePropertyOverrides.OpacityMaskClipValue = SourceInstance->GetOpacityMaskClipValue();
	}
	if (!BasePropertyOverrides.bOverride_BlendMode)
	{
		BasePropertyOverrides.BlendMode = SourceInstance->GetBlendMode();
	}
	if (!BasePropertyOverrides.bOverride_ShadingModel)
	{
		if (SourceInstance->IsShadingModelFromMaterialExpression())
		{
			BasePropertyOverrides.ShadingModel = MSM_FromMaterialExpression;
		}
		else
		{
			BasePropertyOverrides.ShadingModel = SourceInstance->GetShadingModels().GetFirstShadingModel(); 
		}
	}
	if (!BasePropertyOverrides.bOverride_TwoSided)
	{
		BasePropertyOverrides.TwoSided = SourceInstance->IsTwoSided();
	}
	if (!BasePropertyOverrides.bOverride_OutputTranslucentVelocity)
	{
		BasePropertyOverrides.bOutputTranslucentVelocity = SourceInstance->IsTranslucencyWritingVelocity();
	}
	if (!BasePropertyOverrides.DitheredLODTransition)
	{
		BasePropertyOverrides.DitheredLODTransition = SourceInstance->IsDitheredLODTransition();
	}

	// Copy the Lightmass settings...
	// The lightmass functions (GetCastShadowAsMasked, etc.) check if the value is overridden and returns the current value if so, otherwise returns the parent value
	// So we don't need to wrap these in the same "if not overriding" as above
	LightmassSettings.CastShadowAsMasked.ParameterValue = SourceInstance->GetCastShadowAsMasked();
	LightmassSettings.EmissiveBoost.ParameterValue = SourceInstance->GetEmissiveBoost();
	LightmassSettings.DiffuseBoost.ParameterValue = SourceInstance->GetDiffuseBoost();
	LightmassSettings.ExportResolutionScale.ParameterValue = SourceInstance->GetExportResolutionScale();

	//Copy refraction settings
	SourceInstance->GetRefractionSettings(RefractionDepthBias);

	bOverrideSubsurfaceProfile = SourceInstance->bOverrideSubsurfaceProfile;
	// Copy the subsurface profile. GetSubsurfaceProfile_Internal() will return either the overridden profile or one from a parent
	SubsurfaceProfile = SourceInstance->GetSubsurfaceProfile_Internal();
}

#if WITH_EDITOR
void UMaterialEditorInstanceConstant::PostEditUndo()
{
	Super::PostEditUndo();

	if (bIsFunctionPreviewMaterial && SourceFunction)
	{
		bIsFunctionInstanceDirty = true;
		ApplySourceFunctionChanges();
	}
	else if (SourceInstance)
	{
		FMaterialUpdateContext Context;

		UpdateSourceInstanceParent();

		Context.AddMaterialInstance(SourceInstance);
	}
}
#endif

UMaterialEditorMeshComponent::UMaterialEditorMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}
