// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMaterialConverters.h"
#include "Converters/GLTFMaterialUtility.h"
#include "Converters/GLTFMeshUtility.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Tasks/GLTFMaterialTasks.h"

void FGLTFMaterialConverter::Sanitize(const UMaterialInterface*& Material, const FGLTFMeshData*& MeshData, FGLTFMaterialArray& Materials)
{
	if (MeshData == nullptr ||
		!Builder.ExportOptions->bBakeMaterialInputs ||
		!Builder.ExportOptions->bBakeMaterialInputsUsingMeshData ||
		!FGLTFMaterialUtility::MaterialNeedsVertexData(Material))
	{
		MeshData = nullptr;
		Materials = {};
	}
}

FGLTFJsonMaterialIndex FGLTFMaterialConverter::Convert(const UMaterialInterface* Material, const FGLTFMeshData* MeshData, FGLTFMaterialArray Materials)
{
	if (Material == FGLTFMaterialUtility::GetDefault())
	{
		return FGLTFJsonMaterialIndex(INDEX_NONE); // use default gltf definition
	}

	const FGLTFJsonMaterialIndex MaterialIndex = Builder.AddMaterial();
	Builder.SetupTask<FGLTFMaterialTask>(Builder, Material, MeshData, Materials, MaterialIndex);
	return MaterialIndex;
}
