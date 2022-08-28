// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMaterialConverters.h"
#include "Converters/GLTFMaterialUtility.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Tasks/GLTFMaterialTasks.h"

void FGLTFMaterialConverter::Sanitize(const UMaterialInterface*& Material, const FGLTFMeshData*& MeshData, TArray<int32>& SectionIndices)
{
	if (MeshData == nullptr ||
		Builder.ExportOptions->BakeMaterialInputs != EGLTFMaterialBakeMode::UseMeshData ||
		!FGLTFMaterialUtility::NeedsMeshData(Material))
	{
		MeshData = nullptr;
		SectionIndices = {};
	}

	if (MeshData != nullptr)
	{
		const FMeshDescription& MeshDescription = MeshData->GetParent()->Description;
		for (const int32 SectionIndex : SectionIndices)
		{
			const bool bHasDegenerateUVs = UVDegenerateChecker.GetOrAdd(&MeshDescription, SectionIndex, MeshData->TexCoord);
			if (bHasDegenerateUVs)
			{
				Builder.AddWarningMessage(FString::Printf(
					TEXT("Material %s is using mesh data from %s but the lightmap UVs (channel %d) are degenerate (in mesh section %d). Simple baking will be used as fallback"),
					*Material->GetName(),
					*MeshData->GetParent()->Name,
					MeshData->TexCoord,
					SectionIndex));

				MeshData = nullptr;
				SectionIndices = {};
				break;
			}
		}
	}
}

FGLTFJsonMaterialIndex FGLTFMaterialConverter::Convert(const UMaterialInterface* Material, const FGLTFMeshData* MeshData, TArray<int32> SectionIndices)
{
	if (Material == FGLTFMaterialUtility::GetDefault())
	{
		return FGLTFJsonMaterialIndex(INDEX_NONE); // use default gltf definition
	}

	const FGLTFJsonMaterialIndex MaterialIndex = Builder.AddMaterial();
	Builder.SetupTask<FGLTFMaterialTask>(Builder, UVOverlapChecker, Material, MeshData, SectionIndices, MaterialIndex);
	return MaterialIndex;
}
