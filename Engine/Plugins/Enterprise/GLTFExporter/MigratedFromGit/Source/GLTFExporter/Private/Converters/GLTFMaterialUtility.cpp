// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMaterialUtility.h"
#include "Converters/GLTFTextureUtility.h"
#include "Converters/GLTFNameUtility.h"
#include "Misc/DefaultValueHelper.h"
#if WITH_EDITOR
#include "GLTFMaterialAnalyzer.h"
#include "GLTFMaterialBaking/Public/IMaterialBakingModule.h"
#include "GLTFMaterialBaking/Public/MaterialBakingStructures.h"
#include "Modules/ModuleManager.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionCustomOutput.h"
#include "Materials/MaterialExpressionClearCoatNormalCustomOutput.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#endif

#define PROXY_MATERIAL_NAME_PREFIX TEXT("M_GLTF_")
#define PROXY_MATERIAL_ROOT_PATH   TEXT("/GLTFExporter/Materials/Proxy/")

UMaterialInterface* FGLTFMaterialUtility::GetDefaultMaterial()
{
	static UMaterialInterface* DefaultMaterial = GetProxyBaseMaterial(EGLTFJsonShadingModel::Default);
	return DefaultMaterial;
}

UMaterialInterface* FGLTFMaterialUtility::GetProxyBaseMaterial(EGLTFJsonShadingModel ShadingModel)
{
	const FString Name = FGLTFJsonUtility::GetValue(ShadingModel);
	const FString Path = PROXY_MATERIAL_ROOT_PATH PROXY_MATERIAL_NAME_PREFIX + Name + TEXT(".") PROXY_MATERIAL_NAME_PREFIX + Name;
	return LoadObject<UMaterialInterface>(nullptr, *Path);
}

bool FGLTFMaterialUtility::IsProxyMaterial(const UMaterial* Material)
{
	return Material->GetPathName().StartsWith(PROXY_MATERIAL_ROOT_PATH PROXY_MATERIAL_NAME_PREFIX);
}

bool FGLTFMaterialUtility::IsProxyMaterial(const UMaterialInterface* Material)
{
	return IsProxyMaterial(Material->GetMaterial());
}

bool FGLTFMaterialUtility::GetNonDefaultParameterValue(const UMaterialInterface* Material, const FString& PropertyName, float& OutValue)
{
	const FHashedMaterialParameterInfo ParameterInfo = *PropertyName;

	float DefaultValue;
	if (!Material->GetScalarParameterDefaultValue(ParameterInfo, DefaultValue))
	{
		// TODO: report error
		return false;
	}

	float Value;
	if (!Material->GetScalarParameterValue(ParameterInfo, Value, true) || Value == DefaultValue)
	{
		return false;
	}

	OutValue = Value;
	return true;
}

bool FGLTFMaterialUtility::GetNonDefaultParameterValue(const UMaterialInterface* Material, const FString& PropertyName, FLinearColor& OutValue)
{
	const FHashedMaterialParameterInfo ParameterInfo = *PropertyName;

	FLinearColor DefaultValue;
	if (!Material->GetVectorParameterDefaultValue(ParameterInfo, DefaultValue))
	{
		// TODO: report error
		return false;
	}

	FLinearColor Value;
	if (!Material->GetVectorParameterValue(ParameterInfo, Value, true) || Value == DefaultValue)
	{
		return false;
	}

	OutValue = Value;
	return true;
}

bool FGLTFMaterialUtility::GetNonDefaultParameterValue(const UMaterialInterface* Material, const FString& PropertyName, UTexture*& OutValue)
{
	const FHashedMaterialParameterInfo ParameterInfo = *PropertyName;

	UTexture* DefaultValue;
	if (!Material->GetTextureParameterDefaultValue(ParameterInfo, DefaultValue))
	{
		// TODO: report error
		return false;
	}

	UTexture* Value;
	if (!Material->GetTextureParameterValue(ParameterInfo, Value, true) || Value == DefaultValue)
	{
		return false;
	}

	OutValue = Value;
	return true;
}

