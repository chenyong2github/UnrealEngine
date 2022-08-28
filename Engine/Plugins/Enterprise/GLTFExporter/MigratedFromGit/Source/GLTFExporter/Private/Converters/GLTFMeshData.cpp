// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMeshData.h"
#include "Converters/GLTFNameUtility.h"
#include "Converters/GLTFMeshUtility.h"
#include "StaticMeshAttributes.h"
#include "Developer/MeshMergeUtilities/Private/MeshMergeHelpers.h"

FGLTFMeshData::FGLTFMeshData(const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent, int32 LODIndex)
{
	FStaticMeshAttributes(Description).Register();

	if (StaticMeshComponent != nullptr)
	{
		FMeshMergeHelpers::RetrieveMesh(StaticMeshComponent, LODIndex, Description, true);
		Name = FGLTFNameUtility::GetName(StaticMeshComponent);
	}
	else
	{
		FMeshMergeHelpers::RetrieveMesh(StaticMesh, LODIndex, Description);
		StaticMesh->GetName(Name);
	}
}

FGLTFMeshData::FGLTFMeshData(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent, int32 LODIndex)
{
	FStaticMeshAttributes(Description).Register();

	if (SkeletalMeshComponent != nullptr)
	{
		FMeshMergeHelpers::RetrieveMesh(const_cast<USkeletalMeshComponent*>(SkeletalMeshComponent), LODIndex, Description, true);
		Name = FGLTFNameUtility::GetName(SkeletalMeshComponent);
	}
	else
	{
		// NOTE: this is a workaround for the fact that there's no overload for FMeshMergeHelpers::RetrieveMesh
		// that accepts a USkeletalMesh, only a USkeletalMeshComponent.
		// Writing a custom utility function that would work on a "standalone" skeletal mesh is problematic
		// since we would need to implement an equivalent of USkinnedMeshComponent::GetCPUSkinnedVertices too.

		if (UWorld* World = GEditor->GetEditorWorldContext().World())
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParams.ObjectFlags |= RF_Transient;
			SpawnParams.bAllowDuringConstructionScript = true;

			if (AActor* Actor = World->SpawnActor<AActor>(SpawnParams))
			{
				USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(Actor, TEXT(""), RF_Transient);
				Component->RegisterComponent();
				Component->SetSkeletalMesh(const_cast<USkeletalMesh*>(SkeletalMesh));

				FMeshMergeHelpers::RetrieveMesh(Component, LODIndex, Description, true);

				World->DestroyActor(Actor, false, false);
			}
		}

		SkeletalMesh->GetName(Name);
	}
}
