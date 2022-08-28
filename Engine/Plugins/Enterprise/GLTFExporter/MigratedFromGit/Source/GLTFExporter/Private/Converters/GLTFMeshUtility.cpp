// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMeshUtility.h"
#include "Rendering/SkeletalMeshRenderData.h"

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
	const FStaticMeshLODResources::FStaticMeshSectionArray& Sections = MeshLOD.Sections;

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
