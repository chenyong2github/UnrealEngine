// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMaterialConverters.h"
#include "Converters/GLTFMaterialUtility.h"
#include "Converters/GLTFMeshUtility.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Tasks/GLTFMaterialTasks.h"

void FGLTFMaterialConverter::Sanitize(const UMaterialInterface*& Material, const UStaticMesh*& Mesh, int32& LODIndex)
{
	// TODO: add support for UStaticMeshComponent, USkeletalMesh and USkeletalMeshComponent too

	if (Mesh != nullptr &&
		Builder.ExportOptions->bBakeMaterialInputs &&
		Builder.ExportOptions->bBakeMaterialInputsUsingMeshData &&
		MaterialNeedsVertexData(const_cast<UMaterialInterface*>(Material)))
	{
		if (LODIndex < 0)
		{
			LODIndex = FMath::Max(Builder.ExportOptions->DefaultLevelOfDetail, FGLTFMeshUtility::GetMinimumLOD(Mesh));
		}
	}
	else
	{
		Mesh = nullptr;
		LODIndex = -1;
	}
}

FGLTFJsonMaterialIndex FGLTFMaterialConverter::Convert(const UMaterialInterface* Material, const UStaticMesh* Mesh, int32 LODIndex)
{
	if (Material == FGLTFMaterialUtility::GetDefault())
	{
		return FGLTFJsonMaterialIndex(INDEX_NONE); // use default gltf definition
	}

	const FGLTFJsonMaterialIndex MaterialIndex = Builder.AddMaterial();
	Builder.SetupTask<FGLTFMaterialTask>(Builder, Material, Mesh, LODIndex, MaterialIndex);
	return MaterialIndex;
}

bool FGLTFMaterialConverter::MaterialNeedsVertexData(UMaterialInterface* Material)
{
	// TODO: only analyze properties that will be needed for this specific material

	const TArray<EMaterialProperty> Properties =
	{
		MP_BaseColor,
		MP_EmissiveColor,
		MP_Opacity,
		MP_OpacityMask,
		MP_Metallic,
		MP_Roughness,
		MP_Normal,
		// MP_CustomOutput,	// NOTE: causes a crash when used with AnalyzeMaterialProperty
		MP_AmbientOcclusion,
		MP_CustomData0,
		MP_CustomData1
	};

	bool bRequiresVertexData = false;

	int32 NumTextureCoordinates;
	bool bPropertyRequiresVertexData;

	for (const EMaterialProperty Property: Properties)
	{
		const_cast<UMaterialInterface*>(Material)->AnalyzeMaterialProperty(Property, NumTextureCoordinates, bPropertyRequiresVertexData);
		bRequiresVertexData |= bPropertyRequiresVertexData;
	}

	return bRequiresVertexData;
}
