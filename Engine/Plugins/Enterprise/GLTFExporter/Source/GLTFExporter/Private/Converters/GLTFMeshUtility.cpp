// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMeshUtility.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/Material.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "StaticMeshCompiler.h"
#include "SkinnedAssetCompiler.h"

void FGLTFMeshUtility::FullyLoad(const UStaticMesh* InStaticMesh)
{
	UStaticMesh* StaticMesh = const_cast<UStaticMesh*>(InStaticMesh);

#if WITH_EDITOR
	FStaticMeshCompilingManager::Get().FinishCompilation({ StaticMesh });
#endif

	StaticMesh->SetForceMipLevelsToBeResident(30.0f);
	StaticMesh->WaitForStreaming();
}

void FGLTFMeshUtility::FullyLoad(const USkeletalMesh* InSkeletalMesh)
{
	USkeletalMesh* SkeletalMesh = const_cast<USkeletalMesh*>(InSkeletalMesh);

#if WITH_EDITOR
	FSkinnedAssetCompilingManager::Get().FinishCompilation({ SkeletalMesh });
#endif

	SkeletalMesh->SetForceMipLevelsToBeResident(30.0f);
	SkeletalMesh->WaitForStreaming();
}

const UMaterialInterface* FGLTFMeshUtility::GetDefaultMaterial()
{
	static UMaterial* DefaultMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	return DefaultMaterial;
}

const TArray<FStaticMaterial>& FGLTFMeshUtility::GetMaterials(const UStaticMesh* StaticMesh)
{
#if (ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 27)
	return StaticMesh->GetStaticMaterials();
#else
	return StaticMesh->StaticMaterials;
#endif
}

const TArray<FSkeletalMaterial>& FGLTFMeshUtility::GetMaterials(const USkeletalMesh* SkeletalMesh)
{
#if (ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 27)
	return SkeletalMesh->GetMaterials();
#else
	return SkeletalMesh->Materials;
#endif
}

const UMaterialInterface* FGLTFMeshUtility::GetMaterial(const UMaterialInterface* Material)
{
	return Material;
}

const UMaterialInterface* FGLTFMeshUtility::GetMaterial(const FStaticMaterial& Material)
{
	return Material.MaterialInterface;
}

const UMaterialInterface* FGLTFMeshUtility::GetMaterial(const FSkeletalMaterial& Material)
{
	return Material.MaterialInterface;
}

void FGLTFMeshUtility::ResolveMaterials(TArray<const UMaterialInterface*>& Materials, const UStaticMeshComponent* StaticMeshComponent, const UStaticMesh* StaticMesh)
{
	if (StaticMeshComponent != nullptr)
	{
		ResolveMaterials(Materials, StaticMeshComponent->GetMaterials());
	}

	if (StaticMesh != nullptr)
	{
		ResolveMaterials(Materials, GetMaterials(StaticMesh));
	}

	ResolveMaterials(Materials, GetDefaultMaterial());
}

void FGLTFMeshUtility::ResolveMaterials(TArray<const UMaterialInterface*>& Materials, const USkeletalMeshComponent* SkeletalMeshComponent, const USkeletalMesh* SkeletalMesh)
{
	if (SkeletalMeshComponent != nullptr)
	{
		ResolveMaterials(Materials, SkeletalMeshComponent->GetMaterials());
	}

	if (SkeletalMesh != nullptr)
	{
		ResolveMaterials(Materials, GetMaterials(SkeletalMesh));
	}

	ResolveMaterials(Materials, GetDefaultMaterial());
}

template <typename MaterialType>
void FGLTFMeshUtility::ResolveMaterials(TArray<const UMaterialInterface*>& Materials, const TArray<MaterialType>& Defaults)
{
	const int32 Count = Defaults.Num();
	Materials.SetNumZeroed(Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const UMaterialInterface*& Material = Materials[Index];
		if (Material == nullptr)
		{
			Material = GetMaterial(Defaults[Index]);
		}
	}
}

void FGLTFMeshUtility::ResolveMaterials(TArray<const UMaterialInterface*>& Materials, const UMaterialInterface* Default)
{
	for (const UMaterialInterface*& Material : Materials)
	{
		if (Material == nullptr)
		{
			Material = Default;
		}
	}
}