#if WITH_EDITOR

void FGLTFMaterialUtility::SetNonDefaultParameterValue(UMaterialInstanceConstant* Material, const FString& PropertyName, float Value)
{
	const FHashedMaterialParameterInfo ParameterInfo = *PropertyName;

	float DefaultValue;
	if (!Material->GetScalarParameterDefaultValue(ParameterInfo, DefaultValue))
	{
		// TODO: report error
		return;
	}

	if (DefaultValue != Value)
	{
		Material->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(ParameterInfo), Value);
	}
}

void FGLTFMaterialUtility::SetNonDefaultParameterValue(UMaterialInstanceConstant* Material, const FString& PropertyName, const FLinearColor& Value)
{
	const FHashedMaterialParameterInfo ParameterInfo = *PropertyName;

	FLinearColor DefaultValue;
	if (!Material->GetVectorParameterDefaultValue(ParameterInfo, DefaultValue))
	{
		// TODO: report error
		return;
	}

	if (DefaultValue != Value)
	{
		Material->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(ParameterInfo), Value);
	}
}

void FGLTFMaterialUtility::SetNonDefaultParameterValue(UMaterialInstanceConstant* Material, const FString& PropertyName, UTexture* Value)
{
	const FHashedMaterialParameterInfo ParameterInfo = *PropertyName;

	UTexture* DefaultValue;
	if (!Material->GetTextureParameterDefaultValue(ParameterInfo, DefaultValue))
	{
		// TODO: report error
		return;
	}

	if (DefaultValue != Value)
	{
		Material->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(ParameterInfo), Value);
	}
}

bool FGLTFMaterialUtility::IsNormalMap(const FMaterialPropertyEx& Property)
{
	return Property == MP_Normal || Property == TEXT("ClearCoatBottomNormal");
}

bool FGLTFMaterialUtility::IsSRGB(const FMaterialPropertyEx& Property)
{
	return Property == MP_BaseColor || Property == MP_EmissiveColor || Property == MP_SubsurfaceColor || Property == TEXT("TransmittanceColor");
}

#if (ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 27)
FGuid FGLTFMaterialUtility::GetAttributeID(const FMaterialPropertyEx& Property)
{
	return Property.IsCustomOutput()
		? FMaterialAttributeDefinitionMap::GetCustomAttributeID(Property.CustomOutput.ToString())
		: FMaterialAttributeDefinitionMap::GetID(Property.Type);
}

FGuid FGLTFMaterialUtility::GetAttributeIDChecked(const FMaterialPropertyEx& Property)
{
	const FGuid AttributeID = GetAttributeID(Property);
	check(AttributeID != FMaterialAttributeDefinitionMap::GetDefaultID());
	return AttributeID;
}
#endif

FVector4 FGLTFMaterialUtility::GetPropertyDefaultValue(const FMaterialPropertyEx& Property)
{
#if (ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 27)
	return FMaterialAttributeDefinitionMap::GetDefaultValue(GetAttributeIDChecked(Property));

#else
	switch (Property.Type)
	{
		case MP_EmissiveColor:          return FVector4(0,0,0,0);
		case MP_Opacity:                return FVector4(1,0,0,0);
		case MP_OpacityMask:            return FVector4(1,0,0,0);
		case MP_BaseColor:              return FVector4(0,0,0,0);
		case MP_Metallic:               return FVector4(0,0,0,0);
		case MP_Specular:               return FVector4(.5,0,0,0);
		case MP_Roughness:              return FVector4(.5,0,0,0);
		case MP_Anisotropy:             return FVector4(0,0,0,0);
		case MP_Normal:                 return FVector4(0,0,1,0);
		case MP_Tangent:                return FVector4(1,0,0,0);
		case MP_WorldPositionOffset:    return FVector4(0,0,0,0);
		case MP_WorldDisplacement:      return FVector4(0,0,0,0);
		case MP_TessellationMultiplier: return FVector4(1,0,0,0);
		case MP_SubsurfaceColor:        return FVector4(1,1,1,0);
		case MP_CustomData0:            return FVector4(1,0,0,0);
		case MP_CustomData1:            return FVector4(.1,0,0,0);
		case MP_AmbientOcclusion:       return FVector4(1,0,0,0);
		case MP_Refraction:             return FVector4(1,0,0,0);
		case MP_PixelDepthOffset:       return FVector4(0,0,0,0);
		case MP_ShadingModel:           return FVector4(0,0,0,0);
		default:                        break;
	}

	if (Property.Type >= MP_CustomizedUVs0 && Property.Type <= MP_CustomizedUVs7)
	{
		return FVector4(0,0,0,0);
	}

	if (Property == TEXT("ClearCoatBottomNormal"))
	{
		return FVector4(0,0,1,0);
	}

	if (Property == TEXT("TransmittanceColor"))
	{
		return FVector4(.5,.5,.5,0);
	}

	check(false);
	return FVector4();
#endif
}

