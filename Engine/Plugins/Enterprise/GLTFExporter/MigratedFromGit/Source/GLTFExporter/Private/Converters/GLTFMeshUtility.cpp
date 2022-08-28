// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMeshUtility.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "PlatformInfo.h"
#include "StaticMeshAttributes.h"
#include "Developer/MeshMergeUtilities/Private/MeshMergeHelpers.h"

TArray<int32> FGLTFMeshUtility::GetSectionIndices(const FStaticMeshLODResources& MeshLOD, int32 MaterialIndex)
{
	const FStaticMeshLODResources::FStaticMeshSectionArray& Sections = MeshLOD.Sections;

	TArray<int32> SectionIndices;
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

TArray<int32> FGLTFMeshUtility::GetSectionIndices(const FSkeletalMeshLODRenderData& MeshLOD, int32 MaterialIndex)
{
	const TArray<FSkelMeshRenderSection>& Sections = MeshLOD.RenderSections;

	TArray<int32> SectionIndices;
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
		return GetValueForRunningPlatform<int32>(StaticMesh->MinLOD);
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
		return GetValueForRunningPlatform<int32>(SkeletalMesh->MinLod);
	}

	return -1;
}

void FGLTFMeshUtility::RetrieveMesh(USkeletalMesh* SkeletalMesh, int32 LODIndex, FMeshDescription& OutDescription)
{
	// NOTE: this is a workaround for the fact that there's no overload for FGLTFMeshUtility::RetrieveMesh
	// that accepts a USkeletalMesh, only a USkeletalMeshComponent.
	// Writing a custom function that would work on "standalone" skeletal meshes is problematic since
	// we would need to implement an equivalent of USkinnedMeshComponent::GetCPUSkinnedVertices too.

	if (UWorld* World = GEditor->GetEditorWorldContext().World())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transient;
		SpawnParams.bAllowDuringConstructionScript = true;

		if (AActor* Actor = World->SpawnActor<AActor>(SpawnParams))
		{
			USkeletalMeshComponent* SkeletalMeshComponent = NewObject<USkeletalMeshComponent>(Actor, TEXT(""), RF_Transient);
			SkeletalMeshComponent->RegisterComponent();
			SkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);

			FMeshMergeHelpers::RetrieveMesh(SkeletalMeshComponent, LODIndex, OutDescription, true);

			World->DestroyActor(Actor, false, false);
		}
	}
}

template <typename ValueType, typename StructType>
ValueType FGLTFMeshUtility::GetValueForRunningPlatform(const StructType& Properties)
{
	const PlatformInfo::FPlatformInfo& PlatformInfo = GetTargetPlatformManagerRef().GetRunningTargetPlatform()->GetPlatformInfo();
	return Properties.GetValueForPlatformIdentifiers(PlatformInfo.PlatformGroupName, PlatformInfo.VanillaPlatformName);
}