FGLTFIndexArray FGLTFMeshUtility::GetSectionIndices(const UStaticMesh* StaticMesh, int32 LODIndex, int32 MaterialIndex)
{
	if (StaticMesh == nullptr)
	{
		return {};
	}

	const FStaticMeshLODResources& MeshLOD = StaticMesh->GetLODForExport(LODIndex);
	return GetSectionIndices(MeshLOD, MaterialIndex);
}

FGLTFIndexArray FGLTFMeshUtility::GetSectionIndices(const USkeletalMesh* SkeletalMesh, int32 LODIndex, int32 MaterialIndex)
{
	if (SkeletalMesh == nullptr)
	{
		return {};
	}

	const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
	const FSkeletalMeshLODRenderData& MeshLOD = RenderData->LODRenderData[LODIndex];
	return GetSectionIndices(MeshLOD, MaterialIndex);
}

FGLTFIndexArray FGLTFMeshUtility::GetSectionIndices(const FStaticMeshLODResources& MeshLOD, int32 MaterialIndex)
{
	const FStaticMeshSectionArray& Sections = MeshLOD.Sections;

	FGLTFIndexArray SectionIndices;
	SectionIndices.Reserve(Sections.Num());

	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		if (Sections[SectionIndex].MaterialIndex == MaterialIndex)
		{
			SectionIndices.Add(SectionIndex);
		}
	}

	return SectionIndices;
}

FGLTFIndexArray FGLTFMeshUtility::GetSectionIndices(const FSkeletalMeshLODRenderData& MeshLOD, int32 MaterialIndex)
{
	const TArray<FSkelMeshRenderSection>& Sections = MeshLOD.RenderSections;

	FGLTFIndexArray SectionIndices;
	SectionIndices.Reserve(Sections.Num());

	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		if (Sections[SectionIndex].MaterialIndex == MaterialIndex)
		{
			SectionIndices.Add(SectionIndex);
		}
	}

	return SectionIndices;
}

int32 FGLTFMeshUtility::GetLOD(const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent, int32 DefaultLOD)
{
	const int32 ForcedLOD = StaticMeshComponent != nullptr ? StaticMeshComponent->ForcedLodModel - 1 : -1;
	const int32 LOD = ForcedLOD > 0 ? ForcedLOD : FMath::Max(DefaultLOD, GetMinimumLOD(StaticMesh, StaticMeshComponent));
	return FMath::Min(LOD, GetMaximumLOD(StaticMesh));
}

int32 FGLTFMeshUtility::GetLOD(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent, int32 DefaultLOD)
{
	const int32 ForcedLOD = SkeletalMeshComponent != nullptr ? SkeletalMeshComponent->GetForcedLOD() - 1 : -1;
	const int32 LOD = ForcedLOD > 0 ? ForcedLOD : FMath::Max(DefaultLOD, GetMinimumLOD(SkeletalMesh, SkeletalMeshComponent));
	return FMath::Min(LOD, GetMaximumLOD(SkeletalMesh));
}

int32 FGLTFMeshUtility::GetMaximumLOD(const UStaticMesh* StaticMesh)
{
	return StaticMesh != nullptr ? StaticMesh->GetNumLODs() - 1 : -1;
}

int32 FGLTFMeshUtility::GetMaximumLOD(const USkeletalMesh* SkeletalMesh)
{
	if (SkeletalMesh != nullptr)
	{
		const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
		if (RenderData != nullptr)
		{
			return RenderData->LODRenderData.Num() - 1;
		}
	}

	return -1;
}

int32 FGLTFMeshUtility::GetMinimumLOD(const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent)
{
	if (StaticMeshComponent != nullptr && StaticMeshComponent->bOverrideMinLOD)
	{
		return StaticMeshComponent->MinLOD;
	}

	if (StaticMesh != nullptr)
	{
#if (ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 27)
		return StaticMesh->GetMinLOD().Default;
#else
		return StaticMesh->MinLOD.Default;
#endif
	}

	return -1;
}

int32 FGLTFMeshUtility::GetMinimumLOD(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (SkeletalMeshComponent != nullptr && SkeletalMeshComponent->bOverrideMinLod)
	{
		return SkeletalMeshComponent->MinLodModel;
	}

	if (SkeletalMesh != nullptr)
	{
#if (ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 27)
		return SkeletalMesh->GetMinLod().Default;
#else
		return SkeletalMesh->MinLod.Default;
#endif
	}

	return -1;
}