FVector4 FGLTFMaterialUtility::GetPropertyMask(const FMaterialPropertyEx& Property)
{
#if (ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 27)
	switch (FMaterialAttributeDefinitionMap::GetValueType(GetAttributeIDChecked(Property)))
	{
		case MCT_Float:
		case MCT_Float1: return FVector4(1, 0, 0, 0);
		case MCT_Float2: return FVector4(1, 1, 0, 0);
		case MCT_Float3: return FVector4(1, 1, 1, 0);
		case MCT_Float4: return FVector4(1, 1, 1, 1);
		default:
			checkNoEntry();
			return FVector4();
	}

#else
	switch (Property.Type)
	{
		case MP_EmissiveColor:          return FVector4(1,1,1,0);
		case MP_Opacity:                return FVector4(1,0,0,0);
		case MP_OpacityMask:            return FVector4(1,0,0,0);
		case MP_BaseColor:              return FVector4(1,1,1,0);
		case MP_Metallic:               return FVector4(1,0,0,0);
		case MP_Specular:               return FVector4(1,0,0,0);
		case MP_Roughness:              return FVector4(1,0,0,0);
		case MP_Anisotropy:             return FVector4(1,0,0,0);
		case MP_Normal:                 return FVector4(1,1,1,0);
		case MP_Tangent:                return FVector4(1,1,1,0);
		case MP_WorldPositionOffset:    return FVector4(1,1,1,0);
		case MP_WorldDisplacement:      return FVector4(1,1,1,0);
		case MP_TessellationMultiplier: return FVector4(1,0,0,0);
		case MP_SubsurfaceColor:        return FVector4(1,1,1,0);
		case MP_CustomData0:            return FVector4(1,0,0,0);
		case MP_CustomData1:            return FVector4(1,0,0,0);
		case MP_AmbientOcclusion:       return FVector4(1,0,0,0);
		case MP_Refraction:             return FVector4(1,1,0,0);
		case MP_PixelDepthOffset:       return FVector4(1,0,0,0);
		case MP_ShadingModel:           return FVector4(1,0,0,0);
		default:                        break;
	}

	if (Property.Type >= MP_CustomizedUVs0 && Property.Type <= MP_CustomizedUVs7)
	{
		return FVector4(1,1,0,0);
	}

	if (Property == TEXT("ClearCoatBottomNormal"))
	{
		return FVector4(1,1,1,0);
	}

	if (Property == TEXT("TransmittanceColor"))
	{
		return FVector4(1,1,1,0);
	}

	check(false);
	return FVector4();
#endif
}

const FExpressionInput* FGLTFMaterialUtility::GetInputForProperty(const UMaterialInterface* Material, const FMaterialPropertyEx& Property)
{
	if (Property.IsCustomOutput())
	{
		const UMaterialExpressionCustomOutput* CustomOutput = GetCustomOutputByName(Material, Property.CustomOutput.ToString());
		return CustomOutput != nullptr ? &CastChecked<UMaterialExpressionClearCoatNormalCustomOutput>(CustomOutput)->Input : nullptr;
	}

	UMaterial* UnderlyingMaterial = const_cast<UMaterial*>(Material->GetMaterial());
	return UnderlyingMaterial->GetExpressionInputForProperty(Property.Type);
}

