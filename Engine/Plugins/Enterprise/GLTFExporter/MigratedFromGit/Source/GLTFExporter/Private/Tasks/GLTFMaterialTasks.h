// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tasks/GLTFTask.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Converters/GLTFUVOverlapChecker.h"
#include "Converters/GLTFMeshData.h"
#if WITH_EDITOR
#include "MaterialPropertyEx.h"
#endif

struct FGLTFPropertyBakeOutput;

class FGLTFMaterialTask : public FGLTFTask
{
public:

	FGLTFMaterialTask(FGLTFConvertBuilder& Builder, FGLTFUVOverlapChecker& UVOverlapChecker, const UMaterialInterface* Material, const FGLTFMeshData* MeshData, FGLTFIndexArray SectionIndices, FGLTFJsonMaterialIndex MaterialIndex)
		: FGLTFTask(EGLTFTaskPriority::Material)
		, Builder(Builder)
		, UVOverlapChecker(UVOverlapChecker)
		, Material(Material)
		, MeshData(MeshData)
		, SectionIndices(SectionIndices)
		, MaterialIndex(MaterialIndex)
	{
	}

	virtual FString GetName() override
	{
		return Material->GetName();
	}

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	FGLTFUVOverlapChecker& UVOverlapChecker;
	const UMaterialInterface* Material;
	const FGLTFMeshData* MeshData;
	const FGLTFIndexArray SectionIndices;
	const FGLTFJsonMaterialIndex MaterialIndex;

	FString GetMaterialName() const;
	FString GetBakedTextureName(const FString& PropertyName) const;

	void ApplyPrebakedProperties(FGLTFJsonMaterial& OutMaterial) const;
	void ApplyPrebakedProperty(const FString& PropertyName, float& OutValue) const;
	void ApplyPrebakedProperty(const FString& PropertyName, FGLTFJsonColor3& OutValue) const;
	void ApplyPrebakedProperty(const FString& PropertyName, FGLTFJsonColor4& OutValue) const;
	void ApplyPrebakedProperty(const FString& PropertyName, FGLTFJsonTextureInfo& OutValue) const;

	EMaterialShadingModel GetShadingModel() const;
	void ConvertShadingModel(EGLTFJsonShadingModel& OutShadingModel) const;
	void ConvertAlphaMode(EGLTFJsonAlphaMode& OutAlphaMode, EGLTFJsonBlendMode& OutBlendMode) const;

#if WITH_EDITOR
	TSet<FMaterialPropertyEx> MeshDataBakedProperties;

	bool TryGetBaseColorAndOpacity(FGLTFJsonPBRMetallicRoughness& OutPBRParams, const FMaterialPropertyEx& BaseColorProperty, const FMaterialPropertyEx& OpacityProperty);
	bool TryGetMetallicAndRoughness(FGLTFJsonPBRMetallicRoughness& OutPBRParams, const FMaterialPropertyEx& MetallicProperty, const FMaterialPropertyEx& RoughnessProperty);
	bool TryGetClearCoatRoughness(FGLTFJsonClearCoatExtension& OutExtParams, const FMaterialPropertyEx& IntensityProperty, const FMaterialPropertyEx& RoughnessProperty);
	bool TryGetEmissive(FGLTFJsonMaterial& JsonMaterial, const FMaterialPropertyEx& EmissiveProperty);

	bool IsPropertyNonDefault(const FMaterialPropertyEx& Property) const;
	bool TryGetConstantColor(FGLTFJsonColor3& OutValue, const FMaterialPropertyEx& Property) const;
	bool TryGetConstantColor(FGLTFJsonColor4& OutValue, const FMaterialPropertyEx& Property) const;
	bool TryGetConstantColor(FLinearColor& OutValue, const FMaterialPropertyEx& Property) const;
	bool TryGetConstantScalar(float& OutValue, const FMaterialPropertyEx& Property) const;

	bool TryGetSourceTexture(FGLTFJsonTextureInfo& OutTexInfo, const FMaterialPropertyEx& Property, const TArray<FLinearColor>& AllowedMasks = {}) const;
	bool TryGetSourceTexture(const UTexture2D*& OutTexture, int32& OutTexCoord, FGLTFJsonTextureTransform& OutTransform, const FMaterialPropertyEx& Property, const TArray<FLinearColor>& AllowedMasks = {}) const;

	bool TryGetBakedMaterialProperty(FGLTFJsonTextureInfo& OutTexInfo, FGLTFJsonColor3& OutConstant, const FMaterialPropertyEx& Property, const FString& PropertyName);
	bool TryGetBakedMaterialProperty(FGLTFJsonTextureInfo& OutTexInfo, FGLTFJsonColor4& OutConstant, const FMaterialPropertyEx& Property, const FString& PropertyName);
	bool TryGetBakedMaterialProperty(FGLTFJsonTextureInfo& OutTexInfo, float& OutConstant, const FMaterialPropertyEx& Property, const FString& PropertyName);
	bool TryGetBakedMaterialProperty(FGLTFJsonTextureInfo& OutTexInfo, const FMaterialPropertyEx& Property, const FString& PropertyName);

	FGLTFPropertyBakeOutput BakeMaterialProperty(const FMaterialPropertyEx& Property, int32& OutTexCoord);
	FGLTFPropertyBakeOutput BakeMaterialProperty(const FMaterialPropertyEx& Property, int32& OutTexCoord, const FIntPoint& TextureSize, bool bFillAlpha);

	bool StoreBakedPropertyTexture(FGLTFJsonTextureInfo& OutTexInfo, FGLTFPropertyBakeOutput& PropertyBakeOutput, const FString& PropertyName) const;

	static EGLTFMaterialPropertyGroup GetPropertyGroup(const FMaterialPropertyEx& Property);

	template <typename CallbackType>
	static void CombinePixels(const TArray<FColor>& FirstPixels, const TArray<FColor>& SecondPixels, TArray<FColor>& OutPixels, CallbackType Callback);

#endif
};