const UMaterialExpressionCustomOutput* FGLTFMaterialUtility::GetCustomOutputByName(const UMaterialInterface* Material, const FString& Name)
{
	// TODO: should we also search inside material functions and attribute layers?

	for (const UMaterialExpression* Expression : Material->GetMaterial()->Expressions)
	{
		const UMaterialExpressionCustomOutput* CustomOutput = Cast<UMaterialExpressionCustomOutput>(Expression);
		if (CustomOutput != nullptr && CustomOutput->GetDisplayName() == Name)
		{
			return CustomOutput;
		}
	}

	return nullptr;
}

FGLTFPropertyBakeOutput FGLTFMaterialUtility::BakeMaterialProperty(const FIntPoint& OutputSize, const FMaterialPropertyEx& Property, const UMaterialInterface* Material, int32 TexCoord, const FGLTFMeshData* MeshData, const FGLTFIndexArray& MeshSectionIndices, bool bFillAlpha, bool bAdjustNormalmaps)
{
	FMeshData MeshSet;
	MeshSet.TextureCoordinateBox = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
	MeshSet.TextureCoordinateIndex = TexCoord;
	MeshSet.MaterialIndices = MeshSectionIndices; // NOTE: MaterialIndices is actually section indices
	if (MeshData != nullptr)
	{
		MeshSet.RawMeshDescription = const_cast<FMeshDescription*>(&MeshData->Description);
		MeshSet.LightMap = MeshData->LightMap;
		MeshSet.LightMapIndex = MeshData->LightMapTexCoord;
		MeshSet.LightmapResourceCluster = MeshData->LightMapResourceCluster;
		MeshSet.PrimitiveData = &MeshData->PrimitiveData;
	}

	FMaterialDataEx MatSet;
	MatSet.Material = const_cast<UMaterialInterface*>(Material);
	MatSet.PropertySizes.Add(Property, OutputSize);
	MatSet.bTangentSpaceNormal = true;

	TArray<FMeshData*> MeshSettings;
	TArray<FMaterialDataEx*> MatSettings;
	MeshSettings.Add(&MeshSet);
	MatSettings.Add(&MatSet);

	TArray<FBakeOutputEx> BakeOutputs;
	IMaterialBakingModule& Module = FModuleManager::Get().LoadModuleChecked<IMaterialBakingModule>("GLTFMaterialBaking");

	Module.SetLinearBake(true);
	Module.BakeMaterials(MatSettings, MeshSettings, BakeOutputs);
	const bool bIsLinearBake = Module.IsLinearBake(Property);
	Module.SetLinearBake(false);

	FBakeOutputEx& BakeOutput = BakeOutputs[0];

	TGLTFSharedArray<FColor> BakedPixels = MakeShared<TArray<FColor>>(MoveTemp(BakeOutput.PropertyData.FindChecked(Property)));
	const FIntPoint BakedSize = BakeOutput.PropertySizes.FindChecked(Property);
	const float EmissiveScale = BakeOutput.EmissiveScale;

	if (bFillAlpha)
	{
		// NOTE: alpha is 0 by default after baking a property, but we prefer 255 (1.0).
		// It makes it easier to view the exported textures.
		for (FColor& Pixel: *BakedPixels)
		{
			Pixel.A = 255;
		}
	}

	if (bAdjustNormalmaps && IsNormalMap(Property))
	{
		// TODO: add support for adjusting normals in baking module instead
		FGLTFTextureUtility::FlipGreenChannel(*BakedPixels);
	}

	bool bFromSRGB = !bIsLinearBake;
	bool bToSRGB = IsSRGB(Property);
	FGLTFTextureUtility::TransformColorSpace(*BakedPixels, bFromSRGB, bToSRGB);

	FGLTFPropertyBakeOutput PropertyBakeOutput(Property, PF_B8G8R8A8, BakedPixels, BakedSize, EmissiveScale, !bIsLinearBake);

	if (BakedPixels->Num() == 1)
	{
		const FColor& Pixel = (*BakedPixels)[0];

		PropertyBakeOutput.bIsConstant = true;
		PropertyBakeOutput.ConstantValue = bToSRGB ? FLinearColor(Pixel) : Pixel.ReinterpretAsLinear();
	}

	return PropertyBakeOutput;
}

FGLTFJsonTextureIndex FGLTFMaterialUtility::AddTexture(FGLTFConvertBuilder& Builder, TGLTFSharedArray<FColor>& Pixels, const FIntPoint& TextureSize, bool bIgnoreAlpha, bool bIsNormalMap, const FString& TextureName, EGLTFJsonTextureFilter MinFilter, EGLTFJsonTextureFilter MagFilter, EGLTFJsonTextureWrap WrapS, EGLTFJsonTextureWrap WrapT)
{
	// TODO: maybe we should reuse existing samplers?
	FGLTFJsonSampler JsonSampler;
	JsonSampler.Name = TextureName;
	JsonSampler.MinFilter = MinFilter;
	JsonSampler.MagFilter = MagFilter;
	JsonSampler.WrapS = WrapS;
	JsonSampler.WrapT = WrapT;

	// TODO: reuse same texture index when image is the same
	FGLTFJsonTexture JsonTexture;
	JsonTexture.Name = TextureName;
	JsonTexture.Sampler = Builder.AddSampler(JsonSampler);
	JsonTexture.Source = Builder.GetOrAddImage(Pixels, TextureSize, bIgnoreAlpha, bIsNormalMap ? EGLTFTextureType::Normalmaps : EGLTFTextureType::None, TextureName);

	return Builder.AddTexture(JsonTexture);
}

FLinearColor FGLTFMaterialUtility::GetMask(const FExpressionInput& ExpressionInput)
{
	return FLinearColor(ExpressionInput.MaskR, ExpressionInput.MaskG, ExpressionInput.MaskB, ExpressionInput.MaskA);
}

uint32 FGLTFMaterialUtility::GetMaskComponentCount(const FExpressionInput& ExpressionInput)
{
	return ExpressionInput.MaskR + ExpressionInput.MaskG + ExpressionInput.MaskB + ExpressionInput.MaskA;
}

bool FGLTFMaterialUtility::TryGetTextureCoordinateIndex(const UMaterialExpressionTextureSample* TextureSampler, int32& TexCoord, FGLTFJsonTextureTransform& Transform)
{
	const UMaterialExpression* Expression = TextureSampler->Coordinates.Expression;
	if (Expression == nullptr)
	{
		TexCoord = TextureSampler->ConstCoordinate;
		Transform = {};
		return true;
	}

	if (const UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(Expression))
	{
		TexCoord = TextureCoordinate->CoordinateIndex;
		Transform.Offset.X = TextureCoordinate->UnMirrorU ? TextureCoordinate->UTiling * 0.5 : 0.0;
		Transform.Offset.Y = TextureCoordinate->UnMirrorV ? TextureCoordinate->VTiling * 0.5 : 0.0;
		Transform.Scale.X = TextureCoordinate->UTiling * (TextureCoordinate->UnMirrorU ? 0.5 : 1.0);
		Transform.Scale.Y = TextureCoordinate->VTiling * (TextureCoordinate->UnMirrorV ? 0.5 : 1.0);
		Transform.Rotation = 0;
		return true;
	}

	// TODO: add support for advanced expression tree (ex UMaterialExpressionTextureCoordinate -> UMaterialExpressionMultiply -> UMaterialExpressionAdd)

	return false;
}

void FGLTFMaterialUtility::GetAllTextureCoordinateIndices(const UMaterialInterface* InMaterial, const FMaterialPropertyEx& InProperty, FGLTFIndexArray& OutTexCoords)
{
	FGLTFMaterialAnalysis Analysis;
	AnalyzeMaterialProperty(InMaterial, InProperty, Analysis);

	const TBitArray<>& TexCoords = Analysis.TextureCoordinates;
	for (int32 Index = 0; Index < TexCoords.Num(); Index++)
	{
		if (TexCoords[Index])
		{
			OutTexCoords.Add(Index);
		}
	}
}

void FGLTFMaterialUtility::AnalyzeMaterialProperty(const UMaterialInterface* InMaterial, const FMaterialPropertyEx& InProperty, FGLTFMaterialAnalysis& OutAnalysis)
{
	if (GetInputForProperty(InMaterial, InProperty) == nullptr)
	{
		OutAnalysis = FGLTFMaterialAnalysis();
		return;
	}

	UGLTFMaterialAnalyzer::AnalyzeMaterialPropertyEx(InMaterial, InProperty.Type, InProperty.CustomOutput.ToString(), OutAnalysis);
}

FMaterialShadingModelField FGLTFMaterialUtility::EvaluateShadingModelExpression(const UMaterialInterface* Material)
{
	FGLTFMaterialAnalysis Analysis;
	AnalyzeMaterialProperty(Material, MP_ShadingModel, Analysis);

	int32 Value;
	if (FDefaultValueHelper::ParseInt(Analysis.ParameterCode, Value))
	{
		return static_cast<EMaterialShadingModel>(Value);
	}

	return Analysis.ShadingModels;
}

#endif

EMaterialShadingModel FGLTFMaterialUtility::GetRichestShadingModel(const FMaterialShadingModelField& ShadingModels)
{
	if (ShadingModels.HasShadingModel(MSM_ClearCoat))
	{
		return MSM_ClearCoat;
	}

	if (ShadingModels.HasShadingModel(MSM_DefaultLit))
	{
		return MSM_DefaultLit;
	}

	if (ShadingModels.HasShadingModel(MSM_Unlit))
	{
		return MSM_Unlit;
	}

	// TODO: add more shading models when conversion supported

	return ShadingModels.GetFirstShadingModel();
}

FString FGLTFMaterialUtility::ShadingModelsToString(const FMaterialShadingModelField& ShadingModels)
{
	FString Result;

	for (uint32 Index = 0; Index < MSM_NUM; Index++)
	{
		const EMaterialShadingModel ShadingModel = static_cast<EMaterialShadingModel>(Index);
		if (ShadingModels.HasShadingModel(ShadingModel))
		{
			FString Name = FGLTFNameUtility::GetName(ShadingModel);
			Result += Result.IsEmpty() ? Name : TEXT(", ") + Name;
		}
	}

	return Result;
}

bool FGLTFMaterialUtility::NeedsMeshData(const UMaterialInterface* Material)
{
#if WITH_EDITOR
	if (Material != nullptr && !IsProxyMaterial(Material))
	{
		// TODO: only analyze properties that will be needed for this specific material
		const TArray<FMaterialPropertyEx> Properties =
		{
			MP_BaseColor,
			MP_EmissiveColor,
			MP_Opacity,
			MP_OpacityMask,
			MP_Metallic,
			MP_Roughness,
			MP_Normal,
			MP_AmbientOcclusion,
			MP_CustomData0,
			MP_CustomData1,
			TEXT("ClearCoatBottomNormal"),
		};

		bool bNeedsMeshData = false;
		FGLTFMaterialAnalysis Analysis;

		// TODO: optimize baking by separating need for vertex data and primitive data

		for (const FMaterialPropertyEx& Property: Properties)
		{
			AnalyzeMaterialProperty(Material, Property, Analysis);
			bNeedsMeshData |= Analysis.bRequiresVertexData;
			bNeedsMeshData |= Analysis.bRequiresPrimitiveData;
		}

		return bNeedsMeshData;
	}
#endif

	return false;
}

bool FGLTFMaterialUtility::NeedsMeshData(const TArray<const UMaterialInterface*>& Materials)
{
#if WITH_EDITOR
	for (const UMaterialInterface* Material: Materials)
	{
		if (NeedsMeshData(Material))
		{
			return true;
		}
	}
#endif

	return false;
}
